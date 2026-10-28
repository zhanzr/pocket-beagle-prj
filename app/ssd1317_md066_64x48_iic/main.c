/*
 * SSD1317 "MD066" 0.66" 64x48 mono OLED demo for the PocketBeagle
 * (AM3358, Debian), ported from the MP157 DK1 reference
 * (mp157-dk1-prj/apps/ssd1317_md066_64x48_iic) and the AT32F425 reference
 * (at32-start-f407 f425-start bare/ssd1317_md066_64x48_iic).
 *
 * Same Linux user-space architecture as the MP157 reference - i2c-dev for
 * the module, GPIO char-device uAPI for RES - no library dependencies:
 *
 *   SCL  -> P1.28 (I2C2_SCL)   /dev/i2c-2, module 7-bit address 0x3C
 *   SDA  -> P1.26 (I2C2_SDA)
 *   RES  -> P2.22 (GPIO1_14),  active low, driven by line name "P2.22"
 *   VCC  -> P1.14 (3.3 V), GND -> P1.16
 *
 * The module does not ACK while RES is floating/low (observed on this
 * wiring), so RES is requested with an initial active (high) value before
 * the probe and the reset pulse in oled_init().
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "oled.h"

#define OLED_ADDR     0x3C
#define I2C_DEV       "/dev/i2c-2"
#define RES_LINE_NAME "P2.22"
#define MAX_SECONDS   30

static volatile sig_atomic_t running = 1;

static void stop_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* find the GPIO chip+offset whose line name matches name (e.g. "P2.22") */
static int line_find_by_name(const char *name, int *chip_fd,
                             unsigned int *offset, int *chip_idx)
{
    for (int i = 0; i < 16; i++)
    {
        char path[64];
        int fd;

        snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
        {
            if (errno == ENOENT)
                break;
            continue;
        }
        for (unsigned int l = 0; l < 64; l++)
        {
            struct gpioline_info info;

            memset(&info, 0, sizeof(info));
            info.line_offset = l;
            if (ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &info) < 0)
                break;
            if (strcmp(info.name, name) == 0)
            {
                *chip_fd = fd;
                *offset = l;
                *chip_idx = i;
                return 0;
            }
        }
        close(fd);
    }
    return -1;
}

/* request one line as output; initial value high (RES inactive) */
static int line_request_output(int chip_fd, unsigned int offset)
{
    struct gpio_v2_line_request req;

    memset(&req, 0, sizeof(req));
    req.offsets[0] = offset;
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "ssd1317_demo");

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
        return -1;
    {
        struct gpio_v2_line_values vals;

        memset(&vals, 0, sizeof(vals));
        vals.mask = 1;
        vals.bits = 1; /* RES inactive (high) */
        ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals);
    }
    return req.fd;
}

int main(void)
{
    int i2c_fd, res_chip = -1, res_fd, res_chip_idx = -1;
    unsigned int res_offset = 0;

    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

    i2c_fd = open(I2C_DEV, O_RDWR);
    if (i2c_fd < 0)
    {
        fprintf(stderr, "open %s: %s\n", I2C_DEV, strerror(errno));
        return 1;
    }
    if (ioctl(i2c_fd, I2C_SLAVE, OLED_ADDR) < 0)
    {
        fprintf(stderr, "I2C_SLAVE: %s\n", strerror(errno));
        return 1;
    }

    if (line_find_by_name(RES_LINE_NAME, &res_chip, &res_offset,
                          &res_chip_idx) < 0)
    {
        fprintf(stderr, "GPIO line " RES_LINE_NAME " not found\n");
        return 1;
    }
    res_fd = line_request_output(res_chip, res_offset);
    if (res_fd < 0)
    {
        fprintf(stderr, "RES line " RES_LINE_NAME " busy: %s\n",
                strerror(errno));
        return 1;
    }
    printf("RES = " RES_LINE_NAME " (gpiochip%d line %u) ok\n",
           res_chip_idx, res_offset);

    oled_set_i2c_fd(i2c_fd);

    {
        uint8_t probe[2] = { 0x00, 0xAE };

        if (write(i2c_fd, probe, 2) != 2)
        {
            fprintf(stderr, "OLED not detected at 0x%02x on %s (%s) - "
                    "check wiring (SCL=P1.28, SDA=P1.26, RES=P2.22, "
                    "VCC=P1.14, GND=P1.16)\n",
                    OLED_ADDR, I2C_DEV, strerror(errno));
            return 1;
        }
    }

    printf("SSD1317 MD066 64x48 I2C demo start (%s @ 0x%02x)\n",
           I2C_DEV, OLED_ADDR);
    OLED_Init(res_fd);

    while (running)
    {
        OLED_Demo();
        if (!running)
            break;
        oled_delay_ms(200);
    }

    OLED_Clear();
    OLED_ShowString(0, 2, (const unsigned char *)"bye", 16);
    printf("bye\n");

    close(res_fd);
    close(res_chip);
    close(i2c_fd);
    return 0;
}
