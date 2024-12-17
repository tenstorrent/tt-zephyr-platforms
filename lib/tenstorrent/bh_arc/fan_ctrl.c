#include "fan_ctrl.h"

#include "timer.h"
#include "dw_apb_i2c.h"

#define FAN_CTRL_ADDR   0x2C
#define FAN_CTRL_MST_ID 2

// fan controller commands
#define FAN1_CONFIG_1           0x10
#define FAN1_CONFIG_2A          0x11
#define FAN1_CONFIG_3           0x13
#define FAN1_DUTY_CYCLE         0x26
#define FAN_CTRL_CMD_BYTE_SIZE  1
#define FAN_CTRL_DATA_BYTE_SIZE 1

// The fan controller is MAX6639. It controlls 2 fans, only the first fan is intended to be used.
// The first fan is configured as a 4-wire fan with PWM speed-control input.
// The maximum PMBus clock freq is 100kHz.
void FanCtrlInit() {
  I2CInit(I2CMst, FAN_CTRL_ADDR, I2CStandardMode, FAN_CTRL_MST_ID);
  uint8_t fan_control = 0x2; // use positive polarity
  I2CWriteBytes(FAN_CTRL_MST_ID, FAN1_CONFIG_2A, FAN_CTRL_CMD_BYTE_SIZE, &fan_control, FAN_CTRL_DATA_BYTE_SIZE);
  fan_control = 0x83; // enable PWM manual mode, RPM to max
  I2CWriteBytes(FAN_CTRL_MST_ID, FAN1_CONFIG_1, FAN_CTRL_CMD_BYTE_SIZE, &fan_control, FAN_CTRL_DATA_BYTE_SIZE);
  fan_control = 0x23; // disable pulse stretching, deassert THERM, set PWM frequency to high
  I2CWriteBytes(FAN_CTRL_MST_ID, FAN1_CONFIG_3, FAN_CTRL_CMD_BYTE_SIZE, &fan_control, FAN_CTRL_DATA_BYTE_SIZE);
  SetFanSpeed(100);
}

// speed is between 0 to 100
void SetFanSpeed(uint8_t speed) {
  I2CInit(I2CMst, FAN_CTRL_ADDR, I2CStandardMode, FAN_CTRL_MST_ID);
  uint8_t pwm_setting = speed * 1.2; // fan controller pwm has 120 time slots
  I2CWriteBytes(FAN_CTRL_MST_ID, FAN1_DUTY_CYCLE, FAN_CTRL_CMD_BYTE_SIZE, &pwm_setting, FAN_CTRL_DATA_BYTE_SIZE);
}

uint8_t GetFanSpeed() {
  I2CInit(I2CMst, FAN_CTRL_ADDR, I2CStandardMode, FAN_CTRL_MST_ID);
  uint8_t pwm_setting;
  I2CReadBytes(FAN_CTRL_MST_ID, FAN1_DUTY_CYCLE, FAN_CTRL_CMD_BYTE_SIZE, &pwm_setting, FAN_CTRL_DATA_BYTE_SIZE, 0);
  uint8_t fan_speed = pwm_setting / 1.2; // convert from pwm setting to fan speed
  return fan_speed;
}