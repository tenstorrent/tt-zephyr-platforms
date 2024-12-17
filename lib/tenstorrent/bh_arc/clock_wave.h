#ifndef CLOCK_WAVE_H
#define CLOCK_WAVE_H

typedef enum {
  ZeroSkewClk = 0,
  ClockWave = 1,
} ClockingScheme;

void SwitchClkScheme(ClockingScheme clk_scheme);
#endif
