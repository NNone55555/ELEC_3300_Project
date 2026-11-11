#include "login_typing.h"
#include "usb_hid_controller.h"
#include "usbd_hid.h"
#include "stm32f1xx_hal.h"
#include "user_profile.h"
#include <stddef.h>
#include <string.h>

#define KEY_INTERVAL_MS 15U
#define LOGIN_TIMEOUT_MS 10000U
#define LEFT_SHIFT_MODIFIER 0x02U
#define ENTER_USAGE 0x28U
#define CAPS_LOCK_LED 0x02U

/* Private RAM copy, cleared on completion/abort; never log or display it. */
static char password[PROFILE_PASSWORD_MAX + 1U];
static volatile LoginState state = LOGIN_ARMED;
static uint16_t character_index;
static uint32_t session;
static uint32_t started;
static uint32_t next_report;

/* Printable US keyboard layout. Shift is explicitly released after every key. */
static bool EncodeCharacter(char character, uint8_t *usage, uint8_t *modifiers)
{
  *modifiers = 0U;
  if (character >= 'a' && character <= 'z')
  {
    *usage = 0x04U + (uint8_t)(character - 'a');
    if ((USBD_HID_GetKeyboardLeds() & CAPS_LOCK_LED) != 0U)
      *modifiers = LEFT_SHIFT_MODIFIER;
  }
  else if (character >= 'A' && character <= 'Z')
  {
    *usage = 0x04U + (uint8_t)(character - 'A');
    if ((USBD_HID_GetKeyboardLeds() & CAPS_LOCK_LED) == 0U)
      *modifiers = LEFT_SHIFT_MODIFIER;
  }
  else if (character >= '1' && character <= '9')
    *usage = 0x1EU + (uint8_t)(character - '1');
  else if (character == '0')
    *usage = 0x27U;
  else if (character == ' ')
    *usage = 0x2CU;
  else
  {
    const char unshifted[] = "-=[]\\;'`,./";
    const char shifted[] = "_+{}|:\"~<>?";
    const uint8_t punctuation[] = {0x2D,0x2E,0x2F,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
    const char shifted_digits[] = "!@#$%^&*()";
    const char *position = strchr(unshifted, character);
    if (position != NULL && character != '\0')
      *usage = punctuation[position - unshifted];
    else if ((position = strchr(shifted, character)) != NULL && character != '\0')
    {
      *usage = punctuation[position - shifted];
      *modifiers = LEFT_SHIFT_MODIFIER;
    }
    else if ((position = strchr(shifted_digits, character)) != NULL && character != '\0')
    {
      *usage = 0x1EU + (uint8_t)(position - shifted_digits);
      *modifiers = LEFT_SHIFT_MODIFIER;
    }
    else
      return false;
  }
  return true;
}

static bool IsTyping(LoginState current)
{
  return current == LOGIN_CLEAR_KEYS || current == LOGIN_PRESS_KEY || current == LOGIN_RELEASE_KEY;
}

bool Login_RequestPassword(const char *text)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  bool accepted = false;
  if ((state == LOGIN_ARMED || state == LOGIN_SENT || state == LOGIN_ABORTED)
      && UserProfile_ValidPassword(text) && UsbHid_IsConfigured())
  {
    UserProfile_ClearSecret(password, sizeof(password));
    memcpy(password, text, strlen(text));
    uint8_t usage, modifiers;
    bool valid = true;
    for (const char *character = password; *character != '\0'; ++character)
      if (!EncodeCharacter(*character, &usage, &modifiers))
        valid = false;
    if (valid)
    {
      character_index = 0U;
      session = USBD_HID_GetSession();
      started = HAL_GetTick();
      next_report = started;
      state = LOGIN_CLEAR_KEYS; /* Profile manager gates one event per transition. */
      accepted = true;
    }
    else
      state = LOGIN_INVALID_CONFIG;
  }
  __set_PRIMASK(interrupt_state);
  return accepted;
}

void Login_OnUsbUnavailable(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  if (IsTyping(state))
  {
    state = LOGIN_ABORT_RELEASE;
    UserProfile_ClearSecret(password, sizeof(password));
  }
  __set_PRIMASK(interrupt_state);
}

void Login_Cancel(void) {Login_OnUsbUnavailable();}

bool Login_Service(void)
{
  uint32_t interrupt_state = __get_PRIMASK();
  __disable_irq();
  if (!IsTyping(state) && state != LOGIN_ABORT_RELEASE)
  {
    __set_PRIMASK(interrupt_state);
    return false;
  }

  uint32_t now = HAL_GetTick();
  if (!UsbHid_IsConfigured())
  {
    state = LOGIN_ABORT_RELEASE;
    __set_PRIMASK(interrupt_state);
    return true;
  }
  if (IsTyping(state) && (session != USBD_HID_GetSession()
                         || (uint32_t)(now - started) >= LOGIN_TIMEOUT_MS))
    state = LOGIN_ABORT_RELEASE;

  const uint8_t no_keys[6] = {0};
  if (state == LOGIN_ABORT_RELEASE)
  {
    UserProfile_ClearSecret(password, sizeof(password));
    /* Release a possibly held key after USB recovers; never replay the text. */
    if (Send_Keyboard_Action(0U, no_keys))
      state = LOGIN_ABORTED;
  }
  else if ((int32_t)(now - next_report) >= 0)
  {
    if (state == LOGIN_CLEAR_KEYS)
    {
      if (Send_Keyboard_Action(0U, no_keys))
      {
        state = LOGIN_PRESS_KEY;
        next_report = now + KEY_INTERVAL_MS;
      }
    }
    else if (state == LOGIN_PRESS_KEY)
    {
      uint8_t keys[6] = {ENTER_USAGE};
      uint8_t modifiers = 0U;
      bool valid = password[character_index] == '\0'
                   || EncodeCharacter(password[character_index], &keys[0], &modifiers);
      if (!valid)
        state = LOGIN_ABORT_RELEASE;
      else if (Send_Keyboard_Action(modifiers, keys))
      {
        state = LOGIN_RELEASE_KEY;
        next_report = now + KEY_INTERVAL_MS;
      }
    }
    else if (Send_Keyboard_Action(0U, no_keys))
    {
      if (password[character_index] == '\0')
      {
        state = LOGIN_SENT;
        UserProfile_ClearSecret(password, sizeof(password));
      }
      else
      {
        ++character_index;
        state = LOGIN_PRESS_KEY;
        next_report = now + KEY_INTERVAL_MS;
      }
    }
  }
  __set_PRIMASK(interrupt_state);
  return true;
}

LoginState Login_GetState(void)
{
  return state;
}

const char *Login_GetStatusText(void)
{
  switch (state)
  {
    case LOGIN_DISABLED: return "Credential: disabled";
    case LOGIN_ARMED: return "Credential: ready";
    case LOGIN_CLEAR_KEYS:
    case LOGIN_PRESS_KEY:
    case LOGIN_RELEASE_KEY: return "Login: typing once";
    case LOGIN_SENT: return "Credential: sent";
    case LOGIN_ABORT_RELEASE:
    case LOGIN_ABORTED: return "Credential: interrupted";
    case LOGIN_INVALID_CONFIG: return "Login: invalid local config";
  }
  return "Login: error";
}
