/*
  lcd.c - JD9851 1.4" 240x240 LCD driver for the PocketBeagle (MD140
  module). Driver logic ported verbatim from the nano-f411 reference
  (f4-demo/nano-f411/app/jd9851_md140_240x240_cst816d, vendor-verbatim
  init from STM32_LCD_TK014F1828_1246_hardSPI_hal_captouch); the bus
  layer is re-implemented for Linux user space:

  - pixels via /dev/spidev0.0 (SPI0: SCK=P1.08, MOSI=P1.12, hardware
    CS=P1.06), mode 3 (CPOL=1 CPHA=1), MSB first
  - DC(RS) via the GPIO char device, gpiod line name "P2.22"
    (command byte: DC low, data bytes: DC high)
  - CS framing is per-write() by the spidev driver; the panel accepts
    this transaction framing (superset of the vendor's leave-CS-low)

  The module has no reset pin (the vendor sequence settles CS instead)
  and no backlight pin (hardwired on-module).
*/

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "lcd.h"

#define SPI_DEV        "/dev/spidev0.0"
#define SPI_SPEED_HZ   32000000U   /* AM335x MCSPI goes to 48 MHz; back off
                                      to 16 MHz if the wire misbehaves */
#define DC_LINE_NAME   "P2.22"
/* spidev rejects transfers larger than its 'bufsiz' (default 4096) with
 * EMSGSIZE - keep chunks within that limit */
#define TX_CHUNK       4096U

static int s_spi_fd = -1;
static int s_dc_fd = -1;
static unsigned int s_dc_offset;

/* data bytes accumulated before one spidev write() */
static uint8_t s_tx[TX_CHUNK];
static uint32_t s_tx_len;

static uint16_t s_Color     = BLACK;
static uint16_t s_BackColor = BLACK;
static uint8_t  s_Transparent = 0;
static pFONT  *s_AsciiFont = NULL;

/* runtime drawing window */
static uint16_t s_win_x = 0, s_win_y = 0;
static uint16_t s_win_w = COL, s_win_h = ROW;

/* ------------------------------------------------------------------ */
/* port primitives                                                     */
/* ------------------------------------------------------------------ */

static void dc_set(int on)
{
    struct gpio_v2_line_values vals;

    memset(&vals, 0, sizeof(vals));
    vals.mask = 1;
    vals.bits = on ? 1 : 0;
    if (ioctl(s_dc_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0)
    {
        fprintf(stderr, "lcd: DC set: %s\n", strerror(errno));
    }
}

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

void lcd_delay_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
    {
        if (!lcd_running)
            return;
    }
}

unsigned long lcd_kHz(void)
{
    return SPI_SPEED_HZ / 1000U;
}

/* ------------------------------------------------------------------ */
/* bus open                                                            */
/* ------------------------------------------------------------------ */

