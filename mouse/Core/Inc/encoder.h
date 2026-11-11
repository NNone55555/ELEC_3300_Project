#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

typedef struct {
  int32_t steps;
  uint8_t phase_a;
  uint8_t phase_b;
} EncoderSnapshot;

void Encoder_Init(void);
void Encoder_Sample1ms(void);
EncoderSnapshot Encoder_GetSnapshot(void);

#endif
