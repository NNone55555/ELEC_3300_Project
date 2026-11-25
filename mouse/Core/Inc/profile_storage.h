#ifndef PROFILE_STORAGE_H
#define PROFILE_STORAGE_H
#include "user_profile.h"
#include "at24c02.h"

/* Three encrypted 72-byte records (two users and one copy-on-write spare).
 * Bytes 216..223 journal the one-time plaintext migration. Preserve 224..255.
 */
#define PROFILE_RECORD_BYTES 72U
#define PROFILE_STORAGE_BYTES 224U
#define PROFILE_STORAGE_SLOTS (PROFILE_STORAGE_BYTES / PROFILE_RECORD_BYTES)
#define PROFILE_MAX_USERS (PROFILE_STORAGE_SLOTS - 1U)

typedef enum
{
  RECORD_ACTIVE = 0xA5,
  RECORD_CREATE_PENDING = 0xC3,
  RECORD_DELETE_PENDING = 0xD4
} ProfileRecordState;
typedef enum
{
  STORAGE_OK,
  STORAGE_IO_ERROR,
  STORAGE_PROTECTED,
  STORAGE_FULL,
  STORAGE_INVALID,
  STORAGE_NOT_FOUND,
  STORAGE_AUTH_ERROR,
  STORAGE_NONCE_ERROR
} StorageResult;

StorageResult ProfileStorage_Init(I2C_HandleTypeDef *i2c);
StorageResult ProfileStorage_Reload(void);
unsigned ProfileStorage_Count(void);
bool ProfileStorage_Get(unsigned index, UserProfile *profile);
bool ProfileStorage_Find(uint16_t fingerprint_id, UserProfile *profile);
bool ProfileStorage_Pending(UserProfile *profile, ProfileRecordState *state);
StorageResult ProfileStorage_Save(const UserProfile *profile, ProfileRecordState state);
/* Pending record stays valid until every older copy is invalidated/verified. */
StorageResult ProfileStorage_ClearPending(uint16_t fingerprint_id);
StorageResult ProfileStorage_Status(void);
const char *ProfileStorage_ErrorText(StorageResult result);
#endif
