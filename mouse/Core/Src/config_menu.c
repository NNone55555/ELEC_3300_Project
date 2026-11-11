#include "config_menu.h"
#include "console_uart.h"
#include "profile_manager.h"
#include "fingerprint_service.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
  MENU_NONE,
  MENU_ROOT,
  MENU_VERIFY,
  MENU_VERIFY_RETRY,
  MENU_SETTINGS,
  MENU_DPI,
  MENU_DPI_INPUT,
  MENU_USERNAME,
  MENU_USERNAME_INPUT,
  MENU_RENAME_INPUT,
  MENU_CONFIRM,
  MENU_RGB,
  MENU_MODE,
  MENU_COLOR,
  MENU_CUSTOM_RGB,
  MENU_BRIGHTNESS,
  MENU_SPEED,
  MENU_PASSWORD,
  MENU_PASSWORD_INPUT,
  MENU_PASSWORD_CONFIRM,
  MENU_PASSWORD_RETRY,
  MENU_MACROS,
  MENU_MACRO_SELECT,
  MENU_DELETE_CONFIRM,
  MENU_DELETE_WAIT,
  MENU_ADD,
  MENU_ADD_NAME,
  MENU_ENROLL,
  MENU_ENROLL_RETRY,
  MENU_ADOPT,
  MENU_REGISTER_FIELDS,
  MENU_REGISTER_CUSTOM,
  MENU_NEW_SUMMARY,
  MENU_SAVE_WAIT,
  MENU_CANCEL_WAIT,
  MENU_CANCEL_CLEANUP
} MenuState;
typedef enum
{
  CHANGE_DPI,
  CHANGE_USERNAME,
  CHANGE_NAME
} ChangeKind;

static MenuState state, cancel_destination;
static ChangeKind change_kind;
static UserProfile temporary;
static char candidate[PROFILE_NAME_MAX + 1U];
static uint16_t candidate_dpi;
static char password_first[PROFILE_PASSWORD_MAX + 1U];
static uint8_t macro_side, registration_step;
static bool registering, new_template, render_pending, discard_line;
static uint8_t paired_terminator;
static bool save_is_new;
static bool password_mismatch;
static FingerprintState last_progress;
static char line[64], message[160], output[1200];
static size_t line_length, output_length;

