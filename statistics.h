#pragma once

#include <inttypes.h>

#include "gpu.h"

void PollHardwareInfo();
void RefreshStatisticsOverlayText();
void DrawStatisticsOverlay(uint16_t *framebuffer);

#ifdef STATISTICS

extern volatile uint64_t timeWastedPollingGPU;
extern int statsSpiBusSpeed;
extern int statsCpuFrequency;
extern double statsCpuTemperature;
extern double spiThreadUtilizationRate;
extern double spiBusDataRate;
extern int statsGpuPollingWasted;
extern uint64_t statsBytesTransferred;

extern int frameSkipTimeHistorySize;
extern uint64_t frameSkipTimeHistory[FRAME_HISTORY_MAX_SIZE];

// All overlay statistics are double-buffered: the updated data fields
// are polled at certain rate, and updated in the first copy below. However
// it is not desired that any changes in the overlay numbers would trigger
// a repaint of the display, since that would skew the fps counts and similar,
// if updated overlay text would cause an update of a new frame.

// The strings below are what is currently shown on screen, and the fields
// above specify the latest up to date fields of the data.
extern char fpsText[32];
extern char spiUsagePercentageText[32];
extern char spiBusDataRateText[32];
extern uint16_t spiUsageColor, fpsColor;
extern char statsFrameSkipText[32];
extern char spiSpeedText[32];
extern char cpuTemperatureText[32];
extern uint16_t cpuTemperatureColor;
extern char gpuPollingWastedText[32];
extern uint16_t gpuPollingWastedColor;

#endif
