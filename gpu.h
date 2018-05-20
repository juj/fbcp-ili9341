#pragma once

#include <inttypes.h>

void InitGPU();
void AddHistogramSample();
void SnapshotFramebuffer(uint16_t *destination);
bool IsNewFramebuffer(uint16_t *possiblyNewFramebuffer, uint16_t *oldFramebuffer);
uint64_t EstimateFrameRateInterval();
uint64_t PredictNextFrameArrivalTime();

extern uint16_t *videoCoreFramebuffer[2];
extern volatile int numNewGpuFrames;
extern int displayXOffset;
extern int displayYOffset;
extern int gpuFrameWidth;
extern int gpuFrameHeight;
extern int gpuFramebufferScanlineStrideBytes;
extern int gpuFramebufferSizeBytes;

extern int excessPixelsLeft;
extern int excessPixelsRight;
extern int excessPixelsTop;
extern int excessPixelsBottom;

#define FRAME_HISTORY_MAX_SIZE 240
extern int frameTimeHistorySize;

struct FrameHistory
{
  uint64_t time;
  bool interlaced;
};

extern FrameHistory frameTimeHistory[FRAME_HISTORY_MAX_SIZE];
