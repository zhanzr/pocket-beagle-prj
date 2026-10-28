/*
  lcd.h - JD9851 1.4" 240x240 LCD driver for the PocketBeagle (MD140
  module), ported from the nano-f411 reference
  (f4-demo/nano-f411/app/jd9851_md140_240x240_cst816d).

  Panel: plain DC-based 4-wire SPI - command byte with DC low, data
  bytes with DC high. No MISO, no reset pin, no backlight pin on this
  module's connector (backlight hardwired on-module).

  PocketBeagle wiring (this port):
    SCK  -> P1.08 (SPI0_CLK)   /dev/spidev0.0, mode 3, MSB first
    MOSI -> P1.12 (SPI0_MOSI)
    CS   -> P1.06 (SPI0_CS0, hardware, toggled by the spidev driver)
    DC   -> P2.22 (GPIO1_14, gpiod line name "P2.22")
    VDD  -> P1.14 (3.3 V), GND -> P1.16
    Touch: CST816D on I2C2 (P1.28 = SCL, P1.26 = SDA), 7-bit addr 0x15

  Panel geometry: 240x240, windows at COL_Pre = 0, ROW_Pre = 0 (full
  range addressed directly), MADCTL 0x00, 16bpp, IPS inversion on.
  Vendor example: STM32_LCD_TK014F1828_1246_hardSPI_hal_captouch.
*/

#ifndef __LCD_H
#define __LCD_H

#include <signal.h>
#include <stdint.h>

#include "lcd_fonts.h"
#include "lcd_font_1608.h"

/* ---- shared stop flag (set to 0 by the signal handler in main) ---- */
extern volatile sig_atomic_t lcd_running;

/* ---- Panel geometry / demo timing ---- */
#define LCD_Width    240
#define LCD_Height   240
#define COL       240
#define ROW       240
#define COL_Pre   0
#define ROW_Pre   0
#define Delay_Time 500

/* ---- runtime drawing window ----
 * All drawing is relative to this window; it defaults to the full panel
 * and can be shrunk at runtime. */
void LCD_SetWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void LCD_ResetWindow(void);   /* back to the full panel */
uint16_t LCD_W(void);         /* current window width  */
uint16_t LCD_H(void);         /* current window height */

/* ---- raw RGB565 colors (vendor screens use these directly) ---- */
#define WHITE   0xFFFF
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define MAGENTA 0xF81F
#define GREEN   0x07E0
#define CYAN    0x7FFF
#define YELLOW  0xFFE0

/* ---- 24-bit colors (RGB888, converted to RGB565 by LCD_SetColor) ---- */
#define LCD_WHITE       0xFFFFFF
#define LCD_BLACK       0x000000
#define LCD_BLUE        0x0000FF
#define LCD_GREEN       0x00FF00
#define LCD_RED         0xFF0000
#define LCD_CYAN        0x00FFFF
#define LCD_MAGENTA     0xFF00FF
#define LCD_YELLOW      0xFFFF00

#define ABS(X)  ((X) > 0 ? (X) : -(X))

/* ---- API: init / window / colors ---- */
void LCD_GPIOInit(void);
void LCD_RESET(void);
void LCD_IC_Init(void);
void LCD_Init(void);
void LCD_Reinit(void);   /* re-init after a latched state */
void LCD_SetAddress(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_SetColor(uint32_t rbg888);
void LCD_SetBackColor(uint32_t rbg888);
void LCD_Clear(void);
void LCD_ClearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

/* ---- API: vendor screen-window helper (raw, absolute coords) ---- */
void BlockWrite(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend);

/* ---- API: vendor demo screens ---- */
void DispColor(uint32_t color);
void DispFrame(void);
void DispGrayHor16(void);
void DispBand(void);
void StopDelay(uint16_t ms);

/* ---- API: ASCII text ---- */
void LCD_SetAsciiFont(pFONT *font);
void LCD_ShowTransparent(uint8_t mode);
void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t c);
void LCD_DisplayString(uint16_t x, uint16_t y, char *p);

/* ---- API: 2D drawing ---- */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint32_t color);
void LCD_DrawLine_V(uint16_t x, uint16_t y, uint16_t height);
void LCD_DrawLine_H(uint16_t x, uint16_t y, uint16_t width);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void LCD_FillCircle(uint16_t x, uint16_t y, uint16_t r);

/* ---- API: raw buffer blit (RGB565 words) ---- */
void LCD_CopyBuffer(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                    uint16_t *data);

/* ---- port helpers ---- */
void lcd_delay_ms(long ms);          /* interruptible sleep             */
unsigned long lcd_kHz(void);         /* active SPI clock in kHz         */

#endif /* __LCD_H */
