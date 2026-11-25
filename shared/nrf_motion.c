#include "nrf_motion.h"
#include "main.h"
#include <string.h>

#define NRF_R_REGISTER 0x00U
#define NRF_W_REGISTER 0x20U
#define NRF_R_RX_PAYLOAD 0x61U
#define NRF_R_RX_PL_WID 0x60U
#define NRF_W_TX_PAYLOAD 0xA0U
#define NRF_W_ACK_PAYLOAD_P0 0xA8U
#define NRF_ACTIVATE 0x50U
#define NRF_FLUSH_TX 0xE1U
#define NRF_FLUSH_RX 0xE2U

#define NRF_CONFIG 0x00U
#define NRF_EN_AA 0x01U
#define NRF_EN_RXADDR 0x02U
#define NRF_SETUP_AW 0x03U
#define NRF_SETUP_RETR 0x04U
#define NRF_RF_CH 0x05U
#define NRF_RF_SETUP 0x06U
#define NRF_STATUS 0x07U
#define NRF_RX_ADDR_P0 0x0AU
#define NRF_TX_ADDR 0x10U
#define NRF_RX_PW_P0 0x11U
#define NRF_FIFO_STATUS 0x17U
#define NRF_DYNPD 0x1CU
#define NRF_FEATURE 0x1DU

#define CONFIG_POLLING_CRC16 0x7CU /* Mask IRQ outputs, enable two-byte CRC. */
#define CONFIG_PWR_UP 0x02U
#define CONFIG_PRIM_RX 0x01U
#define STATUS_RX_DR 0x40U
#define STATUS_TX_DS 0x20U
#define STATUS_MAX_RT 0x10U
#define STATUS_TX_FLAGS (STATUS_TX_DS | STATUS_MAX_RT)
#define FIFO_RX_EMPTY 0x01U
#define FIFO_TX_FULL 0x20U
#define FEATURE_ACK_PAYLOADS 0x06U /* EN_DPL | EN_ACK_PAY. */
#define SPI_TIMEOUT_MS 2U
#define TX_TIMEOUT_MS 15U

static const uint8_t address[] = {0xC3U, 0x57U, 0x33U, 0x90U, 0xA6U};
static SPI_HandleTypeDef *radio_spi;
static volatile NrfMotionTxState tx_state;
static uint32_t tx_started;
static bool rx_ready;

static void SetCe(GPIO_PinState level)
{
  HAL_GPIO_WritePin(NRF_CE_GPIO_Port, NRF_CE_Pin, level);
}

