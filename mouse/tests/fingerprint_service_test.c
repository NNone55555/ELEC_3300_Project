/* Real EF01 driver, two-capture FSM and async service. UART/module simulated. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fingerprint_service.h"
GPIO_TypeDef test_gpio[5];
uint32_t test_primask;
static uint32_t tick;
static UART_HandleTypeDef uart;
static uint8_t *rx_target, wire[400], slots[300];
static unsigned wire_size, wire_index, stores, deletes, yielded;
static bool finger, unmatched = true, combine_error, delete_error, store_error;
static unsigned malformed, match_id, command_count;
static uint16_t U16(const uint8_t *p)
{
  return ((uint16_t)p[0] << 8) | p[1];
}
uint32_t HAL_GetTick(void)
{
  return tick;
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *data, uint16_t n)
{
  assert(h == &uart && n == 1);
  rx_target = data;
  h->RxState = HAL_UART_STATE_BUSY_RX;
  return HAL_OK;
}
static void Deliver(uint8_t byte)
{
  assert(!test_primask);
  *rx_target = byte;
  uart.RxState = HAL_UART_STATE_READY;
  Zw3020_OnRxComplete(&uart);
}
void osDelay(uint32_t ms)
{
  for (unsigned i = 0; i < ms; ++i)
  {
    ++tick;
    ++yielded;
    for (unsigned b = 0; b < 6 && wire_index < wire_size; ++b)
      Deliver(wire[wire_index++]);
  }
}
static void Ack(uint8_t code, const uint8_t *data, unsigned n)
{
  const uint8_t header[] = {0xEF, 1, 0xFF, 0xFF, 0xFF, 0xFF, 7, 0, (uint8_t)(n + 3)};
  memcpy(wire, header, 9);
  wire[9] = code;
  memcpy(wire + 10, data, n);
  unsigned sum = 0;
  for (unsigned i = 6; i < 10 + n; ++i)
    sum += wire[i];
  wire[10 + n] = sum >> 8;
  wire[11 + n] = sum;
  wire_size = 12 + n;
  wire_index = 0;
  switch (malformed)
  {
    case 1:
      wire[wire_size - 1] ^= 1;
      break;
    case 2:
      wire[6] = 2;
      break;
    case 3:
      wire[2] = 0;
      break;
    case 4:
      wire[7] = 0xFF;
      wire[8] = 0xFF;
      break;
    case 5:
      wire_size = 10;
      break;
    case 6:
      wire_size = 0;
      break;
    case 7:
      for (unsigned i = 0; i < 300; ++i)
        Deliver((uint8_t)i);
      wire_size = 0;
      break;
    case 8:
      uart.RxState = HAL_UART_STATE_READY;
      Zw3020_OnUartError(&uart);
      wire_size = 0;
      break;
  }
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h, const uint8_t *data, uint16_t n,
                                    uint32_t timeout)
{
  (void)timeout;
  assert(h == &uart && n >= 12 && data[0] == 0xEF && data[1] == 1 && data[6] == 1 &&
         data[8] == n - 9);
  unsigned sum = 0;
  for (unsigned i = 6; i < n - 2U; ++i)
    sum += data[i];
  assert(sum == U16(data + n - 2));
  ++command_count;
  uint8_t out[32] = {0}, code = 0;
  unsigned size = 0;
  switch (data[9])
  {
    case 0x13:
      assert(n == 16 && !data[10] && !data[13]);
      break;
    case 0x0F:
      size = 16;
      out[4] = 300 >> 8;
      out[5] = 300 & 255;
      break;
    case 0x1D:
    {
      size = 2;
      unsigned count = 0;
      for (unsigned i = 0; i < 300; ++i)
        count += slots[i];
      out[0] = count >> 8;
      out[1] = count;
      break;
    }
    case 0x1F:
      assert(n == 13 && data[10] < 2);
      size = 32;
      for (unsigned bit = 0; bit < 256; ++bit)
      {
        unsigned id = data[10] * 256 + bit;
        if (id < 300 && slots[id])
          out[bit / 8] |= 1U << (bit % 8);
      }
      break;
    case 0x01:
      code = finger ? 0 : 2;
      break;
    case 0x02:
      assert(n == 13 && (data[10] == 1 || data[10] == 2));
      break;
    case 0x04:
      assert(n == 17 && data[10] == 1 && !U16(data + 11) && U16(data + 13) == 300);
      if (unmatched)
        code = 9;
      else
      {
        size = 4;
        out[0] = match_id >> 8;
        out[1] = match_id;
        out[3] = 99;
      }
      break;
    case 0x05:
      code = combine_error ? 0x0A : 0;
      break;
    case 0x06:
    {
      unsigned id = U16(data + 11);
      assert(n == 15 && data[10] == 1 && id < 300 && !slots[id]);
      if (store_error)
        code = 0x18;
      else
      {
        slots[id] = 1;
        ++stores;
      }
      break;
    }
    case 0x0C:
    {
      unsigned id = U16(data + 10);
      assert(n == 16 && U16(data + 12) == 1 && id < 300);
      ++deletes;
      if (delete_error)
        code = 1;
      else
        slots[id] = 0;
      break;
    }
    default:
      assert(!"Unexpected command, including forbidden erase-all");
  }
  Ack(code, out, size);
  return HAL_OK;
}
static bool Step(FingerprintController *c, uint16_t *id)
{
  osDelay(100);
  return FpService_Step(c, false, id);
}
static void Run(FingerprintController *c, unsigned count)
{
  uint16_t id;
  for (unsigned i = 0; i < count; ++i)
    assert(!Step(c, &id));
}
static void Cancel(FingerprintController *c)
{
  FpService_Cancel();
  Run(c, 1);
  assert(FpService_GetStatus().reply == FP_REPLY_CANCELLED);
}
static void Prepare(FingerprintController *c)
{
  assert(FpService_Request(FP_JOB_ENROLL, 0));
  Run(c, 1);
  assert(c->state == FP_WAIT_FIRST_SCAN);
  finger = true;
  Run(c, 3);
  assert(c->state == FP_WAIT_FIRST_REMOVAL);
  finger = false;
  Run(c, 1);
  assert(c->state == FP_WAIT_SECOND_SCAN);
  finger = true;
  Run(c, 3);
}
int main(void)
{
  FingerprintController c;
  uint16_t id = UINT16_MAX;
  slots[0] = 1;
  assert(Zw3020_Init(&uart));
  Fingerprint_Init(&c);
  Run(&c, 1);
  assert(c.storage.capacity == 300 && c.storage.count == 1);
  FpService_SetConfigurationMode(true);
  Prepare(&c);
  assert(FpService_GetStatus().reply == FP_REPLY_PREPARED && FpService_GetStatus().id == 1 &&
         stores == 0 && !slots[1]);
  unsigned before = command_count;
  Run(&c, 20);
  assert(command_count == before); /* no extraction can overwrite prepared RAM */
  Cancel(&c);
  assert(stores == 0 && slots[0] && !slots[1]);
  assert(FpService_Request(FP_JOB_STORE, 1));
  Run(&c, 1);
  assert(FpService_GetStatus().reply == FP_REPLY_ERROR && stores == 0);
  Prepare(&c);
  assert(FpService_Request(FP_JOB_STORE, 1));
  Run(&c, 1);
  assert(FpService_GetStatus().reply == FP_REPLY_ENROLLED && stores == 1 && slots[0] && slots[1]);
  unmatched = false;
  match_id = 0;
  assert(FpService_Request(FP_JOB_ENROLL, 0));
  Run(&c, 4);
  assert(FpService_GetStatus().reply == FP_REPLY_DUPLICATE && FpService_GetStatus().id == 0 &&
         stores == 1);
  assert(FpService_Request(FP_JOB_VERIFY, 1));
  Run(&c, 3);
  assert(FpService_GetStatus().reply == FP_REPLY_MISMATCH);
  assert(FpService_Request(FP_JOB_VERIFY, 0));
  Run(&c, 3);
  assert(FpService_GetStatus().reply == FP_REPLY_VERIFIED);
  unmatched = true;
  combine_error = true;
  Prepare(&c);
  assert(FpService_GetStatus().reply == FP_REPLY_ERROR && stores == 1);
  combine_error = false;
  Prepare(&c);
  store_error = true;
  assert(FpService_Request(FP_JOB_STORE, 2));
  Run(&c, 1);
  assert(FpService_GetStatus().reply == FP_REPLY_ERROR && !slots[2]);
  store_error = false;
  assert(FpService_Request(FP_JOB_DELETE, 1));
  delete_error = true;
  Run(&c, 1);
  assert(FpService_GetStatus().reply == FP_REPLY_ERROR && slots[1]);
  delete_error = false;
  assert(FpService_Request(FP_JOB_DELETE, 1));
  Run(&c, 1);
  assert(FpService_GetStatus().reply == FP_REPLY_DELETED && !slots[1] && slots[0]);
  before = deletes;
  assert(FpService_Request(FP_JOB_DELETE, 1));
  Run(&c, 1);
  assert(deletes == before && FpService_GetStatus().reply == FP_REPLY_DELETED);
  assert(FpService_Request(FP_JOB_VERIFY, 0));
  finger = false;
  Run(&c, 1);
  tick += 30000;
  Run(&c, 1);
  assert(FpService_GetStatus().reply == FP_REPLY_ERROR);
  for (malformed = 1; malformed <= 8; ++malformed)
    assert(Zw3020_GetImage() < 0);
  malformed = 0;
  memset(slots, 1, 256);
  assert(FpService_Request(FP_JOB_ENROLL, 0));
  Run(&c, 1);
  assert(c.enrollment_id == 256);
  Cancel(&c);
  slots[299] = 1;
  bool occupied = false;
  assert(Zw3020_IsOccupied(299, &occupied) == 0 && occupied);
  assert(Zw3020_Delete(299) == 0 && !slots[299] && slots[0]);
  FpService_SetConfigurationMode(false);
  unmatched = false;
  match_id = 0;
  finger = false;
  Run(&c, 1);
  finger = true;
  bool matched = false;
  for (unsigned i = 0; i < 4; ++i)
    if (Step(&c, &id))
    {
      assert(!matched);
      matched = true;
    }
  assert(matched && id == 0);
  for (unsigned i = 0; i < 10; ++i)
    assert(!Step(&c, &id));
  tick = UINT32_MAX - 3;
  assert(Zw3020_GetImage() == 0);
  assert(yielded > 100);
  puts("PASS: real EF01 packets/checksums, selected-ID verification, duplicate detection, two-scan "
       "compatibility, prepared RAM/Save/Cancel, store errors, single-template delete + bitmap "
       "verification, multi-page index, timeout/malformed/overflow/UART errors, normal match once "
       "per placement, tick wrap.");
  return 0;
}