static void Show(MenuState next)
{
  state = next;
  render_pending = true;
}
static void Notice(const char *text)
{
  snprintf(message, sizeof(message), "%s", text);
  render_pending = true;
}
static void Print(const char *format, ...)
{
  if (output_length >= sizeof(output) - 1U)
    return;
  va_list arguments;
  va_start(arguments, format);
  int count = vsnprintf(output + output_length, sizeof(output) - output_length, format, arguments);
  va_end(arguments);
  if (count > 0)
    output_length += (size_t)count < sizeof(output) - output_length
                         ? (size_t)count
                         : sizeof(output) - output_length - 1U;
}
static bool Number(const char *text, uint32_t maximum, uint32_t *value)
{
  if (*text == 0)
    return false;
  uint32_t number = 0;
  while (*text)
  {
    if (*text < '0' || *text > '9' || number > maximum / 10U)
      return false;
    number = number * 10U + (unsigned)(*text++ - '0');
    if (number > maximum)
      return false;
  }
  *value = number;
  return true;
}
static bool Rgb(const char *text, uint8_t components[3])
{
  for (unsigned i = 0; i < 3U; ++i)
  {
    while (*text == ' ')
      ++text;
    if (*text < '0' || *text > '9')
      return false;
    char *end;
    unsigned long number = strtoul(text, &end, 10);
    if (number > 255U)
      return false;
    while (*end == ' ')
      ++end;
    if ((i < 2U && *end != ',') || (i == 2U && *end != 0))
      return false;
    components[i] = number;
    text = end + (i < 2U);
  }
  return true;
}
static void Palette(uint32_t selection)
{
  static const uint8_t colors[][3] = {{255, 0, 0},   {0, 255, 0},   {0, 0, 255},  {255, 255, 255},
                                      {255, 0, 255}, {0, 255, 255}, {255, 255, 0}};
  temporary.red = colors[selection - 1U][0];
  temporary.green = colors[selection - 1U][1];
  temporary.blue = colors[selection - 1U][2];
}
static void Preview(void)
{
  ProfileManager_Preview(&temporary);
}
static void ColorOptions(void)
{
  Print("1. Red\r\n2. Green\r\n3. Blue\r\n4. White\r\n5. Purple\r\n6. Cyan\r\n7. Yellow\r\n8. "
        "Custom RGB\r\n9. %s\r\n",
        registering ? "Keep current / Next" : "Back");
}
static void ModeOptions(void)
{
  Print("1. Off\r\n2. Static\r\n3. Breathing\r\n4. Rainbow\r\n");
}
static void SpeedOptions(void)
{
  Print("1. Very Slow\r\n2. Slow\r\n3. Normal\r\n4. Fast\r\n5. Very Fast\r\n");
}
static void MacroOptions(void)
{
  Print("1. Mouse Back\r\n2. Mouse Forward\r\n3. Ctrl+C\r\n4. Ctrl+V\r\n5. Ctrl+Z\r\n6. "
        "Ctrl+A\r\n7. Disabled\r\n");
}
static void Summary(void)
{
  Print("Name: %s\r\nFingerprint ID: %u\r\nDPI: %u\r\nRGB: %s - %s (%u,%u,%u)\r\nBrightness: "
        "%u%%\r\nEffect speed: %u\r\nComputer Username: %s\r\nPassword: [%s]\r\nSide Button 1: "
        "%s\r\nSide Button 2: %s\r\n",
        temporary.name, temporary.fingerprint_id, temporary.dpi,
        UserProfile_ModeName(temporary.rgb_mode), UserProfile_ColorName(&temporary), temporary.red,
        temporary.green, temporary.blue, temporary.brightness, temporary.effect_speed,
        temporary.computer_username, temporary.password[0] ? "SET" : "NOT SET",
        UserProfile_MacroName(temporary.side1_macro), UserProfile_MacroName(temporary.side2_macro));
}
static void Render(void)
{
  output_length = 0;
  if (message[0])
    Print("\r\n%s\r\n", message);
  switch (state)
  {
    case MENU_ROOT:
    {
      unsigned count = ProfileStorage_Count();
      UserProfile p;
      Print("\r\n====================================\r\n   ELEC3300 MOUSE "
            "CONFIGURATION\r\n====================================\r\nProfiles: %u/%u  EEPROM: "
            "%s\r\n",
            count, PROFILE_MAX_USERS, ProfileStorage_ErrorText(ProfileStorage_Status()));
      if (count > PROFILE_MAX_USERS)
        Print("Legacy profiles exceed the new two-user capacity. Delete one user before saving new "
              "passwords.\r\n");
      if (ProfileStorage_Status() == STORAGE_IO_ERROR)
      {
        At24c02Diagnostic error = At24c02_GetDiagnostic();
        Print("I2C1 device 0x50: %s at 0x%02X, HAL=%u error=0x%08lX SCL=%u SDA=%u BUSY=%u\r\n"
              "Profile list unavailable until EEPROM reads succeed.\r\n",
              error.operation, error.address, (unsigned)error.hal_status,
              (unsigned long)error.hal_error, (unsigned)error.scl_high, (unsigned)error.sda_high,
              (unsigned)error.bus_busy);
      }
      for (unsigned i = 0; ProfileStorage_Get(i, &p); ++i)
        Print("%u. User ID %u - %s\r\n", i + 1U, p.fingerprint_id, p.name);
      UserProfile_ClearSecret(&p, sizeof(p));
      Print("%u. Add New User\r\n%u. Exit Configuration\r\n%u. Retry EEPROM\r\n%u. Finish pending "
            "registration/deletion recovery\r\n",
            count + 1U, count + 2U, count + 3U, count + 4U);
      break;
    }
    case MENU_VERIFY:
      Print("\r\nSelected: %s (ID %u)\r\nPlease place your finger on the fingerprint "
            "sensor...\r\n0. Back\r\n",
            temporary.name, temporary.fingerprint_id);
      break;
    case MENU_VERIFY_RETRY:
      Print("\r\nFingerprint does not match, timed out, or sensor failed.\r\n1. Retry\r\n2. "
            "Back\r\n");
      break;
    case MENU_SETTINGS:
      Print("\r\nWelcome, %s.\r\n========== USER SETTINGS ==========\r\n1. Change DPI\r\n2. "
            "Configure RGB\r\n3. Change Computer Username\r\n4. Change Computer Password\r\n5. "
            "Configure Side-Button Macros\r\n6. Rename User\r\n7. Delete User\r\n8. Save "
            "Changes\r\n9. Cancel / Back\r\n",
            temporary.name);
      break;
    case MENU_DPI:
      Print("\r\n========== DPI SETTINGS ==========\r\nCurrent DPI: %u\r\n1. Change DPI\r\n2. "
            "Back\r\n",
            temporary.dpi);
      break;
    case MENU_DPI_INPUT:
      Print("\r\nEnter new DPI (100..12000, multiples of 100):\r\n0. Cancel / Back\r\n");
      break;
    case MENU_USERNAME:
      Print("\r\n========== COMPUTER USERNAME ==========\r\nCurrent Username: %s\r\n1. Change "
            "Username\r\n2. Back\r\n",
            temporary.computer_username);
      break;
    case MENU_USERNAME_INPUT:
      Print("\r\nEnter new computer username (1..15 printable ASCII):\r\n0. Cancel / Back\r\n");
      break;
    case MENU_RENAME_INPUT:
      Print(
          "\r\nCurrent Name: %s\r\nEnter new name (1..15 printable ASCII):\r\n0. Cancel / Back\r\n",
          temporary.name);
      break;
    case MENU_CONFIRM:
      if (change_kind == CHANGE_DPI)
        Print("\r\nNew DPI: %u\r\n", candidate_dpi);
      else
        Print("\r\nNew %s: %s\r\n", change_kind == CHANGE_NAME ? "Name" : "Username", candidate);
      Print("1. Confirm\r\n2. Retry\r\n3. Cancel / Back\r\n");
      break;
    case MENU_RGB:
      Print("\r\n========== RGB SETTINGS ==========\r\nCurrent Mode: %s\r\nCurrent Color: %s "
            "(%u,%u,%u)\r\nCurrent Brightness: %u%%\r\nCurrent Effect Speed: %u\r\n1. Change "
            "Lighting Mode\r\n2. Change Color\r\n3. Change Brightness\r\n4. Change Effect "
            "Speed\r\n5. Back\r\n",
            UserProfile_ModeName(temporary.rgb_mode), UserProfile_ColorName(&temporary),
            temporary.red, temporary.green, temporary.blue, temporary.brightness,
            temporary.effect_speed);
      break;
    case MENU_MODE:
      Print("\r\n========== LIGHTING MODE ==========\r\n");
      ModeOptions();
      Print("5. Back\r\n");
      break;
    case MENU_COLOR:
      Print("\r\n========== COLOR ==========\r\n");
      ColorOptions();
      break;
    case MENU_CUSTOM_RGB:
    case MENU_REGISTER_CUSTOM:
      Print("\r\nEnter RGB (R,G,B), example 255,0,128:\r\n0. Cancel / Back\r\n");
      break;
    case MENU_BRIGHTNESS:
      Print("\r\nEnter brightness (0..100%%):\r\n101. Cancel / Back\r\n");
      break;
    case MENU_SPEED:
      Print("\r\n========== EFFECT SPEED ==========\r\n");
      SpeedOptions();
      Print("6. Back\r\n");
      break;
    case MENU_PASSWORD:
      Print("\r\n========== COMPUTER PASSWORD ==========\r\nPassword Status: [%s]\r\n"
            "Stored unencrypted in EEPROM (prototype).\r\n1. Change Password\r\n2. "
            "Clear Password\r\n3. Back\r\n",
            temporary.password[0] ? "SET" : "NOT SET");
      break;
    case MENU_PASSWORD_INPUT:
      if (registering)
        Print("\r\n========== REGISTRATION 7/9 ==========\r\n");
      Print("\r\nEnter new password (hidden, 1..19 characters, A-Z/a-z/0-9 only):\r\n"
            "-1. Cancel / Back\r\n");
      break;
    case MENU_PASSWORD_CONFIRM:
      Print("\r\nConfirm new password (hidden):\r\n-1. Cancel / Back\r\n");
      break;
    case MENU_PASSWORD_RETRY:
      Print("\r\n%s\r\n1. Retry\r\n2. %s\r\n",
            password_mismatch ? "Passwords do not match. Re-enter the password twice."
                              : "Invalid password. Use 1..19 characters: A-Z, a-z, 0-9 only.",
            registering ? "Cancel Registration" : "Back");
      break;
    case MENU_MACROS:
      Print("\r\n========== BUTTON MACROS ==========\r\nSide Button 1: %s\r\nSide Button 2: "
            "%s\r\n1. Configure Side Button 1\r\n2. Configure Side Button 2\r\n3. Back\r\n",
            UserProfile_MacroName(temporary.side1_macro),
            UserProfile_MacroName(temporary.side2_macro));
      break;
    case MENU_MACRO_SELECT:
      Print("\r\n========== SELECT FUNCTION: SIDE %u ==========\r\n", macro_side + 1U);
      MacroOptions();
      Print("8. Back\r\n");
      break;
    case MENU_DELETE_CONFIRM:
      Print("\r\nWARNING: Delete this user?\r\nUser: %s\r\nFingerprint ID: %u\r\n1. Confirm "
            "Delete\r\n2. Cancel / Back\r\n",
            temporary.name, temporary.fingerprint_id);
      break;
    case MENU_DELETE_WAIT:
    case MENU_CANCEL_CLEANUP:
      Print("\r\nFinishing journaled single-user cleanup; please wait...\r\n");
      break;
    case MENU_SAVE_WAIT:
      Print("\r\nSaving and verifying storage; please wait...\r\n");
      break;
    case MENU_CANCEL_WAIT:
      Print("\r\nCancelling fingerprint operation; please wait...\r\n");
      break;
    case MENU_ADD:
      Print("\r\n========== ADD NEW USER ==========\r\n1. Start Registration\r\n2. Back\r\n");
      break;
    case MENU_ADD_NAME:
      Print(
          "\r\nPlease enter a display name (1..15 printable ASCII):\r\n0. Cancel Registration\r\n");
      break;
    case MENU_ENROLL:
    {
      FpServiceStatus fp = FpService_GetStatus();
      Print("\r\n========== FINGERPRINT REGISTRATION ==========\r\n");
      if (fp.progress == FP_WAIT_FIRST_REMOVAL)
        Print("Fingerprint captured! Remove your finger.\r\n");
      else if (fp.progress == FP_WAIT_SECOND_SCAN)
        Print("Place the SAME finger again...\r\n");
      else if (fp.progress == FP_FIND_FREE_SLOT || fp.progress == FP_WAIT_FIRST_SCAN)
        Print("Please place your finger on the fingerprint sensor...\r\n");
      else
        Print("Checking fingerprint / compatibility...\r\n");
      Print("0. Cancel Registration\r\n");
      break;
    }
    case MENU_ENROLL_RETRY:
      Print("\r\nEnrollment failed, timed out, or fingerprint is already linked to a user.\r\n1. "
            "Retry\r\n2. Cancel Registration\r\n");
      break;
    case MENU_ADOPT:
      Print("\r\nFingerprint ID %u already exists in the module but has no profile.\r\n1. Link "
            "this verified existing fingerprint\r\n2. Cancel Registration\r\n",
            temporary.fingerprint_id);
      break;
    case MENU_REGISTER_FIELDS:
      Print("\r\n========== REGISTRATION %u/9 ==========\r\n", registration_step + 1U);
      switch (registration_step)
      {
        case 0:
          Print("Preferred DPI (100..12000 step 100), current: %u\r\n9. Keep current\r\n",
                temporary.dpi);
          break;
        case 1:
          Print("Lighting mode, current: %s\r\n", UserProfile_ModeName(temporary.rgb_mode));
          ModeOptions();
          Print("5. Keep current\r\n");
          break;
        case 2:
          Print("Color:\r\n");
          ColorOptions();
          break;
        case 3:
          Print(
              "Brightness 0..100, current: %u\r\n101. Cancel Registration\r\n102. Keep current\r\n",
              temporary.brightness);
          break;
        case 4:
          Print("Effect speed, current: %u\r\n", temporary.effect_speed);
          SpeedOptions();
          Print("6. Keep current\r\n");
          break;
        case 5:
          Print("Computer username (max 15), current: %s\r\n9. Keep current\r\n",
                temporary.computer_username);
          break;
        case 6:
          Print("1. Enter password (1..19 characters: A-Z, a-z, 0-9)\r\n"
                "2. Leave unset\r\n");
          break;
        case 7:
        case 8:
          Print("Side Button %u, current: %s\r\n", registration_step - 6U,
                UserProfile_MacroName(registration_step == 7U ? temporary.side1_macro
                                                              : temporary.side2_macro));
          MacroOptions();
          Print("8. Keep current\r\n");
          break;
      }
      if (registration_step != 3U)
        Print("0. Cancel Registration\r\n");
      break;
    case MENU_NEW_SUMMARY:
      Print("\r\n========== NEW USER SUMMARY ==========\r\n");
      Summary();
      Print("1. Save New User\r\n2. Cancel Registration\r\n");
      break;
    case MENU_NONE:
      break;
  }
  if (state != MENU_NONE)
    Print("\r\nPlease select an option / enter value: ");
  if (Console_Write(output, output_length))
  {
    render_pending = false;
    message[0] = 0;
  }
}

