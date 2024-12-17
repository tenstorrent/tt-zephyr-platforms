#ifndef FAN_CTRL_H
#define FAN_CTRL_H

#include <stdint.h>

void FanCtrlInit();
void SetFanSpeed(uint8_t speed);
uint8_t GetFanSpeed();

#endif