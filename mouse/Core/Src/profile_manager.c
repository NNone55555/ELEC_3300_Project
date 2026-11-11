#include "profile_manager.h"
#include "fingerprint_service.h"
#include "mouse_app.h"
#include "usb_hid_controller.h"
#include "login_typing.h"
#include "ws2812b.h"

typedef enum
{
  WORK_NONE,
  WORK_STORE_START,
  WORK_STORE_WAIT,
  WORK_DELETE_START,
  WORK_DELETE_WAIT
} Work;
typedef struct
{
  uint16_t dpi;
  WS2812B_Config rgb;
  uint8_t side1, side2;
} RuntimeSnapshot;
static RuntimeSnapshot rollback;
static bool previewing, active_valid, configuration;
static UserProfile active, operation_profile;
static volatile bool match_pending;
static volatile uint16_t matched_id;
static bool credential_pending;
static uint32_t credential_started, recovery_attempt;
static Work work;
static ProfileOperation operation;
static ProfileOperation rollback_result;

static void Apply(const UserProfile *p)
{
  (void)MouseApp_SetDpi(p->dpi);
  WS2812B_Config rgb = {(WS2812B_Mode)p->rgb_mode,
                        p->red,
                        p->green,
                        p->blue,
                        p->brightness,
                        UserProfile_EffectPeriod(p->effect_speed)};
  (void)WS2812B_SetConfig(&rgb);
  UsbHid_SetSideMacros(p->side1_macro, p->side2_macro);
}
static RuntimeSnapshot Capture(void)
{
  RuntimeSnapshot snapshot = {0};
  snapshot.dpi = MouseApp_GetDpi();
  snapshot.rgb = WS2812B_GetConfig();
  UsbHid_GetSideMacros(&snapshot.side1, &snapshot.side2);
  return snapshot;
}
static void Fallback(void)
{
  (void)MouseApp_SetDpi(800);
  (void)WS2812B_SetMode(WS2812B_OFF);
  UsbHid_SetSideMacros(MACRO_BACK, MACRO_FORWARD);
}
void ProfileManager_Init(I2C_HandleTypeDef *i2c)
{
  active_valid = previewing = configuration = credential_pending = match_pending = false;
  UserProfile_ClearSecret(&active, sizeof(active));
  UserProfile_ClearSecret(&operation_profile, sizeof(operation_profile));
  work = WORK_NONE;
  operation = PROFILE_OP_IDLE;
  recovery_attempt = HAL_GetTick();
  FpService_SetConfigurationMode(false);
  (void)ProfileStorage_Init(i2c);
  Fallback();
}
void ProfileManager_OnFingerprintMatch(uint16_t id)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  if (!configuration)
  {
    matched_id = id;
    match_pending = true;
  }
  __set_PRIMASK(irq);
}
void ProfileManager_SetConfigurationMode(bool enabled)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  configuration = enabled;
  match_pending = false;
  credential_pending = false;
  __set_PRIMASK(irq);
  FpService_SetConfigurationMode(enabled);
  if (enabled)
    Login_Cancel();
}
bool ProfileManager_IsConfigurationMode(void)
{
  return configuration;
}
bool ProfileManager_GetActive(UserProfile *p)
{
  if (!active_valid)
    return false;
  if (p != NULL)
    *p = active;
  return true;
}
bool ProfileManager_CanRegister(void)
{
  UserProfile p;
  ProfileRecordState state;
  return work == WORK_NONE && ProfileStorage_Status() == STORAGE_OK &&
         ProfileStorage_Count() < PROFILE_MAX_USERS && !ProfileStorage_Pending(&p, &state);
}
void ProfileManager_BeginPreview(const UserProfile *p)
{
  if (!previewing)
    rollback = Capture();
  previewing = true;
  Apply(p);
}
void ProfileManager_Preview(const UserProfile *p)
{
  if (previewing)
    Apply(p);
}
void ProfileManager_EndPreview(const UserProfile *saved)
{
  if (!previewing)
    return;
  if (saved != NULL && active_valid && active.fingerprint_id == saved->fingerprint_id)
  {
    active = *saved;
    Apply(saved);
  }
  else
  {
    (void)MouseApp_SetDpi(rollback.dpi);
    (void)WS2812B_SetConfig(&rollback.rgb);
    UsbHid_SetSideMacros(rollback.side1, rollback.side2);
  }
  previewing = false;
}
bool ProfileManager_Create(const UserProfile *p, bool prepared_new_template)
{
  if (work != WORK_NONE || !UserProfile_Valid(p))
    return false;
  operation_profile = *p;
  StorageResult result =
      ProfileStorage_Save(p, prepared_new_template ? RECORD_CREATE_PENDING : RECORD_ACTIVE);
  if (result == STORAGE_IO_ERROR && !prepared_new_template && ProfileStorage_Reload() == STORAGE_OK)
  {
    UserProfile readback;
    if (ProfileStorage_Find(p->fingerprint_id, &readback) && UserProfile_Same(p, &readback))
      result = STORAGE_OK;
  }
  if (result != STORAGE_OK)
  {
    operation = PROFILE_OP_STORAGE_ERROR;
    return true;
  }
  if (!prepared_new_template)
  {
    operation = PROFILE_OP_OK;
    return true;
  }
  rollback_result = PROFILE_OP_OK;
  operation = PROFILE_OP_BUSY;
  work = WORK_STORE_START;
  return true;
}
bool ProfileManager_Delete(const UserProfile *p)
{
  if (work != WORK_NONE || !UserProfile_Valid(p) || !ProfileStorage_Find(p->fingerprint_id, NULL))
    return false;
  operation_profile = *p;
  if (ProfileStorage_Save(p, RECORD_DELETE_PENDING) != STORAGE_OK)
  {
    operation = PROFILE_OP_STORAGE_ERROR;
    return true;
  }
  if (active_valid && active.fingerprint_id == p->fingerprint_id)
  {
    active_valid = credential_pending = false;
    UserProfile_ClearSecret(&active, sizeof(active));
    Login_Cancel();
    Fallback();
    if (previewing)
      rollback = Capture();
  }
  rollback_result = PROFILE_OP_OK;
  operation = PROFILE_OP_BUSY;
  work = WORK_DELETE_START;
  return true;
}
ProfileOperation ProfileManager_Operation(void)
{
  return operation;
}
bool ProfileManager_RetryRecovery(void)
{
  ProfileRecordState pending;
  if (work != WORK_NONE || ProfileStorage_Status() != STORAGE_OK ||
      !ProfileStorage_Pending(&operation_profile, &pending))
    return false;
  work = WORK_DELETE_START;
  operation = PROFILE_OP_BUSY;
  rollback_result = PROFILE_OP_OK;
  return true;
}
StorageResult ProfileManager_RetryStorage(void)
{
  StorageResult result = ProfileStorage_Reload();
  if (active_valid)
  {
    UserProfile refreshed;
    if (ProfileStorage_Find(active.fingerprint_id, &refreshed))
    {
      active = refreshed;
      if (!previewing)
        Apply(&active);
    }
    else
    {
      active_valid = false;
      UserProfile_ClearSecret(&active, sizeof(active));
      Fallback();
    }
    UserProfile_ClearSecret(&refreshed, sizeof(refreshed));
  }
  return result;
}

