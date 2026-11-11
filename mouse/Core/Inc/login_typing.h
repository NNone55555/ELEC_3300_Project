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

/* Copies a validated 1..19-character alphanumeric password; ready USB required.
 * No caller buffer is retained. The profile
 * manager gates one request per user transition. Touch never authenticates.
 */
bool Login_RequestPassword(const char *text);
void Login_Cancel(void);

/* USB task owns the keyboard while this returns true. Sends at most one report
 * per call and yields between press/release reports. An interrupted request
 * releases keys without replaying its text.
 */
bool Login_Service(void);
void Login_OnUsbUnavailable(void);
LoginState Login_GetState(void);
const char *Login_GetStatusText(void);

#endif
