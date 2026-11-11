#ifndef EEPROM_TEST_H
#define EEPROM_TEST_H

#include "stm32f1xx_hal.h"

/* Call only from the default task; LCD and blocking I2C stay out of ISRs. */
void EEPROM_Test_Init(I2C_HandleTypeDef *i2c);
void EEPROM_Test_Poll(void);

#endif
