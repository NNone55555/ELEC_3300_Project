#ifndef USER_PROFILE_H
#define USER_PROFILE_H
#include <stdbool.h>
#include <stdint.h>

#define PROFILE_NAME_MAX 15U
#define PROFILE_USERNAME_MAX 15U
#define PROFILE_PASSWORD_MAX 19U
#define PROFILE_PASSWORD_INPUT_MAX PROFILE_PASSWORD_MAX

typedef enum
{
  MACRO_BACK,
  MACRO_FORWARD,
  MACRO_COPY,
  MACRO_PASTE,
  MACRO_UNDO,
  MACRO_SELECT_ALL,
  MACRO_DISABLED,
  MACRO_COUNT
} ProfileMacro;

typedef struct
{
  uint16_t fingerprint_id;
  uint16_t dpi;
  char name[PROFILE_NAME_MAX + 1U];
  char computer_username[PROFILE_USERNAME_MAX + 1U];
  uint8_t rgb_mode; /* Same 0..3 OFF/STATIC/BREATHING/RAINBOW order as WS2812B. */
  uint8_t red, green, blue, brightness, effect_speed; /* Speed 1..5. */
  uint8_t side1_macro, side2_macro;
  char password[PROFILE_PASSWORD_MAX + 1U]; /* Unencrypted prototype storage; never log. */
} UserProfile;

void UserProfile_Default(UserProfile *profile, const char *name, uint16_t fingerprint_id);
bool UserProfile_Valid(const UserProfile *profile);
bool UserProfile_Same(const UserProfile *a, const UserProfile *b);
bool UserProfile_ValidText(const char *text, uint32_t maximum, bool allow_empty);
bool UserProfile_ValidDpi(uint32_t dpi);
bool UserProfile_ValidPassword(const char *text);
uint32_t UserProfile_EffectPeriod(uint8_t speed);
const char *UserProfile_MacroName(uint8_t macro);
const char *UserProfile_ModeName(uint8_t mode);
const char *UserProfile_ColorName(const UserProfile *profile);
void UserProfile_ClearSecret(void *memory, uint32_t size);
#endif