static bool SecretInput(void)
{
  return state == MENU_PASSWORD_INPUT || state == MENU_PASSWORD_CONFIRM ||
         (state == MENU_REGISTER_FIELDS && registration_step == 6U);
}
static void PasswordRejected(bool mismatch)
{
  password_mismatch = mismatch;
  UserProfile_ClearSecret(password_first, sizeof(password_first));
  Show(MENU_PASSWORD_RETRY);
}
static void CancelTo(MenuState destination)
{
  UserProfile_ClearSecret(password_first, sizeof(password_first));
  FpService_Cancel();
  cancel_destination = destination;
  Show(MENU_CANCEL_WAIT);
}
static void FinishRegistrationCancel(void)
{
  UserProfile pending;
  ProfileRecordState kind;
  if (new_template && ProfileStorage_Pending(&pending, &kind) &&
      pending.fingerprint_id == temporary.fingerprint_id && ProfileManager_RetryRecovery())
  {
    Show(MENU_CANCEL_CLEANUP);
    return;
  }
  ProfileManager_EndPreview(NULL);
  registering = new_template = false;
  UserProfile_ClearSecret(&temporary, sizeof(temporary));
  Show(MENU_ROOT);
}
static void BeginEnroll(void)
{
  if (FpService_Request(FP_JOB_ENROLL, 0))
  {
    last_progress = (FingerprintState)-1;
    Show(MENU_ENROLL);
  }
  else
  {
    Notice("Fingerprint service is busy; retry shortly.");
    Show(MENU_ENROLL_RETRY);
  }
}
static void NextRegistration(void)
{
  ++registration_step;
  Preview();
  Show(registration_step >= 9U ? MENU_NEW_SUMMARY : MENU_REGISTER_FIELDS);
}
static void RegisterInput(const char *text)
{
  uint32_t number = 0;
  bool numeric = Number(text, 12000, &number);
  if (*text == 0)
    return;
  if ((registration_step != 3U && numeric && number == 0U) ||
      (registration_step == 3U && numeric && number == 101U))
  {
    CancelTo(MENU_ROOT);
    return;
  }
  /* A blank line cannot identify a deliberate default choice: some monitors
   * send multiple terminators for one Enter. Every field has an explicit
   * numbered Keep/Leave unset option, so a trailing blank cannot skip a step.
   */
  static const uint16_t keep_options[] = {9, 5, 9, 102, 6, 9, 2, 8, 8};
  if (registration_step < sizeof(keep_options) / sizeof(keep_options[0]) && numeric &&
      number == keep_options[registration_step])
  {
    if (registration_step == 6U)
      UserProfile_ClearSecret(temporary.password, sizeof(temporary.password));
    NextRegistration();
    return;
  }
  switch (registration_step)
  {
    case 0:
      if (!numeric || !UserProfile_ValidDpi(number))
      {
        Notice("Invalid DPI: use 100..12000 in steps of 100.");
        return;
      }
      temporary.dpi = number;
      break;
    case 1:
      if (!numeric || number < 1U || number > 4U)
      {
        Notice("Select a mode 1..4.");
        return;
      }
      temporary.rgb_mode = number - 1U;
      break;
    case 2:
      if (!numeric || number < 1U || number > 9U)
      {
        Notice("Select a color 1..9.");
        return;
      }
      if (number == 8U)
      {
        Show(MENU_REGISTER_CUSTOM);
        return;
      }
      if (number <= 7U)
        Palette(number);
      break;
    case 3:
      if (!numeric || number > 100U)
      {
        Notice("Brightness must be 0..100.");
        return;
      }
      temporary.brightness = number;
      break;
    case 4:
      if (!numeric || number < 1U || number > 5U)
      {
        Notice("Speed must be 1..5.");
        return;
      }
      temporary.effect_speed = number;
      break;
    case 5:
      if (!UserProfile_ValidText(text, PROFILE_USERNAME_MAX, false))
      {
        Notice("Username must be 1..15 printable ASCII characters.");
        return;
      }
      memset(temporary.computer_username, 0, sizeof(temporary.computer_username));
      strcpy(temporary.computer_username, text);
      break;
    case 6:
      if (!numeric || number != 1U)
      {
        Notice("Select 1 to enter a password, or 2 to leave unset.");
        return;
      }
      UserProfile_ClearSecret(password_first, sizeof(password_first));
      Show(MENU_PASSWORD_INPUT);
      return;
    case 7:
    case 8:
      if (!numeric || number < 1U || number > 7U)
      {
        Notice("Select function 1..7.");
        return;
      }
      if (registration_step == 7U)
        temporary.side1_macro = number - 1U;
      else
        temporary.side2_macro = number - 1U;
      break;
  }
  NextRegistration();
}

