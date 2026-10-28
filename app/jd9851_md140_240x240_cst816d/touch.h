/*
  touch.h - CST816D capacitive touch controller (I2C2, i2c-dev) for the
  PocketBeagle jd9851_md140_240x240_cst816d port.

  Wiring: TOUCH_SCL = P1.28 (I2C2_SCL, /dev/i2c-2)
          TOUCH_SDA = P1.26 (I2C2_SDA)
  7-bit slave address 0x15.

  Protocol (as in the nano-f411 reference, vendored TK018F3716 example):
  the touch data block is read from register 0x00 (8 bytes). Byte 3 =
  0x80 marks an active touch; byte 4 = X (8 bit), bytes 5:6 = Y (12 bit).
*/
#ifndef __TOUCH_H
#define __TOUCH_H

#include <stdint.h>

int  Touch_Init(void);                      /* open i2c-dev, set slave   */
void Touch_Read(uint8_t *buf, uint8_t len); /* regs 0x00..               */

#endif /* __TOUCH_H */
