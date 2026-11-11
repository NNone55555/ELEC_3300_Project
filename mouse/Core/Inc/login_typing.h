#ifndef LOGIN_TYPING_H
#define LOGIN_TYPING_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  LOGIN_DISABLED,
  LOGIN_ARMED,
  LOGIN_CLEAR_KEYS,
  LOGIN_PRESS_KEY,
  LOGIN_RELEASE_KEY,
  LOGIN_SENT,
  LOGIN_ABORT_RELEASE,
  LOGIN_ABORTED,
  LOGIN_INVALID_CONFIG
} LoginState;

/* Only call for a fresh successful UART fingerprint match, never a touch edge.
 * Accepted once per MCU boot, only for the configured template and ready USB.
 */
bool Login_OnFingerprintMatch(uint16_t template_id);

/* USB task owns the keyboard while this returns true. Sends at most one report
 * per call, yields between press/release reports, and never repeats a password.
 */
bool Login_Service(void);
void Login_OnUsbUnavailable(void);
LoginState Login_GetState(void);
const char *Login_GetStatusText(void);

#endif