void LCD_GPIOInit(void)
{
    uint8_t mode = SPI_MODE_3;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED_HZ;
    int chip_fd = -1, chip_idx = -1;
    unsigned int offset = 0;
    struct gpio_v2_line_request req;

    if (s_spi_fd >= 0)
        return;

    s_spi_fd = open(SPI_DEV, O_RDWR);
    if (s_spi_fd < 0)
    {
        fprintf(stderr, "lcd: open %s: %s (root-only? run with sudo or "
                "add a udev rule)\n", SPI_DEV, strerror(errno));
        exit(1);
    }
    if (ioctl(s_spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(s_spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(s_spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
    {
        fprintf(stderr, "lcd: spi setup: %s\n", strerror(errno));
        exit(1);
    }

    if (line_find_by_name(DC_LINE_NAME, &chip_fd, &offset, &chip_idx) < 0)
    {
        fprintf(stderr, "lcd: DC line " DC_LINE_NAME " not found\n");
        exit(1);
    }
    memset(&req, 0, sizeof(req));
    req.offsets[0] = offset;
    req.num_lines = 1;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    snprintf(req.consumer, sizeof(req.consumer), "jd9851-dc");
    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
    {
        fprintf(stderr, "lcd: DC line request: %s\n", strerror(errno));
        exit(1);
    }
    s_dc_fd = req.fd;
    s_dc_offset = offset;
    dc_set(1);                      /* idle: data level */
    close(chip_fd);

    s_tx_len = 0;
}

/* ------------------------------------------------------------------ */
/* byte-level transfers                                                */
/* ------------------------------------------------------------------ */

static void spi_flush(void)
{
    if (s_tx_len != 0U && s_spi_fd >= 0)
    {
        if (write(s_spi_fd, s_tx, s_tx_len) != (ssize_t)s_tx_len)
        {
            fprintf(stderr, "lcd: spi write: %s\n", strerror(errno));
        }
        s_tx_len = 0U;
    }
}

/* Queue one data byte (framing already open, DC already high). */
static void spi_put(uint8_t dat)
{
    s_tx[s_tx_len++] = dat;
    if (s_tx_len >= TX_CHUNK)
    {
        spi_flush();
    }
}

/* JD9851 command: DC low for the command byte, DC high again for the
 * data bytes that follow inside the same (vendor-tolerated) framing. */
void WriteComm(uint16_t data)
{
    uint8_t cmd = (uint8_t)data;

    spi_flush();
    dc_set(0);
    if (write(s_spi_fd, &cmd, 1) != 1)
    {
        fprintf(stderr, "lcd: spi cmd: %s\n", strerror(errno));
    }
    dc_set(1);
}

void WriteData(uint16_t data)
{
    spi_put((uint8_t)data);
}

void SendData(uint32_t color)
{
    spi_put((uint8_t)(color >> 8));
    spi_put((uint8_t)color);
}

void LCD_WriteDataFast(uint8_t data)
{
    spi_put(data);
}

/* Solid-color burst into the open frame: pattern-filled buffer, chunked
 * writes (the spidev per-write overhead is amortized over 32 KB). */
void LCD_FillBulk(uint32_t color, uint32_t pixels)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;

    spi_flush();
    s_tx_len = 0;
    while (pixels != 0U)
    {
        uint32_t chunk_px = pixels < (TX_CHUNK / 2U)
                            ? pixels : (TX_CHUNK / 2U);
        uint32_t i;

        for (i = 0; i < chunk_px; i++)
        {
            s_tx[2 * i] = hi;
            s_tx[2 * i + 1] = lo;
        }
        s_tx_len = chunk_px * 2U;
        spi_flush();
        pixels -= chunk_px;
        if (!lcd_running)
            return;
    }
}

void LCD_BeginData(void)
{
    spi_flush();
    dc_set(1);
}
void LCD_EndData(void)
{
    spi_flush();
}

/* ------------------------------------------------------------------ */
/* window                                                              */
/* ------------------------------------------------------------------ */

void LCD_SetWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (x >= COL || y >= ROW) { return; }
    if (w == 0U || h == 0U)   { return; }
    if (x + w > COL) { w = (uint16_t)(COL - x); }
    if (y + h > ROW) { h = (uint16_t)(ROW - y); }
    s_win_x = x;
    s_win_y = y;
    s_win_w = w;
    s_win_h = h;
}

void LCD_ResetWindow(void)
{
    s_win_x = 0U;
    s_win_y = 0U;
    s_win_w = COL;
    s_win_h = ROW;
}

uint16_t LCD_W(void)
{
    return s_win_w;
}

uint16_t LCD_H(void)
{
    return s_win_h;
}

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */

/* No reset pin on this module: the vendor sequence settles CS (high,
 * then low) and waits 100 ms before the init commands. */
void LCD_RESET(void)
{
    lcd_delay_ms(20);
    lcd_delay_ms(100);
}

/* Init sequence - JD9851 (vendored TK014F1828_1246 example, verbatim
   registers): password unlock (0xDF), page select (0xDE), panel power/
   DCDC/gamma/timing registers across PAGE0/PAGE2, sleep out (120 ms),
   oscillator + MIPI timing, INVON, COLMOD 16bpp, MADCTL 0x00, display
   on. */
void LCD_IC_Init(void)
{
    WriteComm(0xDF);              /* Password */
    WriteData(0x98);
    WriteData(0x51);
    WriteData(0xE9);

    /* ---------------- PAGE0 ---------------- */
    WriteComm(0xDE);
    WriteData(0x00);

    WriteComm(0xB7);              /* VGMP/VGSP/VGMN/VGSN */
    WriteData(0x31);
    WriteData(0x88);
    WriteData(0x31);
    WriteData(0x10);

    WriteComm(0xC8);              /* gamma */
    WriteData(0x3F); WriteData(0x3C); WriteData(0x34); WriteData(0x2D);
    WriteData(0x2D); WriteData(0x2E); WriteData(0x2A); WriteData(0x2C);
    WriteData(0x2C); WriteData(0x2D); WriteData(0x2D); WriteData(0x2A);
    WriteData(0x2A); WriteData(0x2A); WriteData(0x28); WriteData(0x0E);
    WriteData(0x3F); WriteData(0x3C); WriteData(0x34); WriteData(0x2D);
    WriteData(0x2D); WriteData(0x2E); WriteData(0x2A); WriteData(0x2C);
    WriteData(0x2C); WriteData(0x2D); WriteData(0x2D); WriteData(0x2A);
    WriteData(0x2A); WriteData(0x2A); WriteData(0x28); WriteData(0x0E);

    WriteComm(0xB9);              /* POW_CTRL */
    WriteData(0x33);
    WriteData(0x28);
    WriteData(0xCC);

    WriteComm(0xBB);              /* DCDC_SEL */
    WriteData(0x06);
    WriteData(0x7A);
    WriteData(0x30);
    WriteData(0x30);
    WriteData(0x6C);
    WriteData(0x60);
    WriteData(0x50);
    WriteData(0x70);

    WriteComm(0xBC);              /* VDDD_CTRL */
    WriteData(0x38);
    WriteData(0x3C);

    WriteComm(0xC0);              /* SETSTBA */
    WriteData(0x31);
    WriteData(0x20);

    WriteComm(0xC1);              /* SETPANEL (default) */
    WriteData(0x16);

    WriteComm(0xC3);              /* SETRGBCYC */
    WriteData(0x08);
    WriteData(0x00);
    WriteData(0x0A);
    WriteData(0x10);
    WriteData(0x08);
    WriteData(0x54);
    WriteData(0x45);
    WriteData(0x71);
    WriteData(0x2C);

    WriteComm(0xC4);              /* SETRGBCYC (default) */
    WriteData(0x00);
    WriteData(0xA0);
    WriteData(0x79);
    WriteData(0x0E);
    WriteData(0x0A);
    WriteData(0x16);
    WriteData(0x79);
    WriteData(0x0E);
    WriteData(0x0A);
    WriteData(0x16);
    WriteData(0x79);
    WriteData(0x0E);
    WriteData(0x0A);
    WriteData(0x16);
    WriteData(0x82);
    WriteData(0x00);
    WriteData(0x03);

    WriteComm(0xD0);              /* SET_GD (default) */
    WriteData(0x04);
    WriteData(0x0C);
    WriteData(0x6B);
    WriteData(0x0F);
    WriteData(0x07);
    WriteData(0x03);

    WriteComm(0xD7);              /* RAMCTRL (default) */
    WriteData(0x00);
    WriteData(0x00);

    /* ---------------- PAGE2 ---------------- */
    WriteComm(0xDE);
    WriteData(0x02);
    lcd_delay_ms(1);

    WriteComm(0xB8);              /* DCDC_SET */
    WriteData(0x19);
    WriteData(0xA0);
    WriteData(0x2F);
    WriteData(0x04);
    WriteData(0x33);

    WriteComm(0xC1);              /* SETRGBCYC2 */
    WriteData(0x10);
    WriteData(0x66);
    WriteData(0x66);
    WriteData(0x01);

    /* ---------------- PAGE0 ---------------- */
    WriteComm(0xDE);
    WriteData(0x00);

    WriteComm(0x11);              /* Sleep out */
    lcd_delay_ms(120);

    /* ---------------- PAGE2 ---------------- */
    WriteComm(0xDE);
    WriteData(0x02);
    lcd_delay_ms(1);

    WriteComm(0xC5);              /* OSCM: 10 MHz */
    WriteData(0x01);
    WriteData(0x00);
    WriteData(0x00);
    lcd_delay_ms(1);

    WriteComm(0xCA);              /* SETMIPI_2 */
    WriteData(0x10);
    WriteData(0x20);
    WriteData(0xF4);
    lcd_delay_ms(1);

    /* ---------------- PAGE0 ---------------- */
    WriteComm(0xDE);
    WriteData(0x00);
    lcd_delay_ms(1);

    WriteComm(0x21);              /* INVON - IPS inversion */

    WriteComm(0x3A);              /* COLMOD 16bpp */
    WriteData(0x55);

    WriteComm(0x36);              /* MADCTL */
    WriteData(0x00);

    WriteComm(0x29);              /* Display on */
    lcd_delay_ms(1);

    spi_flush();
}

void LCD_Init(void)
{
    LCD_GPIOInit();
    LCD_RESET();
    LCD_IC_Init();

    /* Clear to black. */
    DispColor(BLACK);
}

/* Re-init an already-running panel: display off, then the full vendor
 * sequence again - its password/page-select preamble restarts the
 * panel's register config, and the sleep out inside provides the
 * power-cycle-like reset. */
void LCD_Reinit(void)
{
    WriteComm(0x28);              /* display off */
    spi_flush();
    lcd_delay_ms(20);

    LCD_RESET();
    LCD_IC_Init();
}

/* BlockWrite - set a pixel window relative to the current drawing
 * window, then leave DC ready for the raster dump. */
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend)
{
    WriteComm(0x2A);
    WriteData((uint16_t)((Xstart + s_win_x) >> 8));
    WriteData((uint16_t)(Xstart + s_win_x));
    WriteData((uint16_t)((Xend + s_win_x) >> 8));
    WriteData((uint16_t)(Xend + s_win_x));

    WriteComm(0x2B);
    WriteData((uint16_t)((Ystart + s_win_y) >> 8));
    WriteData((uint16_t)(Ystart + s_win_y));
    WriteData((uint16_t)((Yend + s_win_y) >> 8));
    WriteData((uint16_t)(Yend + s_win_y));

    WriteComm(0x2C);
}

/* ------------------------------------------------------------------ */
/* vendor demo screens                                                 */
/* ------------------------------------------------------------------ */

void DispColor(uint32_t color)
{
    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    LCD_FillBulk(color, (uint32_t)LCD_W() * LCD_H());
    LCD_EndData();
}

void DispFrame(void)
{
    int i, j;
    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    LCD_WriteDataFast(0xF8); LCD_WriteDataFast(0x00);
    for (i = 0; i < (int)LCD_W() - 2; i++) { LCD_WriteDataFast(0xFF); LCD_WriteDataFast(0xFF); }
    LCD_WriteDataFast(0x00); LCD_WriteDataFast(0x1F);
    for (j = 0; j < (int)LCD_H() - 2; j++)
    {
        LCD_WriteDataFast(0xF8); LCD_WriteDataFast(0x00);
        for (i = 0; i < (int)LCD_W() - 2; i++) { LCD_WriteDataFast(0x00); LCD_WriteDataFast(0x00); }
        LCD_WriteDataFast(0x00); LCD_WriteDataFast(0x1F);
    }
    LCD_WriteDataFast(0xF8); LCD_WriteDataFast(0x00);
    for (i = 0; i < (int)LCD_W() - 2; i++) { LCD_WriteDataFast(0xFF); LCD_WriteDataFast(0xFF); }
    LCD_WriteDataFast(0x00); LCD_WriteDataFast(0x1F);
    LCD_EndData();
}

void DispGrayHor16(void)
{
    int i, j, k;
    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    for (i = 0; i < (int)LCD_H(); i++)
    {
        for (j = 0; j < (int)LCD_W() % 16; j++) { LCD_WriteDataFast(0); LCD_WriteDataFast(0); }
        for (j = 0; j < 16; j++)
        {
            uint16_t c = (uint16_t)(((((j * 2) << 3) | ((j * 4) >> 3)) << 8) |
                                    (((j * 4) << 5) | (j * 2)));
            for (k = 0; k < (int)LCD_W() / 16; k++)
            {
                LCD_WriteDataFast((uint8_t)(c >> 8));
                LCD_WriteDataFast((uint8_t)c);
            }
        }
    }
    LCD_EndData();
}

void DispBand(void)
{
    static const uint16_t color[8] = { 0xF800, 0xF800, 0x07E0, 0x07E0,
                                       0x001F, 0x001F, 0xFFFF, 0xFFFF };
    int i, j, k;
    BlockWrite(0, LCD_W() - 1U, 0, LCD_H() - 1U);
    LCD_BeginData();
    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < (int)LCD_H() / 8; j++)
        {
            for (k = 0; k < (int)LCD_W(); k++)
            {
                LCD_WriteDataFast((uint8_t)(color[i] >> 8));
                LCD_WriteDataFast((uint8_t)color[i]);
            }
        }
    }
    for (j = 0; j < (int)LCD_H() % 8; j++)
    {
        for (k = 0; k < (int)LCD_W(); k++)
        {
            LCD_WriteDataFast((uint8_t)(color[7] >> 8));
            LCD_WriteDataFast((uint8_t)color[7]);
        }
    }
    LCD_EndData();
}

