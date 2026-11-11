#include "fingerprint_service.h"

static volatile bool configuration_mode, request_waiting, cancel_requested;
static FpServiceStatus status;
static FpJob active_job;
static uint16_t target_id, prepared_id;
static bool prepared;
static uint32_t started;

void FpService_SetConfigurationMode(bool enabled)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  configuration_mode = enabled;
  __set_PRIMASK(irq);
}
bool FpService_Request(FpJob job, uint16_t id)
{
  if (job == FP_JOB_NONE)
    return false;
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  if (cancel_requested || request_waiting || active_job != FP_JOB_NONE ||
      status.reply == FP_REPLY_BUSY)
  {
    __set_PRIMASK(irq);
    return false;
  }
  status.job = job;
  status.id = id;
  status.error = 0;
  status.reply = FP_REPLY_BUSY;
  request_waiting = true;
  __set_PRIMASK(irq);
  return true;
}
void FpService_Cancel(void)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  cancel_requested = true;
  status.reply = FP_REPLY_BUSY;
  __set_PRIMASK(irq);
}
FpServiceStatus FpService_GetStatus(void)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  FpServiceStatus copy = status;
  __set_PRIMASK(irq);
  return copy;
}
static void Complete(FpReply reply, uint16_t id, int16_t error)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  status.reply = reply;
  status.id = id;
  status.error = error;
  active_job = FP_JOB_NONE;
  request_waiting = false;
  __set_PRIMASK(irq);
}

bool FpService_Step(FingerprintController *c, bool button, uint16_t *normal_id)
{
  uint32_t irq = __get_PRIMASK();
  __disable_irq();
  bool cancel = cancel_requested;
  cancel_requested = false;
  bool waiting = request_waiting, config = configuration_mode;
  __set_PRIMASK(irq);
  if (cancel)
  {
    prepared = false;
    c->reject_duplicates = c->defer_store = false;
    c->state = c->storage.capacity != 0U ? FP_WAIT_REMOVAL : FP_CONNECTING;
    c->result = FP_RESULT_CANCELLED;
    Complete(FP_REPLY_CANCELLED, 0, 0);
    return false;
  }
  if (waiting && active_job == FP_JOB_NONE)
  {
    if (c->state == FP_ERROR)
      c->state = FP_CONNECTING;
    if (c->state == FP_CONNECTING)
    {
      Fingerprint_Step(c, false);
      if (c->state == FP_ERROR)
        Complete(FP_REPLY_ERROR, 0, c->error);
      return false;
    }
    irq = __get_PRIMASK();
    __disable_irq();
    active_job = status.job;
    target_id = status.id;
    request_waiting = false;
    __set_PRIMASK(irq);
    started = HAL_GetTick();
    c->result = FP_RESULT_NONE;
    if (active_job == FP_JOB_VERIFY)
      c->state = FP_READY;
    else if (active_job == FP_JOB_ENROLL)
    {
      prepared = false;
      c->reject_duplicates = c->defer_store = true;
      c->state = FP_FIND_FREE_SLOT;
    }
  }

  if (active_job == FP_JOB_STORE)
  {
    int16_t result =
        prepared && prepared_id == target_id ? Zw3020_Store(target_id) : ZW3020_BAD_PACKET;
    if (result == ZW3020_OK)
      ++c->storage.count;
    prepared = false;
    c->state = FP_WAIT_REMOVAL;
    Complete(result == ZW3020_OK ? FP_REPLY_ENROLLED : FP_REPLY_ERROR, target_id, result);
    return false;
  }
  if (active_job == FP_JOB_DELETE)
  {
    bool occupied = true;
    int16_t result = Zw3020_IsOccupied(target_id, &occupied);
    bool was_occupied = occupied;
    if (result == ZW3020_OK && occupied)
    {
      result = Zw3020_Delete(target_id);
      int16_t check = Zw3020_IsOccupied(target_id, &occupied);
      if (check == ZW3020_OK && !occupied)
        result = ZW3020_OK;
      else if (check != ZW3020_OK)
        result = check;
      else if (result == ZW3020_OK)
        result = ZW3020_BAD_PACKET;
    }
    if (result == ZW3020_OK && !occupied && was_occupied && c->storage.count != 0U)
      --c->storage.count;
    c->state = FP_WAIT_REMOVAL;
    Complete(result == ZW3020_OK && !occupied ? FP_REPLY_DELETED : FP_REPLY_ERROR, target_id,
             result);
    return false;
  }

  if (active_job != FP_JOB_NONE && (uint32_t)(HAL_GetTick() - started) >= 30000U)
  {
    c->state = FP_WAIT_REMOVAL;
    prepared = false;
    Complete(FP_REPLY_ERROR, 0, ZW3020_TIMEOUT);
    return false;
  }

  bool run = active_job != FP_JOB_NONE || !config || c->state == FP_CONNECTING;
  FingerprintState before = c->state;
  if (run)
    Fingerprint_Step(c, active_job == FP_JOB_NONE && !config && button && c->state == FP_ERROR);
  irq = __get_PRIMASK();
  __disable_irq();
  status.progress = c->state;
  __set_PRIMASK(irq);
  if (active_job == FP_JOB_VERIFY)
  {
    if (before == FP_SEARCH_DATABASE && c->result == FP_RESULT_MATCH)
      Complete(c->user.id == target_id ? FP_REPLY_VERIFIED : FP_REPLY_MISMATCH, c->user.id, 0);
    else if (before == FP_SEARCH_DATABASE && c->result == FP_RESULT_NO_MATCH)
      Complete(FP_REPLY_MISMATCH, UINT16_MAX, 0);
    else if (c->result == FP_RESULT_ERROR)
      Complete(FP_REPLY_ERROR, 0, c->error);
    return false;
  }
  if (active_job == FP_JOB_ENROLL)
  {
    if (c->result == FP_RESULT_PREPARED)
    {
      prepared = true;
      prepared_id = c->enrollment_id;
      Complete(FP_REPLY_PREPARED, prepared_id, 0);
    }
    else if (c->result == FP_RESULT_DUPLICATE)
      Complete(FP_REPLY_DUPLICATE, c->user.id, 0);
    else if (c->result == FP_RESULT_ERROR || c->result == FP_RESULT_TIMEOUT ||
             c->result == FP_RESULT_FULL)
      Complete(FP_REPLY_ERROR, 0, c->error != 0 ? c->error : ZW3020_FULL);
    return false;
  }
  /* Recheck mode after the UART wait: a menu may have started during it. */
  if (!configuration_mode && before == FP_SEARCH_DATABASE && c->result == FP_RESULT_MATCH)
  {
    *normal_id = c->user.id;
    return true;
  }
  return false;
}