void ProfileManager_Update(void)
{
  uint32_t now = HAL_GetTick();
  if (work == WORK_NONE && !configuration && (uint32_t)(now - recovery_attempt) >= 2000U)
  {
    ProfileRecordState pending;
    if (ProfileStorage_Status() == STORAGE_OK &&
        ProfileStorage_Pending(&operation_profile, &pending))
    {
      /* Pending creates roll back; pending deletions finish. No template outside
       * a verified journal record is removed during startup recovery.
       */
      work = WORK_DELETE_START;
      operation = PROFILE_OP_BUSY;
      rollback_result = PROFILE_OP_OK;
    }
    recovery_attempt = now;
  }
  if (work == WORK_STORE_START)
  {
    if (FpService_Request(FP_JOB_STORE, operation_profile.fingerprint_id))
      work = WORK_STORE_WAIT;
  }
  else if (work == WORK_STORE_WAIT)
  {
    FpServiceStatus fp = FpService_GetStatus();
    if (fp.reply != FP_REPLY_BUSY)
    {
      if (fp.reply == FP_REPLY_ENROLLED)
      {
        StorageResult result = ProfileStorage_Save(&operation_profile, RECORD_ACTIVE);
        if (result == STORAGE_OK)
        {
          work = WORK_NONE;
          operation = PROFILE_OP_OK;
        }
        else
        {
          /* A commit may have reached EEPROM despite a readback error. Never
           * delete the fingerprint until a reread resolves that uncertainty.
           */
          if (ProfileStorage_Reload() == STORAGE_OK &&
              ProfileStorage_Find(operation_profile.fingerprint_id, NULL))
          {
            work = WORK_NONE;
            operation = PROFILE_OP_OK;
          }
          else if (ProfileStorage_Status() == STORAGE_OK)
          {
            rollback_result = PROFILE_OP_STORAGE_ERROR;
            work = WORK_DELETE_START;
          }
          else
          {
            work = WORK_NONE;
            operation = PROFILE_OP_STORAGE_ERROR;
          }
        }
      }
      else
      {
        rollback_result = PROFILE_OP_SENSOR_ERROR;
        work = WORK_DELETE_START;
      }
    }
  }
  else if (work == WORK_DELETE_START)
  {
    if (FpService_Request(FP_JOB_DELETE, operation_profile.fingerprint_id))
      work = WORK_DELETE_WAIT;
  }
  else if (work == WORK_DELETE_WAIT)
  {
    FpServiceStatus fp = FpService_GetStatus();
    if (fp.reply != FP_REPLY_BUSY)
    {
      if (fp.reply == FP_REPLY_DELETED)
        operation = ProfileStorage_ClearPending(operation_profile.fingerprint_id) == STORAGE_OK
                        ? rollback_result
                        : PROFILE_OP_STORAGE_ERROR;
      else
        operation = PROFILE_OP_SENSOR_ERROR;
      work = WORK_NONE;
    }
  }

  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  bool have_match = match_pending;
  uint16_t id = matched_id;
  match_pending = false;
  __set_PRIMASK(irq);
  if (!configuration && have_match)
  {
    UserProfile found;
    if (ProfileStorage_Find(id, &found) && (!active_valid || active.fingerprint_id != id))
    {
      Login_Cancel();
      active = found;
      active_valid = true;
      Apply(&active);
      credential_pending = active.password[0] != '\0';
      credential_started = now;
    }
    UserProfile_ClearSecret(&found, sizeof(found));
  }
  if (!configuration && credential_pending)
  {
    if (Login_RequestPassword(active.password) || (uint32_t)(now - credential_started) > 3000U)
      credential_pending = false;
  }
  if (work == WORK_NONE)
    UserProfile_ClearSecret(&operation_profile, sizeof(operation_profile));
}