void StopDelay(uint16_t ms)
{
    lcd_delay_ms(ms);
}

/* ------------------------------------------------------------------ */
/* colors, addressing, text and 2D drawing                             */
/* ------------------------------------------------------------------ */

/* Convert a 24-bit RGB888 into a 16-bit RGB565 word. */
static uint16_t rgb888_to_rgb565(uint32_t c)
{
    uint16_t r = (uint16_t)((c & 0x00F80000UL) >> 8);
    uint16_t g = (uint16_t)((c & 0x0000FC00UL) >> 5);
    uint16_t b = (uint16_t)((c & 0x000000F8UL) >> 3);
    return (uint16_t)(r | g | b);
}

void LCD_SetColor(uint32_t rgb888)
{
    s_Color = rgb888_to_rgb565(rgb888);
}

void LCD_SetBackColor(uint32_t rgb888)
{
    s_BackColor = rgb888_to_rgb565(rgb888);
}

void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    x1 = (uint16_t)(x1 + s_win_x);
    y1 = (uint16_t)(y1 + s_win_y);
    x2 = (uint16_t)(x2 + s_win_x);
    y2 = (uint16_t)(y2 + s_win_y);

    WriteComm(0x2A);
    WriteData((uint16_t)(x1 >> 8));
    WriteData((uint16_t)x1);
    WriteData((uint16_t)(x2 >> 8));
    WriteData((uint16_t)x2);

    WriteComm(0x2B);
    WriteData((uint16_t)(y1 >> 8));
    WriteData((uint16_t)y1);
    WriteData((uint16_t)(y2 >> 8));
    WriteData((uint16_t)y2);

    WriteComm(0x2C);
}

