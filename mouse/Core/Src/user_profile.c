#include "user_profile.h"
#include <string.h>

void UserProfile_ClearSecret(void *memory, uint32_t size)
{
  volatile uint8_t *bytes = memory;
  while (size-- != 0U)
    *bytes++ = 0U;
}

bool UserProfile_ValidText(const char *text, uint32_t maximum, bool allow_empty)
{
  if (text == NULL)
    return false;
  bool nonspace = false;
  for (uint32_t i = 0; i <= maximum; ++i)
  {
    uint8_t ch = (uint8_t)text[i];
    if (ch == 0U)
      return i == 0U ? allow_empty : nonspace;
    if (ch < 32U || ch > 126U)
      return false;
    nonspace |= ch != ' ';
  }
  return false;
}

bool UserProfile_ValidDpi(uint32_t dpi)
{
  return dpi >= 100U && dpi <= 12000U && dpi % 100U == 0U;
}
bool UserProfile_ValidPassword(const char *text)
{
  if (text == NULL)
    return false;
  for (unsigned i = 0; i <= PROFILE_PASSWORD_MAX; ++i)
  {
    unsigned char ch = (unsigned char)text[i];
    if (ch == '\0')
      return i > 0U;
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')))
      return false;
  }
  return false;
}

void UserProfile_Default(UserProfile *profile, const char *name, uint16_t id)
{
  memset(profile, 0, sizeof(*profile));
  profile->fingerprint_id = id;
  profile->dpi = 1600;
  strncpy(profile->name, name, PROFILE_NAME_MAX);
  strncpy(profile->computer_username, name, PROFILE_USERNAME_MAX);
  profile->rgb_mode = 1;
  profile->blue = 255;
  profile->brightness = 50;
  profile->effect_speed = 3;
  profile->side1_macro = MACRO_BACK;
  profile->side2_macro = MACRO_FORWARD;
}

bool UserProfile_Valid(const UserProfile *p)
{
  return p != NULL && p->fingerprint_id != UINT16_MAX && UserProfile_ValidDpi(p->dpi) &&
         UserProfile_ValidText(p->name, PROFILE_NAME_MAX, false) &&
         UserProfile_ValidText(p->computer_username, PROFILE_USERNAME_MAX, true) &&
         p->rgb_mode <= 3U && p->brightness <= 100U && p->effect_speed >= 1U &&
         p->effect_speed <= 5U && p->side1_macro < MACRO_COUNT && p->side2_macro < MACRO_COUNT &&
         (p->password[0] == '\0' || UserProfile_ValidPassword(p->password));
}
bool UserProfile_Same(const UserProfile *a, const UserProfile *b)
{
  return a->fingerprint_id == b->fingerprint_id && a->dpi == b->dpi &&
         strcmp(a->name, b->name) == 0 && strcmp(a->computer_username, b->computer_username) == 0 &&
         a->rgb_mode == b->rgb_mode && a->red == b->red && a->green == b->green &&
         a->blue == b->blue && a->brightness == b->brightness &&
         a->effect_speed == b->effect_speed && a->side1_macro == b->side1_macro &&
         a->side2_macro == b->side2_macro && strcmp(a->password, b->password) == 0;
}

uint32_t UserProfile_EffectPeriod(uint8_t speed)
{
  static const uint32_t periods[] = {8000, 5000, 3000, 1600, 800};
  return speed >= 1U && speed <= 5U ? periods[speed - 1U] : 3000U;
}
const char *UserProfile_MacroName(uint8_t macro)
{
  static const char *const names[] = {"Back",   "Forward", "Ctrl+C",  "Ctrl+V",
                                      "Ctrl+Z", "Ctrl+A",  "Disabled"};
  return macro < MACRO_COUNT ? names[macro] : "Invalid";
}
const char *UserProfile_ModeName(uint8_t mode)
{
  static const char *const names[] = {"Off", "Static", "Breathing", "Rainbow"};
  return mode < 4U ? names[mode] : "Invalid";
}
const char *UserProfile_ColorName(const UserProfile *p)
{
  const uint8_t colors[][3] = {{255, 0, 0},   {0, 255, 0},   {0, 0, 255},  {255, 255, 255},
                               {255, 0, 255}, {0, 255, 255}, {255, 255, 0}};
  static const char *const names[] = {"Red", "Green", "Blue", "White", "Purple", "Cyan", "Yellow"};
  for (unsigned i = 0; i < 7U; ++i)
    if (p->red == colors[i][0] && p->green == colors[i][1] && p->blue == colors[i][2])
      return names[i];
  return "Custom";
}
