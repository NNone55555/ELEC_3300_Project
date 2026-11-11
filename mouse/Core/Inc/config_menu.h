#ifndef CONFIG_MENU_H
#define CONFIG_MENU_H
#include <stdbool.h>
void ConfigMenu_Init(void);
void ConfigMenu_Poll(void);
bool ConfigMenu_Active(void);
bool ConfigMenu_HasInput(void);
#endif