void LCD_Clear(void)
{
    uint32_t n = (uint32_t)s_win_w * s_win_h;
    LCD_SetAddress(0, 0, s_win_w - 1, s_win_h - 1);
    LCD_BeginData();
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(s_BackColor >> 8));
        LCD_WriteDataFast((uint8_t)s_BackColor);
    }
    LCD_EndData();
}

void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t n = (uint32_t)width * height;
    if (n == 0U) { return; }
    LCD_SetAddress(x, y, x + width - 1, y + height - 1);
    LCD_BeginData();
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(s_BackColor >> 8));
        LCD_WriteDataFast((uint8_t)s_BackColor);
    }
    LCD_EndData();
}

/* ---- ASCII text ---- */
void LCD_SetAsciiFont(pFONT *font)
{
    s_AsciiFont = font;
}

void LCD_ShowTransparent(uint8_t mode)
{
    s_Transparent = mode;
}

void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c)
{
    uint16_t index;
    uint8_t  disChar;
    /* Must hold the LARGEST font's pixels (8x16 = 128); the 6x12 font
     * only fills the first 72. */
    uint16_t Buff[8 * 16];

    if ((s_AsciiFont == NULL) || (c < 0x20U) || (c > 0x7EU))
    {
        return;
    }
    c -= 0x20U;   /* table starts at space */

    if (s_Transparent)
    {
        uint16_t bytesPerRow = s_AsciiFont->Sizes / s_AsciiFont->Height;
        for (uint16_t row = 0; row < s_AsciiFont->Height; row++)
        {
            for (uint16_t col = 0; col < s_AsciiFont->Width; col++)
            {
                disChar = s_AsciiFont->pTable[(uint16_t)c * s_AsciiFont->Sizes
                          + (uint16_t)row * bytesPerRow + (col / 8)];
                if (disChar & (uint8_t)(1U << (col % 8)))
                {
                    LCD_DrawPoint(x + col, y + row, s_Color);
                }
            }
        }
        return;
    }

    index = 0;
    /* Row-major fill matching the font layout: each glyph row is
     * bytesPerRow bytes, bit 0 = leftmost pixel (the same convention the
     * transparent path uses). */
    uint16_t bytesPerRow = s_AsciiFont->Sizes / s_AsciiFont->Height;
    for (uint16_t row = 0; row < s_AsciiFont->Height; row++)
    {
        for (uint16_t col = 0; col < s_AsciiFont->Width; col++)
        {
            disChar = s_AsciiFont->pTable[(uint16_t)c * s_AsciiFont->Sizes
                      + (uint16_t)row * bytesPerRow + (col / 8)];
            Buff[index++] = (disChar & (uint8_t)(1U << (col % 8)))
                            ? s_Color : s_BackColor;
        }
    }
    LCD_CopyBuffer(x, y, s_AsciiFont->Width, s_AsciiFont->Height, Buff);
}

