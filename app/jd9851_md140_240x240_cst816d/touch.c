/*
  touch.c - CST816D capacitive touch over i2c-dev (/dev/i2c-2, 7-bit
  address 0x15) for the PocketBeagle jd9851_md140_240x240_cst816d port.
  Same register protocol as the nano-f411 reference (bit-banged there,
  hardware I2C2 here): touch data block from register 0x00, byte 3
  (register 0x03) = 0x80 marks an active touch.
*/
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "touch.h"

#define I2C_DEV    "/dev/i2c-2"
#define CTP_ADDR   0x15   /* 7-bit; 0x2A on the wire for write */

static int s_fd = -1;

int Touch_Init(void)
{
    s_fd = open(I2C_DEV, O_RDWR);
    if (s_fd < 0)
    {
        fprintf(stderr, "touch: open %s: %s\n", I2C_DEV, strerror(errno));
        return -1;
    }
    if (ioctl(s_fd, I2C_SLAVE, CTP_ADDR) < 0)
    {
        fprintf(stderr, "touch: I2C_SLAVE 0x%02x: %s\n", CTP_ADDR,
                strerror(errno));
        close(s_fd);
        s_fd = -1;
        return -1;
    }
    return 0;
}

/* Read len bytes starting at register 0 (the CST816 touch data block). */
void Touch_Read(uint8_t *buf, uint8_t len)
{
    uint8_t reg = 0x00;

    if (s_fd < 0)
        return;
    if (write(s_fd, &reg, 1) != 1)
        return;
    if (read(s_fd, buf, len) != len)
        return;
}
