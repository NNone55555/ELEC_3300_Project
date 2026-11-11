#include "console_uart.h"
#include <string.h>

#define RX_BYTES 256U
#define TX_BYTES 2048U
static UART_HandleTypeDef *console;
static uint8_t received_byte, rx[RX_BYTES], tx[TX_BYTES];
static volatile uint16_t rx_head, rx_tail, tx_head, tx_tail;
static volatile uint16_t active_tx;
static volatile bool rx_fault, rx_dropping;

static void StartTx(void)
{
  if (active_tx != 0U || tx_head == tx_tail)
    return;
  uint16_t length = tx_head > tx_tail ? (unsigned)tx_head - tx_tail : TX_BYTES - tx_tail;
  active_tx = length;
  if (HAL_UART_Transmit_IT(console, &tx[tx_tail], length) != HAL_OK)
    active_tx = 0U;
}
bool Console_Init(UART_HandleTypeDef *uart)
{
  console = uart;
  rx_head = rx_tail = tx_head = tx_tail = active_tx = 0;
  rx_fault = rx_dropping = false;
  return uart != NULL && HAL_UART_Receive_IT(console, &received_byte, 1) == HAL_OK;
}
bool Console_Write(const char *text, size_t length)
{
  if (console == NULL || text == NULL || length >= TX_BYTES)
    return false;
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  uint16_t used = (tx_head + TX_BYTES - tx_tail) % TX_BYTES;
  if (length > TX_BYTES - 1U - used)
  {
    __set_PRIMASK(irq);
    return false;
  }
  uint16_t first = TX_BYTES - tx_head;
  if (first > length)
    first = length;
  memcpy(&tx[tx_head], text, first);
  memcpy(tx, text + first, length - first);
  tx_head = (tx_head + length) % TX_BYTES;
  StartTx();
  __set_PRIMASK(irq);
  return true;
}
void Console_Service(void)
{
  if (console == NULL)
    return;
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  StartTx();
  if (console->RxState == HAL_UART_STATE_READY)
    (void)HAL_UART_Receive_IT(console, &received_byte, 1);
  __set_PRIMASK(irq);
}
int Console_ReadByte(uint8_t *byte)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  if (rx_fault)
  {
    rx_fault = false;
    __set_PRIMASK(irq);
    return -1;
  }
  if (rx_head == rx_tail)
  {
    __set_PRIMASK(irq);
    return 0;
  }
  *byte = rx[rx_tail];
  rx[rx_tail] = 0;
  rx_tail = (rx_tail + 1U) % RX_BYTES;
  __set_PRIMASK(irq);
  return 1;
}
void Console_OnRxComplete(UART_HandleTypeDef *uart)
{
  if (uart != console)
    return;
  uint8_t byte = received_byte;
  bool newline = byte == '\r' || byte == '\n';
  if (!rx_dropping && (rx_head + 1U) % RX_BYTES == rx_tail)
  {
    rx_fault = rx_dropping = true;
    /* No task can consume concurrently with this ISR's short reset. */
    memset(rx, 0, sizeof(rx));
    rx_tail = rx_head;
  }
  if (rx_dropping)
  {
    if (newline)
    {
      rx_dropping = false;
      rx[rx_head] = '\n';
      rx_head = (rx_head + 1U) % RX_BYTES;
    }
  }
  else
  {
    rx[rx_head] = byte;
    rx_head = (rx_head + 1U) % RX_BYTES;
  }
  if (HAL_UART_Receive_IT(uart, &received_byte, 1) != HAL_OK)
    rx_fault = true;
}
void Console_OnTxComplete(UART_HandleTypeDef *uart)
{
  if (uart != console)
    return;
  tx_tail = (tx_tail + active_tx) % TX_BYTES;
  active_tx = 0;
  StartTx();
}
void Console_OnError(UART_HandleTypeDef *uart)
{
  if (uart != console)
    return;
  rx_fault = rx_dropping = true;
  if (uart->RxState == HAL_UART_STATE_READY)
    (void)HAL_UART_Receive_IT(uart, &received_byte, 1);
}