void LCD_DisplayString(uint16_t x, uint16_t y, char *p)
{
    while (x < COL && *p != '\0')
    {
        LCD_DisplayChar(x, y, (uint8_t)*p);
        x += (uint16_t)((s_AsciiFont != NULL) ? s_AsciiFont->Width : 6);
        p++;
    }
}

/* One 8x16 glyph rendered at an integer scale into a static buffer, then
 * blitted in one window write. Scale 4 max: 32x64 px = 4 KB of RGB565. */
void LCD_DisplayCharBig(uint16_t x, uint16_t y, uint8_t c, uint8_t scale)
{
    static uint16_t Buff[32 * 64];
    uint8_t disChar;

    if (c < 0x20U || c > 0x7EU)
    {
        return;
    }
    c -= 0x20U;   /* table starts at space */

    for (uint16_t row = 0; row < 16; row++)
    {
        disChar = asc2_1608[(uint16_t)c * 16U + row];
        for (uint16_t col = 0; col < 8; col++)
        {
            uint16_t color = (disChar & (uint8_t)(1U << (col % 8)))
                             ? s_Color : s_BackColor;
            for (uint16_t sy = 0; sy < scale; sy++)
            {
                uint16_t *dst = &Buff[(row * scale + sy) * 8U * scale];
                for (uint16_t sx = 0; sx < scale; sx++)
                {
                    dst[col * scale + sx] = color;
                }
            }
        }
    }
    LCD_CopyBuffer(x, y, (uint16_t)(8U * scale), (uint16_t)(16U * scale),
                   Buff);
}

