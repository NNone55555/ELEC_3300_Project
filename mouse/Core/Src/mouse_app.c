#include "mouse_app.h"
#include "encoder.h"
#include "fingerprint.h"
#include "pmw3360.h"
#include "mouse_buttons.h"
#include "usb_hid_controller.h"
#include "login_typing.h"
#include "console_uart.h"
#include "config_menu.h"
#include "profile_manager.h"
#include "fingerprint_service.h"
#include "lcd.h"
#include "main.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_CPI 800U
#define MOTION_PERIOD_TICKS 5U
#define OPTICAL_RETRY_MS 2000U
#define FP_CONTROL_FLAG 1U
#define DISPLAY_ROWS 20U
#define DISPLAY_COLUMNS 30U

typedef struct {
  Pmw3360Status optical_status;
  Pmw3360Motion motion;
  int32_t x_total;
  int32_t y_total;
  EncoderSnapshot encoder;
  MouseButtonsSnapshot buttons;
  FingerprintController fingerprint;
} MouseStatus;

static MouseStatus shared_status;
static osMutexId_t status_mutex;
static osThreadId_t fingerprint_thread;
static osThreadId_t motion_thread;
static osThreadId_t usb_thread;
static SPI_HandleTypeDef *motion_spi;
static UART_HandleTypeDef *console;
static I2C_HandleTypeDef *profile_bus;
static volatile uint16_t requested_dpi = DEFAULT_CPI;
static volatile bool inputs_enabled;
static volatile uint32_t button_presses;
static uint8_t button_candidate;
static uint8_t button_stable;
static uint8_t button_samples;
static char displayed_lines[DISPLAY_ROWS][DISPLAY_COLUMNS + 1U];

bool MouseApp_SetDpi(uint16_t dpi)
{
  if (!UserProfile_ValidDpi(dpi)) return false;
  requested_dpi = dpi;
  return true;
}
uint16_t MouseApp_GetDpi(void) {return requested_dpi;}

static const osThreadAttr_t motion_attributes = {
  .name = "motion",
  .stack_size = 1536,
  .priority = osPriorityAboveNormal
};
static const osThreadAttr_t fingerprint_attributes = {
  .name = "fingerprint",
  .stack_size = 3072,
  .priority = osPriorityNormal
};
static const osThreadAttr_t usb_attributes = {
  .name = "wiredMouse",
  .stack_size = 1536,
  .priority = osPriorityHigh
};

static int32_t AddCounts(int32_t current, int16_t delta)
{
  int64_t sum = (int64_t)current + delta;
  if (sum > INT32_MAX)
  {
    return INT32_MAX;
  }
  if (sum < INT32_MIN)
  {
    return INT32_MIN;
  }
  return (int32_t)sum;
}

void MouseApp_SampleInputs1ms(void)
{
  if (!inputs_enabled)
  {
    return;
  }
  Encoder_Sample1ms();
  MouseButtons_Sample1ms();
  uint8_t pressed = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET;
  if (pressed != button_candidate)
  {
    button_candidate = pressed;
    button_samples = 1U;
  }
  else if (button_samples < 30U)
  {
    ++button_samples;
  }
  if (button_samples == 30U && button_stable != button_candidate)
  {
    button_stable = button_candidate;
    if (button_stable != 0U)
    {
      ++button_presses;
    }
  }
}

static void MotionTask(void *argument)
{
  (void)argument;
  Pmw3360Status optical_status = PMW3360_STARTING;
  uint32_t last_attempt = HAL_GetTick() - OPTICAL_RETRY_MS;
  uint32_t next_wake = osKernelGetTickCount();
  uint16_t applied_dpi = 0U;
  for (;;)
  {
    Pmw3360Motion motion = {0};
    if (optical_status == PMW3360_OK)
    {
      uint16_t desired_dpi = requested_dpi;
      if (desired_dpi != applied_dpi)
      {
        optical_status = Pmw3360_SetCpi(desired_dpi);
        if (optical_status == PMW3360_OK) applied_dpi = desired_dpi;
      }
      if (optical_status == PMW3360_OK) optical_status = Pmw3360_ReadMotion(&motion);
      if (optical_status != PMW3360_OK)
      {
        last_attempt = HAL_GetTick();
      }
    }
    else if ((uint32_t)(HAL_GetTick() - last_attempt) >= OPTICAL_RETRY_MS)
    {
      last_attempt = HAL_GetTick();
      applied_dpi = requested_dpi;
      optical_status = Pmw3360_Init(motion_spi, applied_dpi);
    }

    UsbHid_AddMotion(motion.dx, motion.dy, 0);
    osMutexAcquire(status_mutex, osWaitForever);
    shared_status.optical_status = optical_status;
    shared_status.motion = motion;
    if (optical_status == PMW3360_OK)
    {
      shared_status.x_total = AddCounts(shared_status.x_total, motion.dx);
      shared_status.y_total = AddCounts(shared_status.y_total, motion.dy);
    }
    osMutexRelease(status_mutex);

    next_wake += MOTION_PERIOD_TICKS;
    uint32_t now = osKernelGetTickCount();
    if ((int32_t)(next_wake - now) <= 0)
    {
      next_wake = now + MOTION_PERIOD_TICKS;
    }
    osDelayUntil(next_wake);
  }
}

