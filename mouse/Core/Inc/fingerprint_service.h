#ifndef FINGERPRINT_SERVICE_H
#define FINGERPRINT_SERVICE_H
#include "fingerprint.h"

typedef enum
{
  FP_JOB_NONE,
  FP_JOB_VERIFY,
  FP_JOB_ENROLL,
  FP_JOB_STORE,
  FP_JOB_DELETE
} FpJob;
typedef enum
{
  FP_REPLY_IDLE,
  FP_REPLY_BUSY,
  FP_REPLY_VERIFIED,
  FP_REPLY_MISMATCH,
  FP_REPLY_PREPARED,
  FP_REPLY_ENROLLED,
  FP_REPLY_DELETED,
  FP_REPLY_DUPLICATE,
  FP_REPLY_ERROR,
  FP_REPLY_CANCELLED
} FpReply;
typedef struct
{
  FpJob job;
  FpReply reply;
  uint16_t id;
  int16_t error;
  FingerprintState progress;
} FpServiceStatus;

void FpService_SetConfigurationMode(bool enabled);
bool FpService_Request(FpJob job, uint16_t id);
void FpService_Cancel(void);
FpServiceStatus FpService_GetStatus(void);
/* Called only by the existing UART2 owner task. True means fresh normal match. */
bool FpService_Step(FingerprintController *controller, bool button, uint16_t *normal_id);
#endif
