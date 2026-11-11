#ifndef PROFILE_MANAGER_H
#define PROFILE_MANAGER_H
#include "profile_storage.h"

typedef enum
{
  PROFILE_OP_IDLE,
  PROFILE_OP_BUSY,
  PROFILE_OP_OK,
  PROFILE_OP_STORAGE_ERROR,
  PROFILE_OP_SENSOR_ERROR
} ProfileOperation;
void ProfileManager_Init(I2C_HandleTypeDef *i2c);
void ProfileManager_Update(void); /* Existing display/config task: sole EEPROM owner. */
void ProfileManager_OnFingerprintMatch(uint16_t id); /* Fingerprint task, nonblocking. */
void ProfileManager_SetConfigurationMode(bool enabled);
bool ProfileManager_IsConfigurationMode(void);
bool ProfileManager_GetActive(UserProfile *profile);
bool ProfileManager_CanRegister(void);
void ProfileManager_BeginPreview(const UserProfile *profile);
void ProfileManager_Preview(const UserProfile *profile);
void ProfileManager_EndPreview(const UserProfile *saved_profile);
bool ProfileManager_Create(const UserProfile *profile, bool prepared_new_template);
bool ProfileManager_Delete(const UserProfile *profile);
ProfileOperation ProfileManager_Operation(void);
StorageResult ProfileManager_RetryStorage(void);
bool ProfileManager_RetryRecovery(void);
#endif