static void WiredMouseTask(void *argument)
{
  (void)argument;
  uint32_t next_wake = osKernelGetTickCount();
  uint32_t handled_presses = 0U;
  int32_t previous_scroll = Encoder_GetSnapshot().steps;
  for (;;)
  {
    uint32_t presses = button_presses;
    if (presses != handled_presses)
    {
      handled_presses = presses;
      (void)osThreadFlagsSet(fingerprint_thread, FP_CONTROL_FLAG);
    }
    MouseButtonsSnapshot buttons = MouseButtons_GetSnapshot();
    EncoderSnapshot encoder = Encoder_GetSnapshot();
    int64_t scroll_delta = (int64_t)encoder.steps - previous_scroll;
    int32_t wheel = scroll_delta > INT32_MAX ? INT32_MAX
                    : scroll_delta < INT32_MIN ? INT32_MIN : (int32_t)scroll_delta;
    previous_scroll = encoder.steps;
    UsbHid_AddMotion(0, 0, wheel);
    UsbHid_ServiceMouse(buttons.pressed);
    osMutexAcquire(status_mutex, osWaitForever);
    shared_status.buttons = buttons;
    shared_status.encoder = encoder;
    osMutexRelease(status_mutex);
    ++next_wake;
    uint32_t now = osKernelGetTickCount();
    if ((int32_t)(next_wake - now) <= 0)
      next_wake = now + 1U;
    osDelayUntil(next_wake);
  }
}

static void FingerprintTask(void *argument)
{
  (void)argument;
  FingerprintController controller;
  Fingerprint_Init(&controller);
  for (;;)
  {
    uint32_t flags = osThreadFlagsWait(FP_CONTROL_FLAG, osFlagsWaitAny, 100);
    bool pressed = (flags & osFlagsError) == 0U && (flags & FP_CONTROL_FLAG) != 0U;
    uint16_t normal_id;
    if (FpService_Step(&controller, pressed, &normal_id)) ProfileManager_OnFingerprintMatch(normal_id);
    osMutexAcquire(status_mutex, osWaitForever);
    shared_status.fingerprint = controller;
    osMutexRelease(status_mutex);
  }
}

bool MouseApp_Init(SPI_HandleTypeDef *optical_spi, UART_HandleTypeDef *fingerprint_uart,
                   UART_HandleTypeDef *console_uart, I2C_HandleTypeDef *profile_i2c)
{
  motion_spi = optical_spi;
  console = console_uart;
  profile_bus = profile_i2c;
  memset(&shared_status, 0, sizeof(shared_status));
  shared_status.optical_status = PMW3360_STARTING;
  Fingerprint_Init(&shared_status.fingerprint);
  const osMutexAttr_t mutex_attributes = {
    .name = "mouseStatus",
    .attr_bits = osMutexPrioInherit
  };
  status_mutex = osMutexNew(&mutex_attributes);
  if (status_mutex == NULL || !Zw3020_Init(fingerprint_uart) || !Console_Init(console))
  {
    return false;
  }
  Encoder_Init();
  MouseButtons_Init();
  /* If held at startup, require release before the next PA0 action. */
  button_candidate = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET;
  button_stable = button_candidate;
  button_samples = 30U;
  button_presses = 0U;
  inputs_enabled = true;

  fingerprint_thread = osThreadNew(FingerprintTask, NULL, &fingerprint_attributes);
  motion_thread = osThreadNew(MotionTask, NULL, &motion_attributes);
  usb_thread = osThreadNew(WiredMouseTask, NULL, &usb_attributes);
  return fingerprint_thread != NULL && motion_thread != NULL && usb_thread != NULL;
}

static const char *FingerprintStateText(FingerprintState state)
{
  static const char *const names[] = {
    "Connecting", "Place finger to match", "Finding a free slot",
    "Place finger (1/2)", "Extracting scan 1", "Lift finger",
    "Same finger (2/2)", "Extracting scan 2", "Combining scans",
    "Saving template", "Extracting fingerprint", "Searching",
    "Lift for next scan", "Error - PA0 retries", "Checking duplicate", "Template ready for Save"
  };
  return names[state];
}

static const char *FingerprintResultText(FingerprintResult result)
{
  static const char *const names[] = {
    "Waiting", "Matched", "No match", "Enrolled", "Cancelled",
    "Enrollment timed out", "Fingerprint storage full", "Command failed", "Already enrolled", "Prepared in module RAM"
  };
  return names[result];
}

static const char *OpticalStatusText(Pmw3360Status status)
{
  static const char *const names[] = {
    "Ready", "Starting", "SPI error", "ID mismatch",
    "Timing error", "SROM mismatch", "Configuration error"
  };
  return names[status];
}