static void ProcessLine(char *text)
{
  uint32_t option = UINT32_MAX;
  bool numeric = Number(text, 12000, &option);
  if (state == MENU_NONE)
  {
    if (strcmp(text, "config") == 0)
    {
      ProfileManager_SetConfigurationMode(true);
      Show(MENU_ROOT);
    }
    return;
  }
  if (state == MENU_REGISTER_FIELDS)
  {
    RegisterInput(text);
    return;
  }
  if (state == MENU_DPI_INPUT || state == MENU_USERNAME_INPUT || state == MENU_RENAME_INPUT)
  {
    if (numeric && option == 0U)
    {
      Show(state == MENU_DPI_INPUT        ? MENU_DPI
           : state == MENU_USERNAME_INPUT ? MENU_USERNAME
                                          : MENU_SETTINGS);
      return;
    }
    if (state == MENU_DPI_INPUT)
    {
      if (!numeric || !UserProfile_ValidDpi(option))
      {
        Notice("Invalid DPI: 100..12000, step 100.");
        return;
      }
      candidate_dpi = option;
      change_kind = CHANGE_DPI;
    }
    else
    {
      if (!UserProfile_ValidText(text, PROFILE_NAME_MAX, false))
      {
        Notice("Use 1..15 printable ASCII characters.");
        return;
      }
      memset(candidate, 0, sizeof(candidate));
      strcpy(candidate, text);
      change_kind = state == MENU_RENAME_INPUT ? CHANGE_NAME : CHANGE_USERNAME;
    }
    Show(MENU_CONFIRM);
    return;
  }
  if (state == MENU_ADD_NAME)
  {
    if (numeric && option == 0U)
    {
      CancelTo(MENU_ROOT);
      return;
    }
    if (!UserProfile_ValidText(text, PROFILE_NAME_MAX, false))
    {
      Notice("Use 1..15 printable ASCII characters.");
      return;
    }
    UserProfile_Default(&temporary, text, UINT16_MAX);
    BeginEnroll();
    return;
  }
  if (state == MENU_CUSTOM_RGB || state == MENU_REGISTER_CUSTOM)
  {
    if (numeric && option == 0U)
    {
      Show(state == MENU_REGISTER_CUSTOM ? MENU_REGISTER_FIELDS : MENU_COLOR);
      return;
    }
    uint8_t rgb[3];
    if (!Rgb(text, rgb))
    {
      Notice("Invalid RGB. Enter exactly three values 0..255 separated by commas.");
      return;
    }
    temporary.red = rgb[0];
    temporary.green = rgb[1];
    temporary.blue = rgb[2];
    Preview();
    if (state == MENU_REGISTER_CUSTOM)
      NextRegistration();
    else
      Show(MENU_RGB);
    return;
  }
  if (state == MENU_PASSWORD_INPUT || state == MENU_PASSWORD_CONFIRM)
  {
    /* -1 is outside the password alphabet, leaving literal '0'/'1' valid. */
    if (strcmp(text, "-1") == 0)
    {
      UserProfile_ClearSecret(password_first, sizeof(password_first));
      if (registering)
        Show(MENU_REGISTER_FIELDS);
      else
        Show(MENU_PASSWORD);
      return;
    }
    if (state == MENU_PASSWORD_INPUT)
    {
      if (!UserProfile_ValidPassword(text))
      {
        PasswordRejected(false);
        return;
      }
      UserProfile_ClearSecret(password_first, sizeof(password_first));
      memcpy(password_first, text, strlen(text));
      Show(MENU_PASSWORD_CONFIRM);
      return;
    }
    if (!UserProfile_ValidPassword(text) || strcmp(text, password_first) != 0)
    {
      PasswordRejected(true);
      return;
    }
    UserProfile_ClearSecret(temporary.password, sizeof(temporary.password));
    memcpy(temporary.password, password_first, strlen(password_first));
    UserProfile_ClearSecret(password_first, sizeof(password_first));
    Notice("Password updated successfully! (Not saved yet.)");
    if (registering)
      NextRegistration();
    else
      Show(MENU_PASSWORD);
    return;
  }
  if (!numeric)
  {
    Notice("Invalid option. Use the numbered choices.");
    return;
  }
  switch (state)
  {
    case MENU_ROOT:
    {
      unsigned count = ProfileStorage_Count();
      if (option >= 1U && option <= count)
      {
        (void)ProfileStorage_Get(option - 1U, &temporary);
        if (FpService_Request(FP_JOB_VERIFY, temporary.fingerprint_id))
          Show(MENU_VERIFY);
        else
          Notice("Fingerprint service busy; retry.");
      }
      else if (option == count + 1U)
        Show(MENU_ADD);
      else if (option == count + 2U)
      {
        ProfileManager_SetConfigurationMode(false);
        state = MENU_NONE;
        render_pending = false;
        const char *exit_text = "\r\nConfiguration exited. Normal mouse operation resumed.\r\n";
        (void)Console_Write(exit_text, strlen(exit_text));
      }
      else if (option == count + 3U)
        Notice(ProfileStorage_ErrorText(ProfileManager_RetryStorage()));
      else if (option == count + 4U)
      {
        if (ProfileManager_RetryRecovery())
          Show(MENU_DELETE_WAIT);
        else
          Notice("No recoverable pending record, or EEPROM is unavailable.");
      }
      else
        Notice("Invalid option.");
      break;
    }
    case MENU_VERIFY:
      if (option == 0U)
        CancelTo(MENU_ROOT);
      else
        Notice("Waiting for a fingerprint; 0 cancels.");
      break;
    case MENU_VERIFY_RETRY:
      if (option == 1U)
      {
        if (FpService_Request(FP_JOB_VERIFY, temporary.fingerprint_id))
          Show(MENU_VERIFY);
        else
          Notice("Sensor busy; retry.");
      }
      else if (option == 2U)
        CancelTo(MENU_ROOT);
      else
        Notice("Invalid option.");
      break;
    case MENU_SETTINGS:
      switch (option)
      {
        case 1:
          Show(MENU_DPI);
          break;
        case 2:
          Show(MENU_RGB);
          break;
        case 3:
          Show(MENU_USERNAME);
          break;
        case 4:
          Show(MENU_PASSWORD);
          break;
        case 5:
          Show(MENU_MACROS);
          break;
        case 6:
          Show(MENU_RENAME_INPUT);
          break;
        case 7:
          Show(MENU_DELETE_CONFIRM);
          break;
        case 8:
          save_is_new = false;
          if (ProfileManager_Create(&temporary, false))
            Show(MENU_SAVE_WAIT);
          else
            Notice("Operation busy.");
          break;
        case 9:
          ProfileManager_EndPreview(NULL);
          UserProfile_ClearSecret(&temporary, sizeof(temporary));
          Show(MENU_ROOT);
          break;
        default:
          Notice("Invalid option.");
          break;
      }
      break;
    case MENU_DPI:
      if (option == 1U)
        Show(MENU_DPI_INPUT);
      else if (option == 2U)
        Show(MENU_SETTINGS);
      else
        Notice("Invalid option.");
      break;
    case MENU_USERNAME:
      if (option == 1U)
        Show(MENU_USERNAME_INPUT);
      else if (option == 2U)
        Show(MENU_SETTINGS);
      else
        Notice("Invalid option.");
      break;
    case MENU_CONFIRM:
      if (option == 1U)
      {
        if (change_kind == CHANGE_DPI)
        {
          temporary.dpi = candidate_dpi;
          Preview();
          Notice("DPI updated successfully! (Preview; not saved.)");
          Show(MENU_DPI);
        }
        else if (change_kind == CHANGE_USERNAME)
        {
          strcpy(temporary.computer_username, candidate);
          Notice("Username updated (not saved).");
          Show(MENU_USERNAME);
        }
        else
        {
          strcpy(temporary.name, candidate);
          Notice("Name updated (fingerprint ID unchanged; not saved).");
          Show(MENU_SETTINGS);
        }
        UserProfile_ClearSecret(candidate, sizeof(candidate));
      }
      else if (option == 2U)
        Show(change_kind == CHANGE_DPI        ? MENU_DPI_INPUT
             : change_kind == CHANGE_USERNAME ? MENU_USERNAME_INPUT
                                              : MENU_RENAME_INPUT);
      else if (option == 3U)
        Show(change_kind == CHANGE_DPI        ? MENU_DPI
             : change_kind == CHANGE_USERNAME ? MENU_USERNAME
                                              : MENU_SETTINGS);
      else
        Notice("Invalid option.");
      break;
    case MENU_RGB:
      if (option >= 1U && option <= 5U)
      {
        const MenuState destinations[] = {MENU_MODE, MENU_COLOR, MENU_BRIGHTNESS, MENU_SPEED,
                                          MENU_SETTINGS};
        Show(destinations[option - 1U]);
      }
      else
        Notice("Invalid option.");
      break;
    case MENU_MODE:
      if (option >= 1U && option <= 4U)
      {
        temporary.rgb_mode = option - 1U;
        Preview();
        Show(MENU_RGB);
      }
      else if (option == 5U)
        Show(MENU_RGB);
      else
        Notice("Invalid option.");
      break;
    case MENU_COLOR:
      if (option >= 1U && option <= 7U)
      {
        Palette(option);
        Preview();
        Show(MENU_RGB);
      }
      else if (option == 8U)
        Show(MENU_CUSTOM_RGB);
      else if (option == 9U)
        Show(MENU_RGB);
      else
        Notice("Invalid option.");
      break;
    case MENU_BRIGHTNESS:
      if (option <= 100U)
      {
        temporary.brightness = option;
        Preview();
        Show(MENU_RGB);
      }
      else if (option == 101U)
        Show(MENU_RGB);
      else
        Notice("Brightness must be 0..100.");
      break;
    case MENU_SPEED:
      if (option >= 1U && option <= 5U)
      {
        temporary.effect_speed = option;
        Preview();
        Show(MENU_RGB);
      }
      else if (option == 6U)
        Show(MENU_RGB);
      else
        Notice("Invalid option.");
      break;
    case MENU_PASSWORD:
      if (option == 1U)
      {
        UserProfile_ClearSecret(password_first, sizeof(password_first));
        Show(MENU_PASSWORD_INPUT);
      }
      else if (option == 2U)
      {
        UserProfile_ClearSecret(temporary.password, sizeof(temporary.password));
        Notice("Password cleared (not saved).");
        Show(MENU_PASSWORD);
      }
      else if (option == 3U)
        Show(MENU_SETTINGS);
      else
        Notice("Invalid option.");
      break;
    case MENU_PASSWORD_RETRY:
      if (option == 1U)
      {
        if (registering)
        {
          registration_step = 6;
          Show(MENU_PASSWORD_INPUT);
        }
        else
          Show(MENU_PASSWORD_INPUT);
      }
      else if (option == 2U)
      {
        if (registering)
          CancelTo(MENU_ROOT);
        else
          Show(MENU_PASSWORD);
      }
      else
        Notice("Invalid option.");
      break;
    case MENU_MACROS:
      if (option == 1U || option == 2U)
      {
        macro_side = option - 1U;
        Show(MENU_MACRO_SELECT);
      }
      else if (option == 3U)
        Show(MENU_SETTINGS);
      else
        Notice("Invalid option.");
      break;
    case MENU_MACRO_SELECT:
      if (option >= 1U && option <= 7U)
      {
        if (macro_side == 0U)
          temporary.side1_macro = option - 1U;
        else
          temporary.side2_macro = option - 1U;
        Preview();
        Show(MENU_MACROS);
      }
      else if (option == 8U)
        Show(MENU_MACROS);
      else
        Notice("Invalid option.");
      break;
    case MENU_DELETE_CONFIRM:
      if (option == 1U)
      {
        if (ProfileManager_Delete(&temporary))
          Show(MENU_DELETE_WAIT);
        else
          Notice("Operation busy.");
      }
      else if (option == 2U)
        Show(MENU_SETTINGS);
      else
        Notice("Invalid option.");
      break;
    case MENU_ADD:
      if (option == 1U)
      {
        if (ProfileStorage_Status() != STORAGE_OK)
          Notice("Registration blocked: EEPROM is not ready. Select 2 Back, then Retry EEPROM "
                 "on the main menu.");
        else if (!ProfileManager_CanRegister())
          Notice("No capacity, pending recovery, or EEPROM not writable. Resolve storage first.");
        else
        {
          registering = true;
          new_template = false;
          Show(MENU_ADD_NAME);
        }
      }
      else if (option == 2U)
        Show(MENU_ROOT);
      else
        Notice("Invalid option.");
      break;
    case MENU_ENROLL:
      if (option == 0U)
        CancelTo(MENU_ROOT);
      else
        Notice("Follow the scan prompts; 0 cancels registration.");
      break;
    case MENU_ENROLL_RETRY:
      if (option == 1U)
        BeginEnroll();
      else if (option == 2U)
        CancelTo(MENU_ROOT);
      else
        Notice("Invalid option.");
      break;
    case MENU_ADOPT:
      if (option == 1U)
      {
        new_template = false;
        registration_step = 0;
        ProfileManager_BeginPreview(&temporary);
        Show(MENU_REGISTER_FIELDS);
      }
      else if (option == 2U)
        CancelTo(MENU_ROOT);
      else
        Notice("Invalid option.");
      break;
    case MENU_NEW_SUMMARY:
      if (option == 1U)
      {
        save_is_new = true;
        if (ProfileManager_Create(&temporary, new_template))
          Show(MENU_SAVE_WAIT);
        else
          Notice("Operation busy.");
      }
      else if (option == 2U)
        CancelTo(MENU_ROOT);
      else
        Notice("Invalid option.");
      break;
    case MENU_SAVE_WAIT:
    case MENU_DELETE_WAIT:
    case MENU_CANCEL_WAIT:
    case MENU_CANCEL_CLEANUP:
      Notice("Operation in progress; wait for completion.");
      break;
    default:
      Notice("Invalid option.");
      break;
  }
}

