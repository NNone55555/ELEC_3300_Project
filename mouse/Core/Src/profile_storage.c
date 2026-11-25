#include "profile_storage.h"
#include "aes_ccm.h"
#include "boot_epoch.h"
#include "profile_key.h"
#include <string.h>

#define FORMAT_CURRENT 3U
#define PASSWORD_OFFSET 32U
#define NONCE_OFFSET 52U
#define TAG_OFFSET 64U
#define SLOTS_BYTES (PROFILE_STORAGE_SLOTS * PROFILE_RECORD_BYTES)

typedef struct
{
  bool latest;
  ProfileRecordState state;
  uint32_t sequence;
  UserProfile profile;
} Record;

static Record records[PROFILE_STORAGE_SLOTS];
static StorageResult storage_status = STORAGE_IO_ERROR;
static uint32_t newest_sequence, nonce_counter;
static const uint8_t migration_marker[8] = {'E','P','3','W','I','P','E','!'};
static const uint8_t format_marker[8] = {'E','P','3','W','I','P','E','.'};

_Static_assert(PROFILE_STORAGE_BYTES == SLOTS_BYTES + sizeof(migration_marker), "Migration marker space");
_Static_assert(PROFILE_STORAGE_BYTES <= AT24C02_SIZE - 32U, "Reserved EEPROM range");
_Static_assert(PROFILE_RECORD_BYTES == TAG_OFFSET + AES_CCM_TAG_BYTES, "Record layout");
_Static_assert(NONCE_OFFSET == PASSWORD_OFFSET + PROFILE_PASSWORD_MAX + 1U, "Password layout");