static bool Transfer(uint8_t command, const uint8_t *out, uint8_t *in, uint16_t size)
{
  uint8_t tx[1U + WIRELESS_PACKET_SIZE] = {command};
  uint8_t rx[sizeof(tx)];
  if (out != NULL)
    memcpy(&tx[1], out, size);

  HAL_GPIO_WritePin(NRF_CSN_GPIO_Port, NRF_CSN_Pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef result = HAL_SPI_TransmitReceive(radio_spi, tx, rx, size + 1U, SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(NRF_CSN_GPIO_Port, NRF_CSN_Pin, GPIO_PIN_SET);

  if (result != HAL_OK)
    return false;
  if (in != NULL)
    memcpy(in, &rx[1], size);
  return true;
}

static bool WriteRegister(uint8_t reg, uint8_t value)
{
  return Transfer(NRF_W_REGISTER | reg, &value, NULL, 1U);
}

static bool CheckRegister(uint8_t reg, const uint8_t *expected, uint16_t size)
{
  uint8_t readback[sizeof(address)];
  return Transfer(NRF_R_REGISTER | reg, NULL, readback, size)
         && memcmp(readback, expected, size) == 0;
}

static bool EnableAckPayloads(void)
{
  const uint8_t features = FEATURE_ACK_PAYLOADS;
  if (!WriteRegister(NRF_FEATURE, features))
    return false;
  if (!CheckRegister(NRF_FEATURE, &features, 1U))
  {
    const uint8_t activate = 0x73U;
    if (!Transfer(NRF_ACTIVATE, &activate, NULL, 1U)
        || !WriteRegister(NRF_FEATURE, features)
        || !CheckRegister(NRF_FEATURE, &features, 1U))
      return false;
  }
  const uint8_t pipe0 = 1U;
  return WriteRegister(NRF_DYNPD, pipe0) && CheckRegister(NRF_DYNPD, &pipe0, 1U);
}

bool NrfMotion_Init(SPI_HandleTypeDef *spi, NrfMotionRole role)
{
  const uint8_t config = CONFIG_POLLING_CRC16 | CONFIG_PWR_UP
                         | (role == NRF_MOTION_RX ? CONFIG_PRIM_RX : 0U);
  static const uint8_t settings[][2] = {
    {NRF_EN_AA, 0x01U},      /* Auto-ack only on pipe 0. */
    {NRF_EN_RXADDR, 0x01U},
    {NRF_SETUP_AW, 0x03U},   /* Five-byte address. */
    {NRF_SETUP_RETR, 0x23U}, /* 750 us retry delay, up to three retries. */
    {NRF_RF_CH, 76U},
    {NRF_RF_SETUP, 0x00U},   /* 1 Mbit/s, lowest radio drive for bench use. */
    {NRF_RX_PW_P0, WIRELESS_PACKET_SIZE}
  };

  radio_spi = spi;
  tx_state = NRF_TX_UNAVAILABLE;
  rx_ready = false;
  SetCe(GPIO_PIN_RESET);
  HAL_GPIO_WritePin(NRF_CSN_GPIO_Port, NRF_CSN_Pin, GPIO_PIN_SET);
  HAL_Delay(100U); /* Allow radio power-on reset to complete. */

  if (!WriteRegister(NRF_CONFIG, CONFIG_POLLING_CRC16))
    return false;
  for (unsigned i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
  {
    if (!WriteRegister(settings[i][0], settings[i][1])
        || !CheckRegister(settings[i][0], &settings[i][1], 1U))
      return false;
  }

  if (!EnableAckPayloads()
      || !Transfer(NRF_W_REGISTER | NRF_RX_ADDR_P0, address, NULL, sizeof(address))
      || !CheckRegister(NRF_RX_ADDR_P0, address, sizeof(address))
      || !Transfer(NRF_W_REGISTER | NRF_TX_ADDR, address, NULL, sizeof(address))
      || !CheckRegister(NRF_TX_ADDR, address, sizeof(address))
      || !Transfer(NRF_FLUSH_TX, NULL, NULL, 0U)
      || !Transfer(NRF_FLUSH_RX, NULL, NULL, 0U)
      || !WriteRegister(NRF_STATUS, STATUS_RX_DR | STATUS_TX_FLAGS)
      || !WriteRegister(NRF_CONFIG, config)
      || !CheckRegister(NRF_CONFIG, &config, 1U))
    return false;

  HAL_Delay(5U); /* PWR_UP to standby with the module's crystal. */
  if (role == NRF_MOTION_RX)
  {
    SetCe(GPIO_PIN_SET);
    rx_ready = true;
  }
  else
  {
    tx_state = NRF_TX_READY;
  }
  return true;
}

bool NrfMotion_StartTx(const uint8_t payload[WIRELESS_PACKET_SIZE])
{
  if (tx_state == NRF_TX_UNAVAILABLE || tx_state == NRF_TX_PENDING)
    return false;

  if (!Transfer(NRF_FLUSH_TX, NULL, NULL, 0U)
      || !Transfer(NRF_FLUSH_RX, NULL, NULL, 0U)
      || !WriteRegister(NRF_STATUS, STATUS_TX_FLAGS | STATUS_RX_DR)
      || !Transfer(NRF_W_TX_PAYLOAD, payload, NULL, WIRELESS_PACKET_SIZE))
  {
    tx_state = NRF_TX_ERROR;
    return false;
  }

  tx_started = HAL_GetTick();
  tx_state = NRF_TX_PENDING;
  /* CE stays high until polling finishes. With one queued payload, the
   * radio transmits it, waits for ACK and then stops with the FIFO empty.
   */
  SetCe(GPIO_PIN_SET);
  return true;
}

void NrfMotion_PollTx(void)
{
  if (tx_state != NRF_TX_PENDING)
    return;

  uint8_t status;
  NrfMotionTxState result = NRF_TX_ERROR;
  if (Transfer(NRF_R_REGISTER | NRF_STATUS, NULL, &status, 1U) && (status & 0x80U) == 0U)
  {
    if ((status & STATUS_MAX_RT) != 0U)
      result = NRF_TX_NO_ACK;
    else if ((status & STATUS_TX_DS) != 0U)
      result = NRF_TX_ACKED;
    else if ((uint32_t)(HAL_GetTick() - tx_started) < TX_TIMEOUT_MS)
      return;
  }

  SetCe(GPIO_PIN_RESET);
  if (!WriteRegister(NRF_STATUS, STATUS_TX_FLAGS))
    result = NRF_TX_ERROR;
  if (result != NRF_TX_ACKED && !Transfer(NRF_FLUSH_TX, NULL, NULL, 0U))
    result = NRF_TX_ERROR;
  tx_state = result;
}

NrfMotionTxState NrfMotion_GetTxState(void)
{
  return tx_state;
}

static NrfMotionRxResult ReadPayload(uint8_t payload[WIRELESS_PACKET_SIZE])
{
  uint8_t fifo, size;
  if (!Transfer(NRF_R_REGISTER | NRF_FIFO_STATUS, NULL, &fifo, 1U)
      || (fifo & 0x8CU) != 0U) /* Reserved bits are zero, even when RX is empty. */
    return NRF_RX_ERROR;
  if ((fifo & FIFO_RX_EMPTY) != 0U)
    return NRF_RX_EMPTY;

  if (!Transfer(NRF_R_RX_PL_WID, NULL, &size, 1U))
    return NRF_RX_ERROR;
  if (size != WIRELESS_PACKET_SIZE)
  {
    (void)Transfer(NRF_FLUSH_RX, NULL, NULL, 0U);
    (void)WriteRegister(NRF_STATUS, STATUS_RX_DR);
    return NRF_RX_ERROR;
  }
  if (!Transfer(NRF_R_RX_PAYLOAD, NULL, payload, WIRELESS_PACKET_SIZE)
      || !WriteRegister(NRF_STATUS, STATUS_RX_DR))
    return NRF_RX_ERROR;
  return NRF_RX_PACKET;
}

NrfMotionRxResult NrfMotion_Read(uint8_t payload[WIRELESS_PACKET_SIZE])
{
  return rx_ready ? ReadPayload(payload) : NRF_RX_ERROR;
}

NrfMotionRxResult NrfMotion_ReadAck(uint8_t payload[WIRELESS_PACKET_SIZE])
{
  return tx_state == NRF_TX_ACKED ? ReadPayload(payload) : NRF_RX_EMPTY;
}

bool NrfMotion_QueueAck(const uint8_t payload[WIRELESS_PACKET_SIZE])
{
  uint8_t fifo;
  return rx_ready && Transfer(NRF_R_REGISTER | NRF_FIFO_STATUS, NULL, &fifo, 1U)
         && (fifo & FIFO_TX_FULL) == 0U
         && Transfer(NRF_W_ACK_PAYLOAD_P0, payload, NULL, WIRELESS_PACKET_SIZE);
}