static void PollBackend(void)
{
  FpServiceStatus fp = FpService_GetStatus();
  if (state == MENU_VERIFY && fp.reply != FP_REPLY_BUSY)
  {
    if (fp.reply == FP_REPLY_VERIFIED)
    {
      ProfileManager_BeginPreview(&temporary);
      Notice("Fingerprint verified!");
      Show(MENU_SETTINGS);
    }
    else
      Show(MENU_VERIFY_RETRY);
  }
  else if (state == MENU_ENROLL)
  {
    if (fp.reply == FP_REPLY_PREPARED)
    {
      temporary.fingerprint_id = fp.id;
      new_template = true;
      registration_step = 0;
      ProfileManager_BeginPreview(&temporary);
      Notice("Scans are compatible. Template prepared in RAM; final Save persists it.");
      Show(MENU_REGISTER_FIELDS);
    }
    else if (fp.reply == FP_REPLY_DUPLICATE)
    {
      if (ProfileStorage_Find(fp.id, NULL))
        Show(MENU_ENROLL_RETRY);
      else
      {
        temporary.fingerprint_id = fp.id;
        Show(MENU_ADOPT);
      }
    }
    else if (fp.reply != FP_REPLY_BUSY)
      Show(MENU_ENROLL_RETRY);
    else if (fp.progress != last_progress)
    {
      last_progress = fp.progress;
      render_pending = true;
    }
  }
  else if (state == MENU_SAVE_WAIT && ProfileManager_Operation() != PROFILE_OP_BUSY)
  {
    if (ProfileManager_Operation() == PROFILE_OP_OK)
    {
      ProfileManager_EndPreview(&temporary);
      registering = new_template = false;
      UserProfile_ClearSecret(&temporary, sizeof(temporary));
      Notice(save_is_new ? "New user saved and verified!" : "Changes saved and verified!");
      Show(MENU_ROOT);
    }
    else if (save_is_new)
    {
      Notice("Registration save failed. Rolling back; unresolved journal records remain disabled "
             "for recovery.");
      CancelTo(MENU_ROOT);
    }
    else
    {
      Notice("Save failed. Changes are NOT confirmed. Cancel to restore preview; retry EEPROM from "
             "the main menu.");
      Show(MENU_SETTINGS);
    }
  }
  else if ((state == MENU_DELETE_WAIT || state == MENU_CANCEL_CLEANUP) &&
           ProfileManager_Operation() != PROFILE_OP_BUSY)
  {
    ProfileManager_EndPreview(NULL);
    registering = new_template = false;
    UserProfile_ClearSecret(&temporary, sizeof(temporary));
    Notice(ProfileManager_Operation() == PROFILE_OP_OK
               ? "User/template cleanup complete."
               : "Cleanup incomplete: pending user stays disabled. Retry EEPROM/recovery.");
    Show(MENU_ROOT);
  }
  else if (state == MENU_CANCEL_WAIT && fp.reply == FP_REPLY_CANCELLED)
  {
    if (registering)
      FinishRegistrationCancel();
    else
      Show(cancel_destination);
  }
}