void LCD_DisplayStringBig(uint16_t x, uint16_t y, char *p, uint8_t scale)
{
    while (x < COL && *p != '\0')
    {
        LCD_DisplayCharBig(x, y, (uint8_t)*p, scale);
        x += (uint16_t)(8U * scale);
        p++;
    }
}

/* ---- 2D drawing ---- */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color)
{
    LCD_SetAddress(x, y, x, y);
    LCD_BeginData();
    LCD_WriteDataFast((uint8_t)(color >> 8));
    LCD_WriteDataFast((uint8_t)color);
    LCD_EndData();
}

void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height)
{
    LCD_SetAddress(x, y, x, (uint16_t)(y + height - 1));
    LCD_BeginData();
    while (height--)
    {
        LCD_WriteDataFast((uint8_t)(s_Color >> 8));
        LCD_WriteDataFast((uint8_t)s_Color);
    }
    LCD_EndData();
}

void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width)
{
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1), y);
    LCD_BeginData();
    while (width--)
    {
        LCD_WriteDataFast((uint8_t)(s_Color >> 8));
        LCD_WriteDataFast((uint8_t)s_Color);
    }
    LCD_EndData();
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0;
    int16_t yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0;
    int16_t numpixels = 0, curpixel = 0;

    deltax = ABS((int16_t)x2 - (int16_t)x1);
    deltay = ABS((int16_t)y2 - (int16_t)y1);
    x = x1; y = y1;

    if (x2 >= x1) { xinc1 = 1; xinc2 = 1; } else { xinc1 = -1; xinc2 = -1; }
    if (y2 >= y1) { yinc1 = 1; yinc2 = 1; } else { yinc1 = -1; yinc2 = -1; }

    if (deltax >= deltay)
    {
        xinc1 = 0; yinc2 = 0; den = deltax; num = deltax / 2;
        numadd = deltay; numpixels = deltax;
    }
    else
    {
        xinc2 = 0; yinc1 = 0; den = deltay; num = deltay / 2;
        numadd = deltax; numpixels = deltay;
    }
    for (curpixel = 0; curpixel <= numpixels; curpixel++)
    {
        LCD_DrawPoint((uint16_t)x, (uint16_t)y, s_Color);
        num += numadd;
        if (num >= den)
        {
            num -= den;
            x += xinc1;
            y += yinc1;
        }
        x += xinc2;
        y += yinc2;
    }
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    LCD_DrawLine_H(x, y, width);
    LCD_DrawLine_H(x, (uint16_t)(y + height - 1), width);
    LCD_DrawLine_V(x, y, height);
    LCD_DrawLine_V((uint16_t)(x + width - 1), y, height);
}

