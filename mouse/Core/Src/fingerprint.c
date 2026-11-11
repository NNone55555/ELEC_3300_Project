#include "fingerprint.h"
#include <string.h>

#define ENROLL_STAGE_TIMEOUT_MS 15000U

static void EnterState(FingerprintController *controller, FingerprintState state)
{
  controller->state = state;
  controller->state_started = HAL_GetTick();
}

static bool EnrollmentInProgress(FingerprintState state)
{
  return (state >= FP_FIND_FREE_SLOT && state <= FP_SAVE_TEMPLATE)
         || state == FP_CHECK_DUPLICATE || state == FP_HOLD_TEMPLATE;
}

static void CommandFailed(FingerprintController *controller, int16_t error)
{
  controller->error = error;
  controller->error_command = Zw3020_LastCommand();
  controller->result = FP_RESULT_ERROR;
  if (error < 0)
  {
    EnterState(controller, FP_ERROR);
  }
  else
  {
    /* A rejected scan needs a new finger placement, not a UART reconnect. */
    EnterState(controller, FP_WAIT_REMOVAL);
  }
}

void Fingerprint_Init(FingerprintController *controller)
{
  memset(controller, 0, sizeof(*controller));
  EnterState(controller, FP_CONNECTING);
}

static void HandleControl(FingerprintController *controller)
{
  controller->error = ZW3020_OK;
  if (EnrollmentInProgress(controller->state))
  {
    controller->result = FP_RESULT_CANCELLED;
    EnterState(controller, FP_WAIT_REMOVAL);
  }
  else if (controller->state == FP_ERROR)
  {
    controller->result = FP_RESULT_NONE;
    EnterState(controller, FP_CONNECTING);
  }
  else if (controller->state != FP_CONNECTING)
  {
    controller->result = FP_RESULT_NONE;
    EnterState(controller, FP_FIND_FREE_SLOT);
  }
}

