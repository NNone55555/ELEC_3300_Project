/* Real menu, console, storage and manager; simulated EEPROM bus, fingerprint
 * service and runtime peripherals. No simulated result is a hardware claim. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config_menu.h"
#include "console_uart.h"
#include "profile_manager.h"
#include "fingerprint_service.h"
#include "mouse_app.h"
#include "ws2812b.h"
#include "login_typing.h"
#include "usb_hid_controller.h"

uint32_t test_primask;
GPIO_TypeDef test_gpio[5];
static uint32_t tick;
RCC_TypeDef test_rcc;
static I2C_TypeDef i2c_registers;
static I2C_HandleTypeDef bus = {.Instance = &i2c_registers};
static bool stop_pending, stop_stuck;
static unsigned stop_waits;
static unsigned stop_model;
static UART_HandleTypeDef uart;
static uint8_t memory[256], *rx_target;
static const uint8_t *tx_target;
static uint16_t tx_length;
static char transcript[200000];
static size_t transcript_size;
static int write_budget = -1, writes, io_error, corrupt_read, ack_delay;
static uint32_t i2c_error;
static bool tx_busy;
static uint16_t dpi = 800;
static uint8_t macros[2], credential_requests;
static WS2812B_Config rgb;
static FpServiceStatus fp;
static bool cancel_requested;
static unsigned store_requests, delete_requests;
static uint16_t deleted_id;
uint32_t HAL_GetTick(void)
{
  return tick;
}
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *h)
{
  assert(h == &bus);
  return i2c_error;
}
void osDelay(uint32_t ms)
{
  tick += ms;
  if (stop_pending)
  {
    assert((RCC->AHBENR & RCC_AHBENR_FSMCEN) == 0U);
    ++stop_waits;
    if (!stop_stuck)
    {
      i2c_registers.CR1 &= ~I2C_CR1_STOP;
      i2c_registers.SR2 &= ~I2C_SR2_BUSY;
      stop_pending = false;
    }
  }
}
void Test_DataBarrier(void)
{
  /* Restoring LCD before a successful STOP would reproduce the silicon issue. */
  if ((RCC->AHBENR & RCC_AHBENR_FSMCEN) != 0U && !stop_stuck)
    assert(!stop_pending);
}
static void AssertI2cWindow(void)
{
  assert((RCC->AHBENR & RCC_AHBENR_FSMCEN) == 0U);
  assert(!stop_pending);
  assert(test_primask == 0U);
}
static void PendingStop(void)
{
  if (stop_model != 2U)
    i2c_registers.CR1 |= I2C_CR1_STOP;
  if (stop_model != 1U)
    i2c_registers.SR2 |= I2C_SR2_BUSY;
  stop_pending = true;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t dev, uint16_t addr,
                                   uint16_t width, uint8_t *data, uint16_t n, uint32_t timeout)
{
  (void)timeout;
  AssertI2cWindow();
  assert(h == &bus && dev == 0xA0 && width == I2C_MEMADD_SIZE_8BIT && addr + n <= 256);
  if (io_error)
    return HAL_ERROR;
  memcpy(data, memory + addr, n);
  if (corrupt_read && n)
    data[0] ^= 1;
  PendingStop();
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t dev, uint16_t addr,
                                    uint16_t width, uint8_t *data, uint16_t n, uint32_t timeout)
{
  (void)timeout;
  AssertI2cWindow();
  assert(h == &bus && dev == 0xA0 && width == I2C_MEMADD_SIZE_8BIT);
  assert(n > 0 && addr + n <= 224 && addr / 8 == (addr + n - 1U) / 8);
  if (io_error)
    return HAL_ERROR;
  ++writes;
  for (unsigned i = 0; i < n; ++i)
  {
    if (write_budget == 0)
      return HAL_ERROR;
    memory[addr + i] = data[i];
    if (write_budget > 0)
      --write_budget;
  }
  PendingStop();
  return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t dev, uint32_t trials,
                                        uint32_t timeout)
{
  (void)timeout;
  AssertI2cWindow();
  assert(h == &bus && dev == 0xA0 && trials == 1);
  if (ack_delay)
  {
    --ack_delay;
    return HAL_ERROR;
  }
  if (io_error)
    return HAL_ERROR;
  PendingStop();
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h, uint8_t *data, uint16_t n)
{
  assert(h == &uart && n == 1);
  rx_target = data;
  h->RxState = HAL_UART_STATE_BUSY_RX;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h, const uint8_t *data, uint16_t n)
{
  assert(h == &uart && tx_length == 0);
  if (tx_busy)
    return HAL_BUSY;
  tx_target = data;
  tx_length = n;
  return HAL_OK;
}
static void Drain(void)
{
  while (tx_length)
  {
    unsigned n = tx_length;
    assert(transcript_size + n < sizeof(transcript));
    memcpy(transcript + transcript_size, tx_target, n);
    transcript_size += n;
    transcript[transcript_size] = 0;
    tx_length = 0;
    Console_OnTxComplete(&uart);
  }
}
static void Rx(char ch)
{
  assert(rx_target);
  *rx_target = (uint8_t)ch;
  uart.RxState = HAL_UART_STATE_READY;
  Console_OnRxComplete(&uart);
}
bool MouseApp_SetDpi(uint16_t value)
{
  assert(UserProfile_ValidDpi(value));
  dpi = value;
  return true;
}
uint16_t MouseApp_GetDpi(void)
{
  return dpi;
}
HAL_StatusTypeDef WS2812B_SetConfig(const WS2812B_Config *value)
{
  rgb = *value;
  return HAL_OK;
}
WS2812B_Config WS2812B_GetConfig(void)
{
  return rgb;
}
HAL_StatusTypeDef WS2812B_SetMode(WS2812B_Mode mode)
{
  rgb.mode = mode;
  return HAL_OK;
}
void UsbHid_SetSideMacros(uint8_t a, uint8_t b)
{
  macros[0] = a;
  macros[1] = b;
}
void UsbHid_GetSideMacros(uint8_t *a, uint8_t *b)
{
  *a = macros[0];
  *b = macros[1];
}
bool Login_RequestPassword(const char *text)
{
  assert(UserProfile_ValidPassword(text));
  ++credential_requests;
  return true;
}
void Login_Cancel(void) {}
void FpService_SetConfigurationMode(bool enabled)
{
  (void)enabled;
}
bool FpService_Request(FpJob job, uint16_t id)
{
  if (fp.reply == FP_REPLY_BUSY)
    return false;
  fp = (FpServiceStatus){.job = job, .reply = FP_REPLY_BUSY, .id = id};
  if (job == FP_JOB_STORE)
    ++store_requests;
  if (job == FP_JOB_DELETE)
  {
    ++delete_requests;
    deleted_id = id;
  }
  return true;
}
FpServiceStatus FpService_GetStatus(void)
{
  return fp;
}
void FpService_Cancel(void)
{
  cancel_requested = true;
  fp.reply = FP_REPLY_BUSY;
}
static void Poll(void)
{
  if (cancel_requested)
  {
    cancel_requested = false;
    fp.reply = FP_REPLY_CANCELLED;
  }
  tick += 10;
  Console_Service();
  ConfigMenu_Poll();
  ProfileManager_Update();
  Drain();
  assert(test_primask == 0);
  assert((RCC->AHBENR & RCC_AHBENR_FSMCEN) != 0U);
}
static void Input(const char *text)
{
  for (; *text; ++text)
    Rx(*text);
  Rx('\r');
  for (unsigned i = 0; i < 12; ++i)
    Poll();
}
static void Expect(const char *part)
{
  if (!strstr(transcript, part))
  {
    fprintf(stderr, "Missing menu output: %s\n", part);
    abort();
  }
}
static void ResetTranscript(void)
{
  transcript_size = 0;
  transcript[0] = 0;
}
static void Reply(FpReply result, uint16_t id)
{
  fp.reply = result;
  fp.id = id;
  Poll();
  Poll();
}
static void Blank(void)
{
  RCC->AHBENR = RCC_AHBENR_FSMCEN | 1U;
  i2c_registers = (I2C_TypeDef){0};
  stop_pending = stop_stuck = false;
  stop_model = 0;
  memset(memory, 0xFF, sizeof(memory));
  for (unsigned i = 224; i < 256; ++i)
    memory[i] = (uint8_t)i;
  write_budget = -1;
  io_error = corrupt_read = ack_delay = 0;
  i2c_error = 0;
  writes = 0;
  fp = (FpServiceStatus){0};
  ProfileManager_Init(&bus);
  assert((RCC->AHBENR & RCC_AHBENR_FSMCEN) != 0U);
  ConfigMenu_Init();
  Drain();
}
static void RawInput(const char *bytes)
{
  while (*bytes)
    Rx(*bytes++);
  for (unsigned i = 0; i < 12; ++i)
    Poll();
}
static void NewlineHandling(void)
{
  static const char *const endings[] = {"\r", "\n", "\r\n", "\n\r", "\r\r\n"};
  for (unsigned i = 0; i < sizeof(endings) / sizeof(endings[0]); ++i)
  {
    Blank();
    RawInput("config\r\n");
    ResetTranscript();
    Rx('1');
    RawInput(endings[i]);
    Expect("ADD NEW USER");
    assert(!strstr(transcript, "Invalid option"));
    ResetTranscript();
    RawInput("\r\n\n\r\r\n   \r\n");
    assert(!strstr(transcript, "Invalid option") && !strstr(transcript, "ADD NEW USER"));
    Rx('1');
    RawInput(endings[i]);
    Expect("Please enter a display name");
    assert(!strstr(transcript, "Invalid option"));
    Input("0");
  }
  Blank();
  RawInput("config\r\n");
  Input("1");
  Input("1");
  Input("Defaults");
  Reply(FP_REPLY_PREPARED, 7);
  static const char *const keep[] = {"9", "5", "9", "102", "6", "9", "2", "8", "8"};
  for (unsigned step = 1; step <= 9; ++step)
  {
    char expected[32];
    ResetTranscript();
    /* Empty/duplicate Enter cannot choose a default on any wizard field. */
    RawInput("\r\r\n\n\r");
    assert(!strstr(transcript, "REGISTRATION") && !strstr(transcript, "NEW USER SUMMARY"));
    for (const char *p = keep[step - 1U]; *p; ++p)
      Rx(*p);
    RawInput("\r\r\n");
    if (step < 9)
    {
      snprintf(expected, sizeof(expected), "REGISTRATION %u/9", step + 1U);
      Expect(expected);
      assert(!strstr(transcript, "NEW USER SUMMARY"));
    }
    else
      Expect("NEW USER SUMMARY");
  }
  Input("2");
  puts("PASS: CR/LF/CRLF/LFCR/duplicate terminators and empty menu lines do not trigger invalid "
       "choices; wizard defaults require explicit numbered choices.");
}
static void EepromDiagnostics(void)
{
  Blank();
  io_error = 1;
  i2c_error = 4;
  GPIOB->levels = GPIO_PIN_6 | GPIO_PIN_7;
  ProfileManager_Init(&bus);
  ConfigMenu_Init();
  ResetTranscript();
  RawInput("config\r\n");
  Expect("read at 0x00, HAL=1 error=0x00000004 SCL=1 SDA=1");
  Expect("Profile list unavailable");
  At24c02Diagnostic failure = At24c02_GetDiagnostic();
  assert(failure.address == 0 && failure.hal_status == HAL_ERROR && failure.hal_error == 4);
  GPIOB->levels = 0;
  assert(At24c02_GetDiagnostic().scl_high && At24c02_GetDiagnostic().sda_high);
  Input("1");
  Input("1");
  Expect("Registration blocked: EEPROM is not ready");
  assert(writes == 0 && fp.job == FP_JOB_NONE);
  Input("2");
  Input("3");
  assert(ProfileStorage_Status() == STORAGE_IO_ERROR);
  io_error = 0;
  i2c_error = 0;
  Input("3");
  assert(ProfileStorage_Status() == STORAGE_OK && writes == 0);
  /* Nonprintable input remains an error rather than being silently accepted. */
  Input("1");
  ResetTranscript();
  Rx('1');
  Rx(0);
  RawInput("\r\n");
  Expect("discarded");
  assert(!strstr(transcript, "Please enter a display name"));
  Input("2");
  Input("2");
  puts("PASS: failed EEPROM read shows HAL/error/address/pins, blocks enrollment, retries without "
       "writes; embedded NUL input is discarded.");
}
static void RegistrationInputBoundaries(void)
{
  Blank();
  Input("config");
  Input("1");
  Input("1");
  Input("Marcus");
  Reply(FP_REPLY_PREPARED, 9);
  Input("1600");
  Input("2");
  Input("3");
  Input("10");
  Input("2");
  Input("Marcus-PC");
  Input("1");
  ResetTranscript();
  Input("bad!");
  Expect("Invalid password");
  assert(!strstr(transcript, "Passwords do not match") && !strstr(transcript, "bad!"));
  Input("1");
  ResetTranscript();
  RawInput("\r\r\n");
  assert(!strstr(transcript, "REGISTRATION 8/9"));
  Input("Alpha123");
  Expect("Confirm new password");
  Input("Beta456");
  Expect("Passwords do not match");
  Input("1");
  Input("Alpha123");
  Input("Alpha123");
  ResetTranscript();
  RawInput("3\r\r\n");
  Expect("Side Button 2");
  assert(!strstr(transcript, "NEW USER SUMMARY"));
  ResetTranscript();
  RawInput("\n\r\r\n");
  assert(!strstr(transcript, "NEW USER SUMMARY"));
  Input("4");
  Expect("NEW USER SUMMARY");
  Expect("Side Button 1: Ctrl+C");
  Expect("Side Button 2: Ctrl+V");
  assert(writes == 0);
  Input("1");
  assert(fp.job == FP_JOB_STORE);
  Reply(FP_REPLY_ENROLLED, 9);
  Expect("New user saved and verified");
  UserProfile saved;
  assert(ProfileStorage_Find(9, &saved) && !strcmp(saved.password, "Alpha123"));
  assert(saved.side1_macro == MACRO_COPY && saved.side2_macro == MACRO_PASTE);
  puts("PASS: unsupported first password and confirmation mismatch are distinct; hidden input "
       "stays hidden; duplicate/empty lines cannot skip Side Button 2 or save registration.");
}
static void FsmcWorkaround(void)
{
  Blank();
  uint8_t byte = 0;
  unsigned waits = stop_waits;
  assert(At24c02_Read(0, &byte, 1) && stop_waits > waits);
  assert(RCC->AHBENR == (RCC_AHBENR_FSMCEN | 1U));
  /* Nested verification must not re-enable a clock disabled by its caller. */
  RCC->AHBENR &= ~RCC_AHBENR_FSMCEN;
  assert(At24c02_WriteVerified(0, &byte, 1));
  assert(RCC->AHBENR == 1U);
  assert(At24c02_Read(0, &byte, 1) && RCC->AHBENR == 1U);
  Blank();
  io_error = 1;
  assert(!At24c02_Read(0, &byte, 1) && (RCC->AHBENR & RCC_AHBENR_FSMCEN));
  assert(!At24c02_WriteVerified(0, &byte, 1) && (RCC->AHBENR & RCC_AHBENR_FSMCEN));
  Blank();
  ack_delay = 20;
  assert(!At24c02_WriteVerified(0, &byte, 1) && (RCC->AHBENR & RCC_AHBENR_FSMCEN));
  Blank();
  corrupt_read = 1;
  assert(!At24c02_WriteVerified(0, &byte, 1) && (RCC->AHBENR & RCC_AHBENR_FSMCEN));
  /* Either STOP or BUSY independently keeps FSMC gated; all waits are bounded. */
  for (unsigned model = 0; model < 3U; ++model)
  {
    Blank();
    stop_model = model;
    stop_stuck = true;
    if (model == 2U)
      tick = UINT32_MAX - 3U;
    uint32_t started = tick;
    assert(!At24c02_Read(0, &byte, 1));
    assert((uint32_t)(tick - started) == 10U && (RCC->AHBENR & RCC_AHBENR_FSMCEN));
    At24c02Diagnostic failure = At24c02_GetDiagnostic();
    assert(failure.hal_status == HAL_TIMEOUT &&
           !strcmp(failure.operation, "STOP/bus idle timeout"));
    assert(failure.bus_busy == (model != 1U));
  }
  Blank();
  puts("PASS: FSMC off for every EEPROM read/write/ACK, STOP/BUSY completion before LCD restore, "
       "original clock state and error cleanup, bounded waits with interrupts enabled and tick "
       "wrap.");
}
static UserProfile Make(const char *name, uint16_t id)
{
  UserProfile p;
  UserProfile_Default(&p, name, id);
  return p;
}
static void Match(uint16_t id)
{
  ProfileManager_OnFingerprintMatch(id);
  ProfileManager_Update();
}
static void Select(uint16_t id)
{
  UserProfile p;
  for (unsigned i = 0; ProfileStorage_Get(i, &p); ++i)
    if (p.fingerprint_id == id)
    {
      char number[8];
      snprintf(number, sizeof(number), "%u", i + 1);
      Input(number);
      assert(fp.job == FP_JOB_VERIFY);
      return;
    }
  assert(!"Missing profile");
}
static void MenuExisting(void)
{
  Blank();
  UserProfile a = Make("Alex", 0), b = Make("Bob", 1), loaded;
  strcpy(a.password, "Alpha123");
  strcpy(b.password, "Beta456");
  b.dpi = 3200;
  assert(ProfileStorage_Save(&a, RECORD_ACTIVE) == STORAGE_OK);
  assert(ProfileStorage_Save(&b, RECORD_ACTIVE) == STORAGE_OK);
  writes = 0;
  Match(0);
  assert(dpi == 1600 && credential_requests == 1);
  Match(0);
  assert(credential_requests == 1);
  Match(1);
  Match(0);
  assert(credential_requests == 3);
  Match(999);
  assert(credential_requests == 3 && dpi == 1600);
  Input("confix\bg");
  assert(ConfigMenu_Active());
  Expect("Alex");
  Expect("Bob");
  Select(0);
  Reply(FP_REPLY_MISMATCH, 1);
  Expect("Fingerprint does not match");
  Input("1");
  Reply(FP_REPLY_VERIFIED, 0);
  Expect("USER SETTINGS");
  Input("1");
  Input("1");
  Input("2550");
  Expect("Invalid DPI");
  assert(dpi == 1600);
  Input("99999999999999999999");
  Input("-100");
  Input("2400");
  Input("1");
  assert(dpi == 2400);
  Input("2");
  Input("2");
  Input("1");
  Input("3");
  assert(rgb.mode == WS2812B_BREATHING);
  Input("2");
  Input("8");
  Input("256,0,1");
  Expect("Invalid RGB");
  assert(rgb.blue == 255);
  Input("1,2,3,4");
  Input("255,0,128");
  assert(rgb.red == 255 && rgb.blue == 128);
  Input("3");
  Input("0");
  assert(rgb.brightness == 0);
  Input("3");
  Input("100");
  Input("4");
  Input("5");
  assert(rgb.period_ms == 800);
  Input("5");
  Input("3");
  Input("1");
  Input("alex-PC");
  Input("1");
  Input("2");
  Input("4");
  Input("1");
  ResetTranscript();
  Input("Alpha123");
  Input("Beta456");
  Expect("Passwords do not match");
  assert(!strstr(transcript, "Alpha123") && !strstr(transcript, "Beta456"));
  Input("1");
  Input("Beta456");
  Input("Beta456");
  Input("3");
  Input("5");
  Input("1");
  Input("3");
  Input("2");
  Input("4");
  assert(macros[0] == MACRO_COPY && macros[1] == MACRO_PASTE);
  Input("3");
  Input("6");
  Input("Alex2");
  Input("1");
  assert(writes == 0);
  Input("9");
  assert(dpi == 1600 && rgb.brightness == 50 && macros[0] == MACRO_BACK && writes == 0);
  assert(ProfileStorage_Find(0, &loaded) && UserProfile_Same(&loaded, &a));
  Select(0);
  Reply(FP_REPLY_VERIFIED, 0);
  Input("6");
  Input("Alex2");
  Input("1");
  Input("8");
  Expect("Changes saved and verified");
  assert(writes > 0 && ProfileStorage_Find(0, &loaded));
  assert(!strcmp(loaded.name, "Alex2") && loaded.fingerprint_id == 0 && loaded.dpi == 1600);
  Select(0);
  Reply(FP_REPLY_VERIFIED, 0);
  Input("1");
  Input("1");
  Input("4800");
  Input("1");
  Input("2");
  Input("2");
  Input("1");
  Input("4");
  Input("2");
  Input("1");
  Input("3");
  Input("75");
  Input("4");
  Input("4");
  Input("5");
  Input("3");
  Input("1");
  Input("alex-PC");
  Input("1");
  Input("2");
  Input("4");
  Input("1");
  Input("Beta456");
  Input("Beta456");
  Input("3");
  Input("5");
  Input("1");
  Input("3");
  Input("2");
  Input("4");
  Input("3");
  Input("8");
  assert(ProfileStorage_Find(0, &loaded) && loaded.dpi == 4800 && loaded.rgb_mode == 3 &&
         loaded.red == 255 && loaded.blue == 0);
  assert(loaded.brightness == 75 && loaded.effect_speed == 4 &&
         !strcmp(loaded.password, "Beta456"));
  assert(!strcmp(loaded.computer_username, "alex-PC") && loaded.side1_macro == MACRO_COPY &&
         loaded.side2_macro == MACRO_PASTE);
  assert(dpi == 4800); /* Saved active profile is applied, without another credential event. */
  /* Reset/remount reloads actual bytes; RAM preview is not the persistence test. */
  ProfileManager_Init(&bus);
  assert(ProfileStorage_Count() == 2 && ProfileStorage_Find(0, &loaded) &&
         !strcmp(loaded.name, "Alex2"));
  Match(0);
  assert(credential_requests == 4);
  Match(0);
  assert(credential_requests == 4);
  puts("PASS: verification, every settings submenu, validation, hidden matching passwords, "
       "zero-write preview/cancel, saved rename, remount, A/A/B/A credentials.");
}
static void Wizard(const char *name, uint16_t id, bool legacy)
{
  unsigned count = ProfileStorage_Count();
  char number[8];
  snprintf(number, sizeof(number), "%u", count + 1);
  Input(number);
  Input("1");
  Input(name);
  assert(fp.job == FP_JOB_ENROLL);
  Reply(legacy ? FP_REPLY_DUPLICATE : FP_REPLY_PREPARED, id);
  if (legacy)
  {
    Expect("Link this verified existing fingerprint");
    Input("1");
  }
  Input("1600");
  Input("2");
  Input("3");
  Input("50");
  Input("3");
  Input("test-PC");
  Input("1");
  Input("Gamma789");
  Input("Gamma789");
  Input("3");
  Input("4");
  Expect("NEW USER SUMMARY");
}
static void RegistrationDeletion(void)
{
  Blank();
  Input("config");
  Wizard("Charlie", 2, false);
  unsigned before = delete_requests;
  Input("2");
  assert(writes == 0 && delete_requests == before &&
         ProfileStorage_Count() == 0); /* Never stored the prepared template. */
  Wizard("Charlie", 2, false);
  Input("1");
  assert(fp.job == FP_JOB_STORE && ProfileStorage_Count() == 0);
  Reply(FP_REPLY_ENROLLED, 2);
  Poll();
  assert(ProfileStorage_Count() == 1);
  Expect("New user saved and verified");
  UserProfile saved;
  assert(ProfileStorage_Find(2, &saved) && !strcmp(saved.password, "Gamma789") &&
         saved.side1_macro == MACRO_COPY);
  /* Saved duplicate is rejected instead of overwriting its profile. */
  Input("2");
  Input("1");
  Input("Duplicate");
  Reply(FP_REPLY_DUPLICATE, 2);
  Expect("Enrollment failed");
  Input("2");
  Wizard("Existing", 0, true);
  before = delete_requests;
  Input("2");
  assert(delete_requests == before && ProfileStorage_Count() == 1);
  Wizard("Existing", 0, true);
  before = store_requests;
  Input("1");
  assert(store_requests == before && ProfileStorage_Count() == 2);
  Select(2);
  Reply(FP_REPLY_VERIFIED, 2);
  Input("7");
  Input("1");
  assert(fp.job == FP_JOB_DELETE && deleted_id == 2 && !ProfileStorage_Find(2, NULL));
  Reply(FP_REPLY_ERROR, 2);
  Expect("Cleanup incomplete");
  assert(!ProfileStorage_Find(2, NULL));
  assert(ProfileStorage_Reload() == STORAGE_OK && !ProfileStorage_Find(2, NULL));
  unsigned count = ProfileStorage_Count();
  char number[8];
  snprintf(number, sizeof(number), "%u", count + 4);
  Input(number);
  assert(fp.job == FP_JOB_DELETE && deleted_id == 2);
  Reply(FP_REPLY_DELETED, 2);
  Expect("cleanup complete");
  UserProfile pending;
  ProfileRecordState kind;
  assert(!ProfileStorage_Pending(&pending, &kind) && ProfileStorage_Count() == 1);
  /* A power loss between template store and profile activation rolls back only its journaled ID. */
  UserProfile p = Make("Pending", 5);
  assert(ProfileStorage_Save(&p, RECORD_CREATE_PENDING) == STORAGE_OK);
  ProfileManager_Init(&bus);
  tick += 2000;
  ProfileManager_Update();
  assert(fp.job == FP_JOB_DELETE && deleted_id == 5);
  fp.reply = FP_REPLY_DELETED;
  ProfileManager_Update();
  assert(!ProfileStorage_Pending(&pending, &kind));
  puts("PASS: deferred enrollment Save/Cancel, legacy ID adoption, duplicate rejection, targeted "
       "delete failure/recovery, interrupted registration recovery.");
}
static void InputFaults(void)
{
  Blank();
  Input("config");
  UserProfile p = Make("Alex", 0);
  assert(ProfileStorage_Save(&p, RECORD_ACTIVE) == STORAGE_OK);
  /* Refresh root; configuration is still active from entry. */
  Input("4");
  Select(0);
  Reply(FP_REPLY_VERIFIED, 0);
  Input("6");
  char large[400];
  memset(large, 'X', sizeof(large) - 1);
  large[sizeof(large) - 1] = 0;
  Input(large);
  Expect("discarded");
  assert(writes > 0);
  int baseline = writes;
  /* 64-byte line overflow, without overflowing the UART ring. */
  for (unsigned i = 0; i < 80; ++i)
  {
    Rx('Y');
    Poll();
  }
  Rx('\r');
  Poll();
  Poll();
  Expect("discarded");
  Input("NameTooLong123456789");
  Expect("1..15");
  Input("Okay");
  Input("1");
  Input("9");
  assert(writes == baseline);
  Input("3");
  assert(!ConfigMenu_Active());
  puts("PASS: UART ring overflow, overlong line/name, backspace, numbered cancel/exit, no "
       "accidental writes.");
}
static void FailureWorkflows(void)
{
  Blank();
  UserProfile a = Make("Alex", 0);
  assert(ProfileStorage_Save(&a, RECORD_ACTIVE) == STORAGE_OK);
  Input("config");
  Select(0);
  Reply(FP_REPLY_VERIFIED, 0);
  Input("4");
  Input("1");
  ResetTranscript();
  for (unsigned i = 0; i < 40; ++i)
  {
    Rx('S');
    Poll();
  }
  Rx('\r');
  Poll();
  Poll();
  assert(!strstr(transcript, "SSSS"));
  Expect("discarded");
  Input("invalid-pass");
  Expect("Invalid password");
  Input("1");
  Input("Alpha123");
  Input("Alpha123");
  Input("2");
  Input("3");
  int before = writes;
  Input("1");
  Input("1");
  Input("3200");
  Input("1");
  Input("2");
  io_error = 1;
  Input("8");
  Expect("Save failed");
  assert(writes == before);
  Input("9");
  assert(dpi == 800);
  io_error = 0;
  Input("3"); /* retry while the failed mount reports zero users */
  Input("3");
  assert(!ConfigMenu_Active());
  Input("config");
  Input("4");
  assert(ProfileStorage_Status() == STORAGE_OK && ProfileStorage_Count() == 1);
  Wizard("New", 4, false);
  Input("1");
  assert(fp.job == FP_JOB_STORE);
  Reply(FP_REPLY_ERROR, 4);
  assert(fp.job == FP_JOB_DELETE && deleted_id == 4);
  Reply(FP_REPLY_DELETED, 4);
  Poll();
  Poll();
  assert(!ProfileStorage_Find(4, NULL) && ProfileStorage_Count() == 1);
  Expect("Registration save failed");
  /* A full store refuses registration before any sensor command. */
  UserProfile b = Make("Bob", 1), c = Make("Charlie", 2);
  assert(ProfileStorage_Save(&b, RECORD_ACTIVE) == STORAGE_OK &&
         ProfileStorage_Save(&c, RECORD_ACTIVE) == STORAGE_FULL);
  FpJob previous = fp.job;
  Input("3");
  Input("1");
  Expect("No capacity");
  assert(fp.job == previous);
  puts("PASS: overlong/invalid password rejected without echo, clear-password RAM path, failed "
       "save/cancel/retry, sensor-store rollback and full-capacity registration refusal.");
}
static void ConsoleTransport(void)
{
  Drain();
  assert(Console_Init(&uart));
  ResetTranscript();
  char payload[2048];
  memset(payload, 'T', sizeof(payload));
  tx_busy = true;
  assert(Console_Write(payload, 2040));
  assert(!Console_Write(payload, 8));
  assert(tx_length == 0);
  tx_busy = false;
  Console_Service();
  assert(tx_length == 2040);
  assert(!Console_Write(payload, 8));
  Drain();
  assert(transcript_size == 2040);
  assert(Console_Write(payload, 100));
  Drain();
  assert(transcript_size == 2140); /* ring wrap */
  for (unsigned i = 0; i < 300; ++i)
    Rx('P');
  Rx('\r');
  uint8_t byte = 0;
  assert(Console_ReadByte(&byte) == -1);
  assert(Console_ReadByte(&byte) == 1 && byte == '\n');
  assert(Console_ReadByte(&byte) == 0);
  UART_HandleTypeDef other = {0};
  Console_OnRxComplete(&other);
  Console_OnTxComplete(&other);
  Console_OnError(&other);
  Console_OnError(&uart);
  assert(Console_ReadByte(&byte) == -1);
  Rx('x');
  Rx('\r');
  assert(Console_ReadByte(&byte) == 1 && byte == '\n');
  puts("PASS: UART TX all-or-nothing capacity, persistent queued TX, busy retry/wrap, overflow "
       "resynchronization and callback UART isolation.");
}
static void StorageFaults(void)
{
  Blank();
  UserProfile old = Make("Alex", 0), next = old, out;
  next.dpi = 2400;
  assert(ProfileStorage_Save(&old, RECORD_ACTIVE) == STORAGE_OK);
  uint8_t image[256];
  memcpy(image, memory, 256);
  /* Cut each programmed byte of a COW replacement, then reboot/remount. */
  for (int cut = 0; cut <= (int)PROFILE_RECORD_BYTES + 2; ++cut)
  {
    memcpy(memory, image, 256);
    write_budget = -1;
    assert(ProfileStorage_Init(&bus) == STORAGE_OK);
    write_budget = cut;
    StorageResult result = ProfileStorage_Save(&next, RECORD_ACTIVE);
    write_budget = -1;
    StorageResult mount = ProfileStorage_Init(&bus);
    assert(mount == STORAGE_OK || mount == STORAGE_PROTECTED);
    assert(ProfileStorage_Find(0, &out));
    assert(out.dpi == 1600 || out.dpi == 2400);
    if (result == STORAGE_OK)
      assert(out.dpi == 2400);
    assert(!memcmp(memory + 224, image + 224, 32));
  }
  memcpy(memory, image, 256);
  assert(ProfileStorage_Init(&bus) == STORAGE_OK);
  UserProfile b = Make("Bob", 1), c = Make("Charlie", 2), d = Make("No room", 3);
  assert(ProfileStorage_Save(&b, RECORD_ACTIVE) == STORAGE_OK &&
         ProfileStorage_Save(&c, RECORD_ACTIVE) == STORAGE_FULL);
  assert(ProfileStorage_Count() == PROFILE_MAX_USERS &&
         ProfileStorage_Save(&d, RECORD_ACTIVE) == STORAGE_FULL);
  assert(ProfileStorage_Save(&next, RECORD_ACTIVE) == STORAGE_OK && ProfileStorage_Count() == 2);
  assert(ProfileStorage_Save(&next, RECORD_DELETE_PENDING) == STORAGE_OK);
  memcpy(image, memory, 256);
  for (int cut = 0; cut <= 4; ++cut)
  {
    memcpy(memory, image, 256);
    write_budget = -1;
    assert(ProfileStorage_Init(&bus) == STORAGE_OK);
    write_budget = cut;
    (void)ProfileStorage_ClearPending(0);
    write_budget = -1;
    assert(ProfileStorage_Init(&bus) == STORAGE_OK);
    assert(!ProfileStorage_Find(0, NULL));
    assert(ProfileStorage_Count() == 1);
  }
  Blank();
  memory[0] = 0x12;
  assert(ProfileStorage_Reload() == STORAGE_PROTECTED);
  int before = writes;
  assert(ProfileStorage_Save(&old, RECORD_ACTIVE) == STORAGE_PROTECTED && writes == before);
  Blank();
  io_error = 1;
  assert(ProfileStorage_Reload() == STORAGE_IO_ERROR && ProfileStorage_Count() == 0);
  io_error = 0;
  assert(ProfileStorage_Reload() == STORAGE_OK);
  corrupt_read = 1;
  assert(ProfileStorage_Save(&old, RECORD_ACTIVE) == STORAGE_IO_ERROR);
  assert(!strcmp(At24c02_GetDiagnostic().operation, "readback mismatch"));
  corrupt_read = 0;
  Blank();
  ack_delay = 20;
  assert(ProfileStorage_Save(&old, RECORD_ACTIVE) == STORAGE_IO_ERROR);
  assert(!strcmp(At24c02_GetDiagnostic().operation, "ACK poll"));
  Blank();
  assert(!At24c02_WriteVerified(255, (const uint8_t *)"xx", 2));
  assert(!At24c02_Read(256, memory, 1));
  assert(ProfileStorage_Save(&old, RECORD_ACTIVE) == STORAGE_OK);
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS * PROFILE_RECORD_BYTES; i += PROFILE_RECORD_BYTES)
    if (memory[i] == 'E' && memory[i + 3] == RECORD_ACTIVE)
      memory[i + 10] ^= 1;
  assert(ProfileStorage_Reload() == STORAGE_OK && ProfileStorage_Count() == 0);
  puts("PASS: EEPROM page/bounds/ACK/readback, CRC, two-user limit and full-capacity update, "
       "foreign data protection, every-byte interrupted save and deletion, reserved 32 bytes "
       "preserved.");
}
static uint16_t LegacyCrc(const uint8_t *p, unsigned n)
{
  uint16_t crc = 0xFFFF;
  while (n--)
  {
    crc ^= (uint16_t)*p++ << 8;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (uint16_t)((uint32_t)crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U);
  }
  return crc;
}
static void LegacyRecord(unsigned slot, const UserProfile *p, uint32_t seq)
{
  uint8_t *raw = memory + slot * PROFILE_LEGACY_RECORD_BYTES;
  memset(raw, 0, PROFILE_LEGACY_RECORD_BYTES);
  raw[0] = 'E';
  raw[1] = 'P';
  raw[2] = 1;
  raw[3] = RECORD_ACTIVE;
  for (unsigned b = 0; b < 4; ++b)
    raw[4 + b] = seq >> (b * 8);
  raw[8] = p->fingerprint_id;
  raw[9] = p->fingerprint_id >> 8;
  raw[10] = p->dpi;
  raw[11] = p->dpi >> 8;
  memcpy(raw + 12, p->name, 16);
  memcpy(raw + 28, p->computer_username, 16);
  raw[44] = p->rgb_mode;
  raw[45] = p->red;
  raw[46] = p->green;
  raw[47] = p->blue;
  raw[48] = p->brightness;
  raw[49] = p->effect_speed;
  raw[50] = p->side1_macro;
  raw[51] = p->side2_macro;
  raw[52] = 2; /* Retired demo token must not turn into literal password bytes. */
  uint16_t crc = LegacyCrc(raw, 54);
  raw[54] = crc;
  raw[55] = crc >> 8;
}
static void LegacyMigration(void)
{
  UserProfile users[4], found;
  for (unsigned i = 0; i < 4; ++i)
  {
    users[i] = Make("Marcus123456789", 10 + i);
    strcpy(users[i].computer_username, "Workstation1234");
    users[i].dpi = 1600 + i * 100;
  }
  /* Every possible placement of one or two old-format live profiles. */
  for (unsigned mask = 1; mask < 16; ++mask)
  {
    unsigned count = 0;
    for (unsigned bit = 0; bit < 4; ++bit)
      count += (mask >> bit) & 1U;
    if (count > 2)
      continue;
    Blank();
    for (unsigned i = 0; i < 4; ++i)
      if (mask & (1U << i))
        LegacyRecord(i, &users[i], i + 1);
    int before = writes;
    assert(ProfileStorage_Init(&bus) == STORAGE_OK && ProfileStorage_Count() == count &&
           writes == before);
    for (unsigned i = 0; i < 4; ++i)
      if (mask & (1U << i))
      {
        assert(ProfileStorage_Find(users[i].fingerprint_id, &found) &&
               UserProfile_Same(&found, &users[i]));
        strcpy(found.password, "Abcdefgh01234567XYZ");
        assert(ProfileStorage_Save(&found, RECORD_ACTIVE) == STORAGE_OK);
        assert(ProfileStorage_Count() == count);
        assert(ProfileStorage_Find(users[i].fingerprint_id, &found) &&
               !strcmp(found.password, "Abcdefgh01234567XYZ"));
      }
    for (unsigned round = 0; round < 3; ++round)
      for (unsigned i = 0; i < 4; ++i)
        if (mask & (1U << i))
        {
          assert(ProfileStorage_Find(users[i].fingerprint_id, &found));
          ++found.effect_speed;
          if (found.effect_speed > 5)
            found.effect_speed = 1;
          assert(ProfileStorage_Save(&found, RECORD_ACTIVE) == STORAGE_OK);
        }
    for (unsigned b = 224; b < 256; ++b)
      assert(memory[b] == (uint8_t)b);
  }
  /* A full three-user legacy allocation is readable and supports targeted
   * deletion before the user starts saving into the two-user format. */
  Blank();
  for (unsigned i = 0; i < 3; ++i)
    LegacyRecord(i, &users[i], i + 1);
  assert(ProfileStorage_Init(&bus) == STORAGE_OK && ProfileStorage_Count() == 3);
  assert(ProfileStorage_Save(&users[0], RECORD_ACTIVE) == STORAGE_FULL);
  assert(ProfileStorage_Save(&users[0], RECORD_DELETE_PENDING) == STORAGE_OK &&
         !ProfileStorage_Find(10, NULL));
  assert(ProfileStorage_ClearPending(10) == STORAGE_OK && ProfileStorage_Count() == 2);
  strcpy(users[1].password, "Saved123");
  assert(ProfileStorage_Save(&users[1], RECORD_ACTIVE) == STORAGE_OK);
  /* Reset after each programmed byte, including legacy relocation and trimming. */
  Blank();
  LegacyRecord(0, &users[0], 1);
  LegacyRecord(1, &users[1], 2);
  uint8_t image[256];
  memcpy(image, memory, sizeof(image));
  UserProfile update = users[0];
  strcpy(update.password, "Abcdefgh01234567XYZ");
  assert(strlen(update.password) == 19);
  assert(ProfileStorage_Init(&bus) == STORAGE_OK);
  write_budget = 10000;
  assert(ProfileStorage_Save(&update, RECORD_ACTIVE) == STORAGE_OK);
  int programmed = 10000 - write_budget;
  for (int cut = 0; cut <= programmed; ++cut)
  {
    memcpy(memory, image, sizeof(image));
    write_budget = -1;
    assert(ProfileStorage_Init(&bus) == STORAGE_OK);
    write_budget = cut;
    StorageResult saved = ProfileStorage_Save(&update, RECORD_ACTIVE);
    write_budget = -1;
    StorageResult mounted = ProfileStorage_Init(&bus);
    assert(mounted == STORAGE_OK || mounted == STORAGE_PROTECTED);
    assert(ProfileStorage_Count() == 2);
    assert(ProfileStorage_Find(10, &found));
    assert(!strcmp(found.name, users[0].name) && found.dpi == users[0].dpi);
    assert(!found.password[0] || !strcmp(found.password, update.password));
    if (saved == STORAGE_OK)
      assert(!strcmp(found.password, update.password));
    assert(ProfileStorage_Find(11, &found) && found.password[0] == 0);
    assert(!memcmp(memory + 224, image + 224, 32));
  }
  puts("PASS: all legacy record placements migrate without losing settings, 15-character names or "
       "users; three-user legacy deletion; every-byte interrupted migration/relocation; old demo "
       "tokens cleared.");
}
static void CustomPasswordMenus(void)
{
  Blank();
  UserProfile p = Make("Marcus", 0), readback;
  assert(ProfileStorage_Save(&p, RECORD_ACTIVE) == STORAGE_OK);
  Input("config");
  const char *const values[] = {"0", "1", "Z", "a", "9", "Abc9Z", "Abcdefgh01234567XYZ"};
  for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
  {
    Select(0);
    Reply(FP_REPLY_VERIFIED, 0);
    Input("4");
    Input("1");
    ResetTranscript();
    int before = writes;
    Input(values[i]);
    Expect("Confirm new password");
    char echoed[32];
    snprintf(echoed, sizeof(echoed), "%s\r\n", values[i]);
    assert(!strstr(transcript, echoed));
    Input(values[i]);
    assert(writes == before);
    Input("3");
    Input("8");
    assert(ProfileStorage_Find(0, &readback) && !strcmp(readback.password, values[i]));
    assert(ProfileStorage_Reload() == STORAGE_OK && ProfileStorage_Find(0, &readback) &&
           !strcmp(readback.password, values[i]));
  }
  const char *const invalid[] = {"bad!",    " abc",   "abc ",
                                 "abc-def", "foo_99", "AAAAAAAAAAAAAAAAAAAA"};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
  {
    Select(0);
    Reply(FP_REPLY_VERIFIED, 0);
    Input("4");
    Input("1");
    ResetTranscript();
    int before = writes;
    Input(invalid[i]);
    if (strlen(invalid[i]) <= 19)
    {
      Expect("Invalid password");
      Input("2");
    }
    else
    {
      Expect("discarded");
      Input("-1");
    }
    Input("3");
    Input("9");
    assert(writes == before);
    assert(ProfileStorage_Find(0, &readback) && !strcmp(readback.password, values[6]));
  }
  Select(0);
  Reply(FP_REPLY_VERIFIED, 0);
  Input("4");
  Input("1");
  Input("Cancel123");
  Input("-1");
  Input("3");
  Input("9");
  assert(ProfileStorage_Find(0, &readback) && !strcmp(readback.password, values[6]));
  assert(ProfileStorage_Count() == 1 && !strstr(transcript, "documented demo fixtures"));
  Select(0);
  Reply(FP_REPLY_VERIFIED, 0);
  Input("4");
  Input("2");
  Input("3");
  Input("9");
  assert(ProfileStorage_Find(0, &readback) && !strcmp(readback.password, values[6]));
  Select(0);
  Reply(FP_REPLY_VERIFIED, 0);
  Input("4");
  Input("2");
  Input("3");
  Input("8");
  assert(ProfileStorage_Reload() == STORAGE_OK && ProfileStorage_Find(0, &readback) &&
         !readback.password[0]);
  puts("PASS: hidden custom passwords 1..19, literal 0/1, save/reload, invalid chars/20-byte "
       "rejection, untrimmed whitespace rejection and numbered cancellation preserve existing "
       "settings.");
}
static void PasswordValidation(void)
{
  assert(!UserProfile_ValidPassword(NULL));
  assert(!UserProfile_ValidPassword(""));
  assert(UserProfile_ValidPassword("A"));
  assert(UserProfile_ValidPassword("a"));
  assert(UserProfile_ValidPassword("0"));
  assert(UserProfile_ValidPassword("1"));
  assert(UserProfile_ValidPassword("Abc012XYZ"));
  assert(UserProfile_ValidPassword("Abcdefgh01234567XYZ"));   /* 19 */
  assert(!UserProfile_ValidPassword("Abcdefgh012345678XYZ")); /* 20 */
  assert(!UserProfile_ValidPassword("abc def"));
  assert(!UserProfile_ValidPassword(" abc"));
  assert(!UserProfile_ValidPassword("abc "));
  assert(!UserProfile_ValidPassword("abc-def"));
  assert(!UserProfile_ValidPassword("abc_def"));
  assert(!UserProfile_ValidPassword("abc!"));
  assert(!UserProfile_ValidPassword("abc\n"));
  assert(!UserProfile_ValidPassword("abc\xC3\xA9"));
  puts("PASS: password validator accepts exactly 1..19 ASCII letters/digits; empty, 20+, "
       "whitespace, punctuation and non-ASCII are rejected.");
}
int main(void)
{
  assert(Console_Init(&uart));
  PasswordValidation();
  ConsoleTransport();
  FsmcWorkaround();
  NewlineHandling();
  RegistrationInputBoundaries();
  EepromDiagnostics();
  MenuExisting();
  RegistrationDeletion();
  InputFaults();
  FailureWorkflows();
  StorageFaults();
  LegacyMigration();
  CustomPasswordMenus();
  assert(!strstr(transcript, "Alpha123") && !strstr(transcript, "Beta456") &&
         !strstr(transcript, "Gamma789"));
  return 0;
}