void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r)
{
    int16_t Xadd = -(int16_t)r, Yadd = 0, err = 2 - 2 * (int16_t)r, e2;
    do
    {
        LCD_DrawPoint((uint16_t)(x - Xadd), (uint16_t)(y + Yadd), s_Color);
        LCD_DrawPoint((uint16_t)(x + Xadd), (uint16_t)(y + Yadd), s_Color);
        LCD_DrawPoint((uint16_t)(x + Xadd), (uint16_t)(y - Yadd), s_Color);
        LCD_DrawPoint((uint16_t)(x - Xadd), (uint16_t)(y - Yadd), s_Color);
        e2 = err;
        if (e2 <= Yadd)
        {
            Yadd++;
            err += (int16_t)(Yadd * 2 + 1);
            if (-Xadd == Yadd && e2 <= Xadd) { e2 = 0; }
        }
        if (e2 > Xadd)
        {
            Xadd++;
            err += (int16_t)(Xadd * 2 + 1);
        }
    }
    while (Xadd <= 0);
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t n = (uint32_t)width * height;
    if (n == 0U) { return; }
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1), (uint16_t)(y + height - 1));
    LCD_BeginData();
    LCD_FillBulk(s_Color, n);
    LCD_EndData();
}

void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r)
{
    int32_t  D;
    uint32_t CurX, CurY;

    D = 3 - ((int32_t)r << 1);
    CurX = 0;
    CurY = r;
    while (CurX <= CurY)
    {
        if (CurY > 0)
        {
            LCD_DrawLine_V((uint16_t)(x - CurX), (uint16_t)(y - CurY),
                           (uint16_t)(2 * CurY));
            LCD_DrawLine_V((uint16_t)(x + CurX), (uint16_t)(y - CurY),
                           (uint16_t)(2 * CurY));
        }
        if (CurX > 0)
        {
            LCD_DrawLine_V((uint16_t)(x - CurY), (uint16_t)(y - CurX),
                           (uint16_t)(2 * CurX));
            LCD_DrawLine_V((uint16_t)(x + CurY), (uint16_t)(y - CurX),
                           (uint16_t)(2 * CurX));
        }
        if (D < 0)
        {
            D += (int32_t)(CurX << 2) + 6;
        }
        else
        {
            D += (int32_t)((CurX - CurY) << 2) + 10;
            CurY--;
        }
        CurX++;
    }
    LCD_DrawCircle(x, y, r);
}

void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint16_t *data)
{
    uint32_t n = (uint32_t)width * height;
    LCD_SetAddress(x, y, (uint16_t)(x + width - 1), (uint16_t)(y + height - 1));
    LCD_BeginData();
    while (n--)
    {
        LCD_WriteDataFast((uint8_t)(*data >> 8));
        LCD_WriteDataFast((uint8_t)*data);
        data++;
    }
    LCD_EndData();
}
