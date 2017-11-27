#pragma once

#include <inttypes.h>

void InitGPU();
void AddHistogramSample();
uint64_t EstimateFrameRateInterval();
uint64_t PredictNextFrameArrivalTime();

extern uint16_t *videoCoreFramebuffer[2];
extern volatile int numNewGpuFrames;
extern int displayXOffset;
extern int displayYOffset;

#define FRAME_HISTORY_MAX_SIZE 240
extern int frameTimeHistorySize;

struct FrameHistory
{
  uint64_t time;
  bool interlaced;
};

extern FrameHistory frameTimeHistory[FRAME_HISTORY_MAX_SIZE];