static void RenderLine(uint8_t row, const char *text)
{
  char padded[DISPLAY_COLUMNS + 1U];
  memset(padded, ' ', DISPLAY_COLUMNS);
  padded[DISPLAY_COLUMNS] = '\0';
  size_t length = strlen(text);
  if (length > DISPLAY_COLUMNS)
  {
    length = DISPLAY_COLUMNS;
  }
  memcpy(padded, text, length);
  if (memcmp(displayed_lines[row], padded, sizeof(padded)) != 0)
  {
    /* LCD_DrawString paints the character background, including padding. */
    LCD_DrawString(0, row * 16U, padded);
    memcpy(displayed_lines[row], padded, sizeof(padded));
  }
}

static void RenderStatus(const MouseStatus *status)
{
  char text[80];
  RenderLine(0, "ELEC3300 wired USB mouse");
  RenderLine(1, "USART1: config + Enter");
  RenderLine(2, FingerprintStateText(status->fingerprint.state));
  const FingerprintController *fp = &status->fingerprint;
  snprintf(text, sizeof(text), "FP: %s", FingerprintResultText(fp->result));
  RenderLine(3, text);
  if (fp->user_identified)
  {
    snprintf(text, sizeof(text), "User ID=%u score=%u", (unsigned)fp->user.id,
               (unsigned)fp->user.score);
  }
  else
  {
    snprintf(text, sizeof(text), "User: waiting for fingerprint");
  }
  RenderLine(4, text);
  snprintf(text, sizeof(text), "Fingerprints: %u/%u slot:%u", (unsigned)fp->storage.count,
             (unsigned)fp->storage.capacity, (unsigned)fp->enrollment_id);
  RenderLine(5, text);
  snprintf(text, sizeof(text), "Optical: %s DPI=%u", OpticalStatusText(status->optical_status), requested_dpi);
  RenderLine(6, text);
  snprintf(text, sizeof(text), "dX=%d dY=%d", status->motion.dx, status->motion.dy);
  RenderLine(7, text);
  snprintf(text, sizeof(text), "X total: %ld", (long)status->x_total);
  RenderLine(8, text);
  snprintf(text, sizeof(text), "Y total: %ld", (long)status->y_total);
  RenderLine(9, text);
  snprintf(text, sizeof(text), "Scroll: %ld", (long)status->encoder.steps);
  RenderLine(10, text);
  snprintf(text, sizeof(text), "SQ=%u lift=%u A=%u B=%u", (unsigned)status->motion.quality,
             (unsigned)status->motion.lifted, (unsigned)status->encoder.phase_a,
             (unsigned)status->encoder.phase_b);
  RenderLine(11, text);
  snprintf(text, sizeof(text), "FP touch=%u cmd=%02X code=%d",
             (unsigned)(HAL_GPIO_ReadPin(FP_TOUCH_GPIO_Port, FP_TOUCH_Pin) == GPIO_PIN_SET),
             (unsigned)fp->error_command, fp->error);
  RenderLine(12, text);
  uint8_t buttons = status->buttons.pressed;
  snprintf(text, sizeof(text), "L=%u R=%u M=%u (1=pressed)",
             (unsigned)((buttons & MOUSE_BUTTON_LEFT) != 0U),
             (unsigned)((buttons & MOUSE_BUTTON_RIGHT) != 0U),
             (unsigned)((buttons & MOUSE_BUTTON_MIDDLE) != 0U));
  RenderLine(13, text);
  snprintf(text, sizeof(text), "Side1=%u Side2=%u",
             (unsigned)((buttons & MOUSE_BUTTON_SIDE1) != 0U),
             (unsigned)((buttons & MOUSE_BUTTON_SIDE2) != 0U));
  RenderLine(14, text);
  snprintf(text, sizeof(text), "Raw pins=%02X USB overflow=%lu",
             (unsigned)status->buttons.pin_levels,
             (unsigned long)UsbHid_GetButtonOverflows());
  RenderLine(15, text);
  RenderLine(16, UsbHid_IsConfigured() ? "USB: mouse + keyboard ready" : "USB: waiting / suspended");
  RenderLine(17, Login_GetStatusText());
  snprintf(text, sizeof(text), "Heap free: %lu bytes", (unsigned long)xPortGetFreeHeapSize());
  RenderLine(18, text);
  UserProfile active;
  if (ProfileManager_GetActive(&active)) snprintf(text, sizeof(text), "Active: %s ID=%u", active.name, active.fingerprint_id);
  else snprintf(text, sizeof(text), "Profiles: %u/%u; scan user", ProfileStorage_Count(), PROFILE_MAX_USERS);
  RenderLine(19, text);
  UserProfile_ClearSecret(&active, sizeof(active));
}

void MouseApp_RunDisplay(void)
{
  ProfileManager_Init(profile_bus);
  ConfigMenu_Init();
  uint32_t last_display = HAL_GetTick() - 100U;
  for (;;)
  {
    Console_Service();
    ConfigMenu_Poll();
    ProfileManager_Update();
    MouseStatus snapshot;
    osMutexAcquire(status_mutex, osWaitForever);
    snapshot = shared_status;
    osMutexRelease(status_mutex);
    if ((uint32_t)(HAL_GetTick() - last_display) >= 100U)
    {last_display = HAL_GetTick(); RenderStatus(&snapshot);}

    osDelay(10);
  }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
  (void)task;
  (void)name;
  Error_Handler();
}
