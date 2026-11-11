#ifndef FINGERPRINT_H
#define FINGERPRINT_H

#include "zw3020.h"

typedef enum {
  FP_CONNECTING,
  FP_READY,
  FP_FIND_FREE_SLOT,
  FP_WAIT_FIRST_SCAN,
  FP_EXTRACT_FIRST,
  FP_WAIT_FIRST_REMOVAL,
  FP_WAIT_SECOND_SCAN,
  FP_EXTRACT_SECOND,
  FP_COMBINE_SCANS,
  FP_SAVE_TEMPLATE,
  FP_EXTRACT_MATCH,
  FP_SEARCH_DATABASE,
  FP_WAIT_REMOVAL,
  FP_ERROR,
  FP_CHECK_DUPLICATE,
  FP_HOLD_TEMPLATE
} FingerprintState;

typedef enum {
  FP_RESULT_NONE,
  FP_RESULT_MATCH,
  FP_RESULT_NO_MATCH,
  FP_RESULT_ENROLLED,
  FP_RESULT_CANCELLED,
  FP_RESULT_TIMEOUT,
  FP_RESULT_FULL,
  FP_RESULT_ERROR,
  FP_RESULT_DUPLICATE,
  FP_RESULT_PREPARED
} FingerprintResult;

typedef struct {
  FingerprintState state;
  FingerprintResult result;
  Zw3020Info storage;
  uint16_t enrollment_id;
  bool user_identified;
  Zw3020Match user;
  int16_t error;
  uint8_t error_command;
  uint32_t state_started;
  bool reject_duplicates;
  bool defer_store;
} FingerprintController;

void Fingerprint_Init(FingerprintController *controller);
/* One FSM step per call. A PA0 press requests enroll, cancel or reconnect. */
void Fingerprint_Step(FingerprintController *controller, bool control_pressed);

#endif