static uint16_t U16(const uint8_t *bytes)
{
  return bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t U32(const uint8_t *bytes)
{
  return U16(bytes) | ((uint32_t)U16(bytes + 2) << 16);
}

static void Put16(uint8_t *bytes, uint16_t value)
{
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

static void Put32(uint8_t *bytes, uint32_t value)
{
  Put16(bytes, (uint16_t)value);
  Put16(bytes + 2, (uint16_t)(value >> 16));
}

static bool Newer(uint32_t a, uint32_t b)
{
  uint32_t difference = a - b;
  return difference != 0U && difference < 0x80000000U;
}

static bool RecognizedState(uint8_t state)
{
  return state == RECORD_ACTIVE || state == RECORD_CREATE_PENDING || state == RECORD_DELETE_PENDING;
}

static bool Header(const uint8_t *raw, uint8_t version)
{
  return raw[0] == 'E' && raw[1] == 'P' && raw[2] == version;
}

static bool LegacyPresent(const uint8_t image[PROFILE_STORAGE_BYTES])
{
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
    if (Header(image + i * PROFILE_RECORD_BYTES, 2U))
      return true;
  for (unsigned offset = 0; offset < PROFILE_STORAGE_BYTES; offset += 56U)
    if (Header(image + offset, 1U))
      return true;
  return false;
}

static bool WipeLegacy(void)
{
  static const uint8_t zero[SLOTS_BYTES] = {0};
  /* Publish the initialized format only after the plaintext slots are cleared.
   * It prevents a damaged current slot from being mistaken for a legacy slot. */
  return At24c02_WriteVerified(SLOTS_BYTES, migration_marker, sizeof(migration_marker))
         && At24c02_WriteVerified(0, zero, sizeof(zero))
         && At24c02_WriteVerified(SLOTS_BYTES, format_marker, sizeof(format_marker));
}

static bool Encode(uint8_t raw[PROFILE_RECORD_BYTES], const UserProfile *p,
                   ProfileRecordState state, uint32_t sequence)
{
  uint64_t boot = BootEpoch_Value();
  if (boot == 0U || nonce_counter == UINT32_MAX)
    return false;
  memset(raw, 0, PROFILE_RECORD_BYTES);
  raw[0] = 'E';
  raw[1] = 'P';
  raw[2] = FORMAT_CURRENT;
  raw[3] = state;
  Put32(raw + 4, sequence);
  raw[8] = (uint8_t)p->fingerprint_id;
  Put16(raw + 9, p->dpi);
  memcpy(raw + 11, p->computer_username, strlen(p->computer_username));
  raw[26] = p->rgb_mode | (p->effect_speed << 4);
  raw[27] = p->red;
  raw[28] = p->green;
  raw[29] = p->blue;
  raw[30] = p->brightness;
  raw[31] = p->side1_macro | (p->side2_macro << 4);
  memcpy(raw + PASSWORD_OFFSET, p->password, strlen(p->password));
  Put32(raw + NONCE_OFFSET, (uint32_t)boot);
  Put32(raw + NONCE_OFFSET + 4, (uint32_t)(boot >> 32));
  Put32(raw + NONCE_OFFSET + 8, ++nonce_counter);
  return AesCcm_Seal(PROFILE_KEY, raw + NONCE_OFFSET, raw, PASSWORD_OFFSET,
                     raw + PASSWORD_OFFSET, NONCE_OFFSET - PASSWORD_OFFSET, raw + TAG_OFFSET);
}

static bool Decode(Record *record, const uint8_t *raw)
{
  UserProfile *p = &record->profile;
  if (!RecognizedState(raw[3]))
    return false;
  memcpy(p->password, raw + PASSWORD_OFFSET, sizeof(p->password));
  if (!AesCcm_Open(PROFILE_KEY, raw + NONCE_OFFSET, raw, PASSWORD_OFFSET,
                   (uint8_t *)p->password, sizeof(p->password), raw + TAG_OFFSET))
    return false;

  p->fingerprint_id = raw[8];
  p->dpi = U16(raw + 9);
  memcpy(p->computer_username, raw + 11, PROFILE_USERNAME_MAX);
  p->rgb_mode = raw[26] & 0x0FU;
  p->effect_speed = raw[26] >> 4;
  p->red = raw[27];
  p->green = raw[28];
  p->blue = raw[29];
  p->brightness = raw[30];
  p->side1_macro = raw[31] & 0x0FU;
  p->side2_macro = raw[31] >> 4;
  if (p->password[PROFILE_PASSWORD_MAX] != 0 || !UserProfile_Valid(p))
  {
    Crypto_Clear(p, sizeof(*p));
    return false;
  }
  record->state = (ProfileRecordState)raw[3];
  record->sequence = U32(raw + 4);
  record->latest = true;
  return true;
}

StorageResult ProfileStorage_Reload(void)
{
  uint8_t image[PROFILE_STORAGE_BYTES];
  Crypto_Clear(records, sizeof(records));
  newest_sequence = 0;
  if (!At24c02_Read(0, image, sizeof(image)))
  {
    Crypto_Clear(image, sizeof(image));
    return storage_status = STORAGE_IO_ERROR;
  }

  bool current = false;
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
    current |= Header(image + i * PROFILE_RECORD_BYTES, FORMAT_CURRENT);
  bool migrating = memcmp(image + SLOTS_BYTES, migration_marker, sizeof(migration_marker)) == 0;
  bool initialized = memcmp(image + SLOTS_BYTES, format_marker, sizeof(format_marker)) == 0;
  if (migrating && current)
  {
    Crypto_Clear(image, sizeof(image));
    return storage_status = STORAGE_PROTECTED;
  }
  if (!initialized && !current && (migrating || LegacyPresent(image)))
  {
    if (BootEpoch_Value() == 0U)
    {
      Crypto_Clear(image, sizeof(image));
      return storage_status = STORAGE_NONCE_ERROR;
    }
    bool wiped = WipeLegacy();
    Crypto_Clear(image, sizeof(image));
    return storage_status = wiped ? STORAGE_OK : STORAGE_IO_ERROR;
  }

  bool corrupt = false, foreign = false, have_sequence = false;
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
  {
    const uint8_t *raw = image + i * PROFILE_RECORD_BYTES;
    Record *record = &records[i];
    if (raw[3] == 0U) /* Incomplete write or deleted record. */
      continue;
    if (!Header(raw, FORMAT_CURRENT))
    {
      for (unsigned j = 0; j < PROFILE_RECORD_BYTES; ++j)
        foreign |= raw[j] != 0U && raw[j] != 0xFFU;
      continue;
    }
    if (!Decode(record, raw))
    {
      corrupt = true;
      continue;
    }
    if (!have_sequence || Newer(record->sequence, newest_sequence))
    {
      newest_sequence = record->sequence;
      have_sequence = true;
    }
  }

  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
    if (records[i].latest)
      for (unsigned j = i + 1U; j < PROFILE_STORAGE_SLOTS; ++j)
      {
        if (!records[j].latest || records[i].profile.fingerprint_id != records[j].profile.fingerprint_id)
          continue;
        unsigned stale = Newer(records[i].sequence, records[j].sequence) ? j : i;
        records[stale].latest = false;
        if (stale == i)
          break;
      }
  Crypto_Clear(image, sizeof(image));
  if (corrupt)
    return storage_status = STORAGE_AUTH_ERROR;
  if (foreign)
    return storage_status = STORAGE_PROTECTED;
  if (BootEpoch_Value() == 0U || nonce_counter == UINT32_MAX)
    return storage_status = STORAGE_NONCE_ERROR;
  if (!initialized && !At24c02_WriteVerified(SLOTS_BYTES, format_marker, sizeof(format_marker)))
    return storage_status = STORAGE_IO_ERROR;
  return storage_status = STORAGE_OK;
}

StorageResult ProfileStorage_Init(I2C_HandleTypeDef *i2c)
{
  At24c02_Init(i2c);
  return ProfileStorage_Reload();
}
StorageResult ProfileStorage_Status(void)
{
  return storage_status;
}
unsigned ProfileStorage_Count(void)
{
  unsigned count = 0;
  if (storage_status == STORAGE_IO_ERROR)
    return 0;
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
    if (records[i].latest && records[i].state == RECORD_ACTIVE)
      ++count;
  return count;
}
bool ProfileStorage_Get(unsigned index, UserProfile *p)
{
  if (p == NULL || storage_status == STORAGE_IO_ERROR)
    return false;
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
    if (records[i].latest && records[i].state == RECORD_ACTIVE)
    {
      if (index-- == 0U)
      {
        *p = records[i].profile;
        return true;
      }
    }
  return false;
}
bool ProfileStorage_Find(uint16_t id, UserProfile *profile)
{
  UserProfile found;
  for (unsigned i = 0; ProfileStorage_Get(i, &found); ++i)
    if (found.fingerprint_id == id)
    {
      if (profile != NULL)
        *profile = found;
      UserProfile_ClearSecret(&found, sizeof(found));
      return true;
    }
  UserProfile_ClearSecret(&found, sizeof(found));
  return false;
}
bool ProfileStorage_Pending(UserProfile *profile, ProfileRecordState *state)
{
  if (profile == NULL || state == NULL || storage_status == STORAGE_IO_ERROR)
    return false;
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
    if (records[i].latest && records[i].state != RECORD_ACTIVE)
    {
      *profile = records[i].profile;
      *state = records[i].state;
      return true;
    }
  return false;
}
static StorageResult Persist(unsigned slot, const UserProfile *profile, ProfileRecordState state)
{
  uint8_t raw[PROFILE_RECORD_BYTES], verify[PROFILE_RECORD_BYTES], invalid = 0;
  if (!Encode(raw, profile, state, newest_sequence + 1U))
  {
    Crypto_Clear(raw, sizeof(raw));
    return storage_status = STORAGE_NONCE_ERROR;
  }

  unsigned address = slot * PROFILE_RECORD_BYTES;
  raw[3] = 0U;
  bool ok = At24c02_WriteVerified(address + 3U, &invalid, 1U)
            && At24c02_WriteVerified(address, raw, sizeof(raw))
            && At24c02_Read(address, verify, sizeof(verify))
            && memcmp(raw, verify, sizeof(raw)) == 0;
  raw[3] = state; /* The authenticated state is committed last. */
  ok = ok && At24c02_WriteVerified(address + 3U, raw + 3, 1U);
  StorageResult result = ok ? ProfileStorage_Reload() : (storage_status = STORAGE_IO_ERROR);
  if (result == STORAGE_OK && (!records[slot].latest || records[slot].state != state))
    result = storage_status = STORAGE_AUTH_ERROR;
  Crypto_Clear(raw, sizeof(raw));
  Crypto_Clear(verify, sizeof(verify));
  return result;
}

StorageResult ProfileStorage_Save(const UserProfile *profile, ProfileRecordState state)
{
  if (!UserProfile_Valid(profile) || !RecognizedState(state))
    return STORAGE_INVALID;
  if (storage_status != STORAGE_OK)
    return storage_status;
  unsigned distinct = 0;
  bool existing = false;
  unsigned spare = PROFILE_STORAGE_SLOTS;
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
  {
    if (records[i].latest)
    {
      ++distinct;
      existing |= records[i].profile.fingerprint_id == profile->fingerprint_id;
    }
    else if (spare == PROFILE_STORAGE_SLOTS)
      spare = i;
  }
  if ((!existing && distinct >= PROFILE_MAX_USERS) || spare == PROFILE_STORAGE_SLOTS)
    return STORAGE_FULL;
  return Persist(spare, profile, state);
}

StorageResult ProfileStorage_ClearPending(uint16_t id)
{
  if (storage_status != STORAGE_OK)
    return storage_status;
  unsigned latest = PROFILE_STORAGE_SLOTS;
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
    if (records[i].latest && records[i].profile.fingerprint_id == id && records[i].state != RECORD_ACTIVE)
      latest = i;
  if (latest == PROFILE_STORAGE_SLOTS)
    return STORAGE_NOT_FOUND;

  /* Re-read and authenticate older records before invalidating them, then
   * invalidate the pending record last so reset cannot resurrect a user. */
  uint8_t raw[PROFILE_RECORD_BYTES], invalid = 0U;
  for (unsigned i = 0; i < PROFILE_STORAGE_SLOTS; ++i)
  {
    if (i == latest)
      continue;
    unsigned address = i * PROFILE_RECORD_BYTES;
    if (!At24c02_Read(address, raw, sizeof(raw)))
      return storage_status = STORAGE_IO_ERROR;
    Record saved = {0};
    bool match = Header(raw, FORMAT_CURRENT) && Decode(&saved, raw) && saved.profile.fingerprint_id == id;
    Crypto_Clear(&saved, sizeof(saved));
    if (match && !At24c02_WriteVerified(address + 3U, &invalid, 1U))
      return storage_status = STORAGE_IO_ERROR;
  }
  if (!At24c02_WriteVerified(latest * PROFILE_RECORD_BYTES + 3U, &invalid, 1U))
    return storage_status = STORAGE_IO_ERROR;
  return ProfileStorage_Reload();
}

const char *ProfileStorage_ErrorText(StorageResult result)
{
  static const char *const names[] = {
    "OK", "EEPROM I2C/write/readback error (retry storage)",
    "Unrecognized allocation: write protected", "Profile storage full (maximum two users)",
    "Invalid profile", "Profile not found", "Profile authentication failed",
    "Flash nonce journal unavailable"
  };
  return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "Unknown storage error";
}