void Fingerprint_Step(FingerprintController *controller, bool control_pressed)
{
  if (control_pressed)
  {
    HandleControl(controller);
    return;
  }

  bool waiting_for_enrollment = controller->state == FP_WAIT_FIRST_SCAN
                                || controller->state == FP_WAIT_FIRST_REMOVAL
                                || controller->state == FP_WAIT_SECOND_SCAN;
  if (waiting_for_enrollment
      && (uint32_t)(HAL_GetTick() - controller->state_started) >= ENROLL_STAGE_TIMEOUT_MS)
  {
    controller->result = FP_RESULT_TIMEOUT;
    EnterState(controller, FP_WAIT_REMOVAL);
    return;
  }

  int16_t result;
  switch (controller->state)
  {
    case FP_CONNECTING:
      result = Zw3020_Connect(&controller->storage);
      if (result != ZW3020_OK)
      {
        CommandFailed(controller, result);
        EnterState(controller, FP_ERROR);
        break;
      }
      controller->error = ZW3020_OK;
      controller->result = FP_RESULT_NONE;
      EnterState(controller, FP_READY);
      break;

    case FP_FIND_FREE_SLOT:
      result = Zw3020_FindFreeSlot(controller->storage.capacity, &controller->enrollment_id);
      if (result == ZW3020_FULL)
      {
        controller->result = FP_RESULT_FULL;
        EnterState(controller, FP_WAIT_REMOVAL);
      }
      else if (result != ZW3020_OK)
      {
        CommandFailed(controller, result);
      }
      else
      {
        EnterState(controller, FP_WAIT_FIRST_SCAN);
      }
      break;

    case FP_READY:
    case FP_WAIT_FIRST_SCAN:
    case FP_WAIT_SECOND_SCAN:
      result = Zw3020_GetImage();
      if (result == ZW3020_NO_FINGER)
      {
        break;
      }
      if (result != ZW3020_OK)
      {
        CommandFailed(controller, result);
        break;
      }
      controller->result = FP_RESULT_NONE;
      controller->error = ZW3020_OK;
      if (controller->state == FP_WAIT_FIRST_SCAN)
      {
        EnterState(controller, FP_EXTRACT_FIRST);
      }
      else if (controller->state == FP_WAIT_SECOND_SCAN)
      {
        EnterState(controller, FP_EXTRACT_SECOND);
      }
      else
      {
        EnterState(controller, FP_EXTRACT_MATCH);
      }
      break;

    case FP_EXTRACT_FIRST:
      result = Zw3020_Extract(1);
      if (result == ZW3020_OK)
      {
        EnterState(controller, controller->reject_duplicates ? FP_CHECK_DUPLICATE : FP_WAIT_FIRST_REMOVAL);
      }
      else
      {
        CommandFailed(controller, result);
      }
      break;

    case FP_WAIT_FIRST_REMOVAL:
    case FP_WAIT_REMOVAL:
      result = Zw3020_GetImage();
      if (result == ZW3020_NO_FINGER)
      {
        if (controller->state == FP_WAIT_FIRST_REMOVAL)
        {
          EnterState(controller, FP_WAIT_SECOND_SCAN);
        }
        else
        {
          EnterState(controller, FP_READY);
        }
      }
      else if (result != ZW3020_OK)
      {
        CommandFailed(controller, result);
      }
      break;

    case FP_EXTRACT_SECOND:
      result = Zw3020_Extract(2);
      if (result == ZW3020_OK)
      {
        EnterState(controller, FP_COMBINE_SCANS);
      }
      else
      {
        CommandFailed(controller, result);
      }
      break;

    case FP_COMBINE_SCANS:
      result = Zw3020_Combine();
      if (result == ZW3020_OK)
      {
        if (controller->defer_store)
        {
          controller->result = FP_RESULT_PREPARED;
          EnterState(controller, FP_HOLD_TEMPLATE);
        }
        else EnterState(controller, FP_SAVE_TEMPLATE);
      }
      else
      {
        CommandFailed(controller, result);
      }
      break;

    case FP_SAVE_TEMPLATE:
      result = Zw3020_Store(controller->enrollment_id);
      if (result == ZW3020_OK)
      {
        ++controller->storage.count;
        controller->result = FP_RESULT_ENROLLED;
        EnterState(controller, FP_WAIT_REMOVAL);
      }
      else
      {
        CommandFailed(controller, result);
      }
      break;

    case FP_EXTRACT_MATCH:
      result = Zw3020_Extract(1);
      if (result == ZW3020_OK)
      {
        EnterState(controller, FP_SEARCH_DATABASE);
      }
      else
      {
        CommandFailed(controller, result);
      }
      break;

    case FP_SEARCH_DATABASE:
    {
      Zw3020Match match;
      result = Zw3020_Search(controller->storage.capacity, &match);
      if (result == ZW3020_OK)
      {
        controller->user = match;
        controller->user_identified = true;
        controller->result = FP_RESULT_MATCH;
        EnterState(controller, FP_WAIT_REMOVAL);
      }
      else if (result == ZW3020_NO_MATCH || result == ZW3020_EMPTY)
      {
        controller->result = FP_RESULT_NO_MATCH;
        EnterState(controller, FP_WAIT_REMOVAL);
      }
      else
      {
        CommandFailed(controller, result);
      }
      break;
    }

    case FP_ERROR:
      /* PA0 retries the connection. Other tasks keep collecting input. */
      break;

    case FP_CHECK_DUPLICATE:
    {
      Zw3020Match match;
      result = Zw3020_Search(controller->storage.capacity, &match);
      if (result == ZW3020_OK)
      {
        controller->user = match;
        controller->result = FP_RESULT_DUPLICATE;
        EnterState(controller, FP_WAIT_REMOVAL);
      }
      else if (result == ZW3020_NO_MATCH || result == ZW3020_EMPTY)
        EnterState(controller, FP_WAIT_FIRST_REMOVAL);
      else CommandFailed(controller, result);
      break;
    }
    case FP_HOLD_TEMPLATE:
      /* Combined template stays in module RAM until explicit Save or Cancel. */
      break;
  }
}
