#include "uart_test.h"
#include "lcd.h"

static UART_HandleTypeDef *test_uart;
static uint8_t rx_byte;
static volatile uint8_t last_digit;
static uint8_t displayed_digit;
static uint32_t last_transmit;

void UART_Test_Init(UART_HandleTypeDef *uart)
{
  test_uart = uart;
  last_digit = 0U;
  displayed_digit = 0U;
  LCD_DrawString(0, 20, "115200 baud / 8N1");
  LCD_DrawString(0, 60, "Last received: -");
  LCD_DrawString(0, 140, "Send a digit 0-9 from PC");

  /* Discard stale input from before this test started. */
  __HAL_UART_CLEAR_OREFLAG(test_uart);
  if (HAL_UART_Receive_IT(test_uart, &rx_byte, 1) != HAL_OK)
  {
    LCD_DrawString(0, 120, "RX start error");
  }
  /* Send immediately on the first poll, then once per second. */
  last_transmit = HAL_GetTick() - 1000U;
}

void UART_Test_Poll(void)
{
  /* A single-byte mailbox is enough: only the most recent digit is needed.
   * LCD access and blocking transmission run only in the default task.
   */
  uint8_t digit = last_digit;
  if (digit != 0U && digit != displayed_digit)
  {
    LCD_DrawChar(15U * WIDTH_EN_CHAR, 60, (char)digit);
    displayed_digit = digit;
  }

  /* HAL ends reception after overrun; re-arm it here if that happens. */
  if (HAL_UART_GetState(test_uart) == HAL_UART_STATE_READY)
  {
    (void)HAL_UART_Receive_IT(test_uart, &rx_byte, 1);
  }

  uint32_t now = HAL_GetTick();
  if ((uint32_t)(now - last_transmit) >= 1000U)
  {
    static const uint8_t message[] = "USART1 OK\r\n";
    last_transmit = now;
    HAL_StatusTypeDef result = HAL_UART_Transmit(test_uart, message,
                                                sizeof(message) - 1U, 100);
    LCD_DrawString(0, 100, result == HAL_OK ? "TX OK   " : "TX error");
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if (uart == test_uart)
  {
    if (rx_byte >= '0' && rx_byte <= '9')
    {
      last_digit = rx_byte;
    }
    /* Ignore CR/LF and other characters; receive the next byte immediately. */
    (void)HAL_UART_Receive_IT(uart, &rx_byte, 1);
  }
}