void ConfigMenu_Init(void)
{
  state = MENU_NONE;
  line_length = 0;
  render_pending = discard_line = false;
  paired_terminator = 0;
  registering = new_template = false;
  UserProfile_ClearSecret(password_first, sizeof(password_first));
  password_mismatch = false;
  memset(line, 0, sizeof(line));
  const char *banner = "\r\nELEC3300 configuration ready: type config then Enter.\r\n"
                       "Password storage is unencrypted (prototype).\r\n";
  (void)Console_Write(banner, strlen(banner));
}
bool ConfigMenu_Active(void)
{
  return state != MENU_NONE;
}
bool ConfigMenu_HasInput(void)
{
  return line_length != 0U;
}
void ConfigMenu_Poll(void)
{
  PollBackend();
  if (render_pending)
  {
    Render();
    if (render_pending)
      return;
  }
  for (unsigned budget = 0; budget < 64U; ++budget)
  {
    uint8_t byte;
    int result = Console_ReadByte(&byte);
    if (result == 0)
      break;
    if (result < 0)
    {
      UserProfile_ClearSecret(line, sizeof(line));
      line_length = 0;
      discard_line = true;
      UserProfile_ClearSecret(password_first, sizeof(password_first));
      Notice("UART RX overflow/error: current input discarded. Send Enter, then retry.");
      continue;
    }
    if (paired_terminator != 0U && byte == paired_terminator)
    {
      paired_terminator = 0;
      continue;
    }
    if (byte == '\r' || byte == '\n')
    {
      paired_terminator = byte == '\r' ? '\n' : '\r';
      if (!discard_line)
      {
        line[line_length] = 0;
        if (!SecretInput())
        {
          while (line_length && line[line_length - 1U] == ' ')
            line[--line_length] = 0;
          size_t start = 0;
          while (line[start] == ' ')
            ++start;
          if (start)
            memmove(line, line + start, line_length - start + 1U);
        }
        /* Empty submissions never advance a prompt. Wizard defaults use
         * explicit choices so duplicate Enter bytes cannot skip a field.
         */
        if (line[0] != '\0')
        {
          if (state != MENU_NONE)
            (void)Console_Write("\r\n", 2);
          ProcessLine(line);
        }
      }
      else
        Notice("Input line discarded; no changes made. Retry the prompt.");
      UserProfile_ClearSecret(line, sizeof(line));
      line_length = 0;
      discard_line = false;
      if (render_pending)
        break;
      continue;
    }
    paired_terminator = 0;
    if (discard_line)
      continue;
    if (byte == 8U || byte == 127U)
    {
      if (line_length)
      {
        line[--line_length] = 0;
        if (state != MENU_NONE && !SecretInput())
          (void)Console_Write("\b \b", 3);
      }
      continue;
    }
    size_t maximum = SecretInput() ? PROFILE_PASSWORD_INPUT_MAX : sizeof(line) - 1U;
    if (byte < 32U || byte > 126U || line_length >= maximum)
    {
      discard_line = true;
      UserProfile_ClearSecret(line, sizeof(line));
      line_length = 0;
      UserProfile_ClearSecret(password_first, sizeof(password_first));
      continue;
    }
    line[line_length++] = byte;
    if (state != MENU_NONE && !SecretInput())
    {
      char echo = byte;
      (void)Console_Write(&echo, 1);
    }
  }
}
