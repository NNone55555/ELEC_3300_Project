#include "profile_storage.h"
#include <string.h>

#define RECORD_COUNT (PROFILE_STORAGE_SLOTS + PROFILE_LEGACY_SLOTS)
#define FORMAT_CURRENT 2U
#define FORMAT_LEGACY 1U

typedef struct
{
  bool header, valid, latest;
  uint8_t version, length;
  uint16_t address;
  ProfileRecordState state;
  uint32_t sequence;
  UserProfile profile;
} Record;
static Record records[RECORD_COUNT];
static StorageResult storage_status = STORAGE_IO_ERROR;
static uint32_t newest_sequence;
_Static_assert(PROFILE_STORAGE_BYTES <= AT24C02_SIZE - 8U, "Legacy counter overlap");
_Static_assert(PROFILE_RECORD_BYTES % 8U == 0U && PROFILE_STORAGE_SLOTS >= 2U, "Storage layout");

static uint16_t Crc(const uint8_t *data, unsigned length)
{
  uint16_t crc = 0xFFFF;
  while (length--)
  {
    crc ^= (uint16_t)*data++ << 8;
    for (unsigned bit = 0; bit < 8U; ++bit)
      crc = (crc & 0x8000U) ? ((uint32_t)crc << 1) ^ 0x1021U : (uint32_t)crc << 1;
  }
  return crc;
}
static uint16_t U16(const uint8_t *p)
{
  return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t U32(const uint8_t *p)
{
  return U16(p) | ((uint32_t)U16(p + 2) << 16);
}
static void Put16(uint8_t *p, uint16_t value)
{
  p[0] = value;
  p[1] = value >> 8;
}
static void Put32(uint8_t *p, uint32_t value)
{
  Put16(p, value);
  Put16(p + 2, value >> 16);
}
static bool Newer(uint32_t a, uint32_t b)
{
  return (int32_t)(a - b) > 0;
}
static bool Overlap(unsigned a, unsigned an, unsigned b, unsigned bn)
{
  return a < b + bn && b < a + an;
}
static bool RecognizedState(uint8_t state)
{
  return state == RECORD_ACTIVE || state == RECORD_CREATE_PENDING || state == RECORD_DELETE_PENDING;
}

static void Encode(uint8_t *raw, unsigned length, unsigned version, const UserProfile *p,
                   ProfileRecordState state, uint32_t sequence)
{
  memset(raw, 0, length);
  raw[0] = 'E';
  raw[1] = 'P';
  raw[2] = version;
  raw[3] = state;
  Put32(raw + 4, sequence);
  Put16(raw + 8, p->fingerprint_id);
  Put16(raw + 10, p->dpi);
  memcpy(raw + 12, p->name, strlen(p->name));
  if (version == FORMAT_CURRENT)
  {
    /* 15-byte names are zero-padded; their NUL terminators exist in RAM only. */
    memcpy(raw + 27, p->computer_username, strlen(p->computer_username));
    raw[42] = p->rgb_mode;
    raw[43] = p->red;
    raw[44] = p->green;
    raw[45] = p->blue;
    raw[46] = p->brightness;
    raw[47] = p->effect_speed;
    raw[48] = p->side1_macro;
    raw[49] = p->side2_macro;
    memcpy(raw + 50, p->password, strlen(p->password)); /* 20-byte field, NUL included. */
  }
  else
  {
    /* Used only to relocate an old record, or journal its explicit deletion.
     * Old demo tokens are retired, never reinterpreted as password characters.
     */
    memcpy(raw + 28, p->computer_username, strlen(p->computer_username));
    raw[44] = p->rgb_mode;
    raw[45] = p->red;
    raw[46] = p->green;
    raw[47] = p->blue;
    raw[48] = p->brightness;
    raw[49] = p->effect_speed;
    raw[50] = p->side1_macro;
    raw[51] = p->side2_macro;
  }
  Put16(raw + length - 2U, Crc(raw, length - 2U));
}

static void Decode(Record *r, const uint8_t *raw)
{
  if (raw[0] != 'E' || raw[1] != 'P' || raw[2] != r->version)
    return;
  r->header = true;
  if (!RecognizedState(raw[3]) || U16(raw + r->length - 2U) != Crc(raw, r->length - 2U))
    return;
  UserProfile *p = &r->profile;
  p->fingerprint_id = U16(raw + 8);
  p->dpi = U16(raw + 10);
  if (r->version == FORMAT_CURRENT)
  {
    memcpy(p->name, raw + 12, PROFILE_NAME_MAX);
    memcpy(p->computer_username, raw + 27, PROFILE_USERNAME_MAX);
    p->rgb_mode = raw[42];
    p->red = raw[43];
    p->green = raw[44];
    p->blue = raw[45];
    p->brightness = raw[46];
    p->effect_speed = raw[47];
    p->side1_macro = raw[48];
    p->side2_macro = raw[49];
    memcpy(p->password, raw + 50, sizeof(p->password));
  }
  else
  {
    memcpy(p->name, raw + 12, sizeof(p->name));
    memcpy(p->computer_username, raw + 28, sizeof(p->computer_username));
    p->rgb_mode = raw[44];
    p->red = raw[45];
    p->green = raw[46];
    p->blue = raw[47];
    p->brightness = raw[48];
    p->effect_speed = raw[49];
    p->side1_macro = raw[50];
    p->side2_macro = raw[51];
    if (raw[52] > 3U)
      return;
    /* Legacy demo ID is discarded. All other user settings are preserved. */
  }
  if (!UserProfile_Valid(p))
    return;
  r->valid = r->latest = true;
  r->state = (ProfileRecordState)raw[3];
  r->sequence = U32(raw + 4);
}
StorageResult ProfileStorage_Reload(void)
{
  uint8_t image[PROFILE_STORAGE_BYTES];
  if (!At24c02_Read(0, image, sizeof(image)))
  {
    UserProfile_ClearSecret(image, sizeof(image));
    return storage_status = STORAGE_IO_ERROR;
  }
  UserProfile_ClearSecret(records, sizeof(records));
  newest_sequence = 0;
  bool have_sequence = false;
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
  {
    Record *r = &records[i];
    r->version = i < PROFILE_STORAGE_SLOTS ? FORMAT_CURRENT : FORMAT_LEGACY;
    r->length = r->version == FORMAT_CURRENT ? PROFILE_RECORD_BYTES : PROFILE_LEGACY_RECORD_BYTES;
    r->address = (i < PROFILE_STORAGE_SLOTS ? i : i - PROFILE_STORAGE_SLOTS) * r->length;
    Decode(r, image + r->address);
    if (r->valid && (!have_sequence || Newer(r->sequence, newest_sequence)))
    {
      newest_sequence = r->sequence;
      have_sequence = true;
    }
  }
  /* A newer overlapping record proves the old bytes were reclaimed. Even if
   * their CRC happens to collide, they cannot be an independently live copy.
   */
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    for (unsigned j = 0; j < RECORD_COUNT; ++j)
      if (i != j && records[i].valid && records[j].valid &&
          Newer(records[j].sequence, records[i].sequence) &&
          Overlap(records[i].address, records[i].length, records[j].address, records[j].length))
        records[i].latest = false;
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    for (unsigned j = 0; j < RECORD_COUNT; ++j)
      if (i != j && records[i].valid && records[j].valid &&
          records[i].profile.fingerprint_id == records[j].profile.fingerprint_id &&
          (Newer(records[j].sequence, records[i].sequence) ||
           (records[j].sequence == records[i].sequence && j > i)))
        records[i].latest = false;
  bool foreign = false;
  for (unsigned byte = 0; byte < sizeof(image); ++byte)
  {
    if (image[byte] == 0U || image[byte] == 0xFFU)
      continue;
    bool owned = false;
    for (unsigned i = 0; i < RECORD_COUNT; ++i)
      if (records[i].header && byte >= records[i].address &&
          byte < records[i].address + records[i].length)
        owned = true;
    foreign |= !owned;
  }
  unsigned distinct = 0;
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    distinct += records[i].latest;
  UserProfile_ClearSecret(image, sizeof(image));
  /* Three legacy users remain readable/deletable, but new format capacity is two. */
  return storage_status =
             (foreign || distinct > PROFILE_LEGACY_SLOTS - 1U) ? STORAGE_PROTECTED : STORAGE_OK;
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
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    if (records[i].latest && records[i].state == RECORD_ACTIVE)
      ++count;
  return count;
}
bool ProfileStorage_Get(unsigned index, UserProfile *p)
{
  if (p == NULL || storage_status == STORAGE_IO_ERROR)
    return false;
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
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
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    if (records[i].latest && records[i].state != RECORD_ACTIVE)
    {
      *profile = records[i].profile;
      *state = records[i].state;
      return true;
    }
  return false;
}
static bool LatestCovers(unsigned byte)
{
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    if (records[i].latest && byte >= records[i].address &&
        byte < records[i].address + records[i].length)
      return true;
  return false;
}
static bool FreeRange(unsigned address, unsigned length)
{
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    if (records[i].latest && Overlap(address, length, records[i].address, records[i].length))
      return false;
  return true;
}
static bool TrimStaleTails(unsigned address, unsigned length)
{
  bool trim[PROFILE_STORAGE_BYTES] = {false};
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
  {
    Record *r = &records[i];
    if (!r->header || r->latest || !Overlap(address, length, r->address, 3))
      continue;
    for (unsigned b = r->address; b < r->address + r->length; ++b)
      if ((b < address || b >= address + length) && !LatestCovers(b))
        trim[b] = true;
  }
  uint8_t zero[8] = {0};
  for (unsigned b = 0; b < sizeof(trim);)
  {
    if (!trim[b])
    {
      ++b;
      continue;
    }
    unsigned start = b;
    while (b < sizeof(trim) && trim[b] && b - start < sizeof(zero))
      ++b;
    if (!At24c02_WriteVerified(start, zero, b - start))
      return false;
  }
  return true;
}
static StorageResult Persist(unsigned address, unsigned length, unsigned version,
                             const UserProfile *p, ProfileRecordState state)
{
  uint8_t raw[PROFILE_RECORD_BYTES], verify[PROFILE_RECORD_BYTES], invalid = 0;
  Encode(raw, length, version, p, state, newest_sequence + 1U);
  uint8_t commit = raw[3];
  raw[3] = 0;
  bool ok = TrimStaleTails(address, length) && At24c02_WriteVerified(address + 3, &invalid, 1) &&
            At24c02_WriteVerified(address, raw, length) && At24c02_Read(address, verify, length) &&
            memcmp(raw, verify, length) == 0 && At24c02_WriteVerified(address + 3, &commit, 1);
  raw[3] = commit;
  if (ok)
    ok = At24c02_Read(address, verify, length) && memcmp(raw, verify, length) == 0;
  UserProfile_ClearSecret(raw, sizeof(raw));
  UserProfile_ClearSecret(verify, sizeof(verify));
  return ok ? ProfileStorage_Reload() : (storage_status = STORAGE_IO_ERROR);
}
/* Relocate legacy records only into disjoint spare pages. Each relocation is
 * independently committed and reloadable, so reset cannot lose a saved user.
 * This is performed only inside an explicitly requested Save/Delete.
 */
static int FindCurrentTarget(void)
{
  for (unsigned slot = 0; slot < PROFILE_STORAGE_SLOTS; ++slot)
  {
    unsigned target = slot * PROFILE_RECORD_BYTES;
    for (unsigned move = 0; move <= PROFILE_LEGACY_SLOTS; ++move)
    {
      if (FreeRange(target, PROFILE_RECORD_BYTES))
        return (int)target;
      int blocker = -1;
      bool current_blocker = false;
      for (unsigned i = 0; i < RECORD_COUNT; ++i)
        if (records[i].latest &&
            Overlap(target, PROFILE_RECORD_BYTES, records[i].address, records[i].length))
        {
          if (records[i].version == FORMAT_CURRENT)
            current_blocker = true;
          else
            blocker = (int)i;
        }
      if (current_blocker || blocker < 0)
        break;
      int spare = -1;
      for (unsigned s = 0; s < PROFILE_LEGACY_SLOTS; ++s)
      {
        unsigned candidate = s * PROFILE_LEGACY_RECORD_BYTES;
        if (!Overlap(candidate, PROFILE_LEGACY_RECORD_BYTES, target, PROFILE_RECORD_BYTES) &&
            FreeRange(candidate, PROFILE_LEGACY_RECORD_BYTES))
        {
          spare = (int)candidate;
          break;
        }
      }
      if (spare < 0)
        break;
      UserProfile copy = records[blocker].profile;
      ProfileRecordState copy_state = records[blocker].state;
      StorageResult result =
          Persist(spare, PROFILE_LEGACY_RECORD_BYTES, FORMAT_LEGACY, &copy, copy_state);
      UserProfile_ClearSecret(&copy, sizeof(copy));
      if (result != STORAGE_OK)
        return -1;
    }
  }
  return -1;
}
StorageResult ProfileStorage_Save(const UserProfile *profile, ProfileRecordState state)
{
  if (!UserProfile_Valid(profile) || !RecognizedState(state))
    return STORAGE_INVALID;
  if (storage_status != STORAGE_OK)
    return storage_status;
  unsigned distinct = 0;
  bool existing = false;
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    if (records[i].latest)
    {
      ++distinct;
      existing |= records[i].profile.fingerprint_id == profile->fingerprint_id;
    }
  if ((!existing && distinct >= PROFILE_MAX_USERS) ||
      (distinct > PROFILE_MAX_USERS && state != RECORD_DELETE_PENDING))
    return STORAGE_FULL;
  int target = distinct <= PROFILE_MAX_USERS ? FindCurrentTarget() : -1;
  if (storage_status != STORAGE_OK)
    return storage_status;
  if (target >= 0)
    return Persist(target, PROFILE_RECORD_BYTES, FORMAT_CURRENT, profile, state);
  /* A full old-format allocation can still delete one selected user safely. */
  if (state == RECORD_DELETE_PENDING)
    for (unsigned s = 0; s < PROFILE_LEGACY_SLOTS; ++s)
      if (FreeRange(s * PROFILE_LEGACY_RECORD_BYTES, PROFILE_LEGACY_RECORD_BYTES))
        return Persist(s * PROFILE_LEGACY_RECORD_BYTES, PROFILE_LEGACY_RECORD_BYTES, FORMAT_LEGACY,
                       profile, state);
  return STORAGE_FULL;
}
StorageResult ProfileStorage_ClearPending(uint16_t id)
{
  if (storage_status != STORAGE_OK)
    return storage_status;
  int latest = -1;
  for (unsigned i = 0; i < RECORD_COUNT; ++i)
    if (records[i].latest && records[i].profile.fingerprint_id == id)
      latest = (int)i;
  if (latest < 0 || records[latest].state == RECORD_ACTIVE)
    return STORAGE_NOT_FOUND;
  uint8_t invalid = 0;
  for (unsigned pass = 0; pass < 2U; ++pass)
    for (unsigned i = 0; i < RECORD_COUNT; ++i)
      if (records[i].valid && records[i].profile.fingerprint_id == id &&
          ((pass == 1U) == (i == (unsigned)latest)))
      {
        /* Ignore a stale marker already covered by a newer live record. */
        unsigned marker = records[i].address + 3U;
        bool covered = false;
        if (i != (unsigned)latest)
          for (unsigned j = 0; j < RECORD_COUNT; ++j)
            if (records[j].latest && j != (unsigned)latest && marker >= records[j].address &&
                marker < records[j].address + records[j].length)
              covered = true;
        if (!covered && !At24c02_WriteVerified(marker, &invalid, 1))
          return storage_status = STORAGE_IO_ERROR;
      }
  return ProfileStorage_Reload();
}
const char *ProfileStorage_ErrorText(StorageResult result)
{
  static const char *const names[] = {"OK",
                                      "EEPROM I2C/write/readback error (retry storage)",
                                      "Unrecognized/corrupt allocation: write protected",
                                      "Profile storage full (maximum two users)",
                                      "Invalid profile",
                                      "Profile not found"};
  return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "Storage error";
}
