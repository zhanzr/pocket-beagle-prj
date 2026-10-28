/*
  jd9851_md140_240x240_cst816d main for the PocketBeagle (AM3358,
  Debian), ported from the nano-f411 reference. MD140 module: JD9851
  240x240 IPS over its plain DC-based 4-wire SPI on /dev/spidev0.0
  (vendor-verbatim init), plus CST816D capacitive touch on I2C2 with
  touch state printed on the console.

  PocketBeagle wiring:
    SCK=P1.08 (SPI0_CLK), MOSI=P1.12 (SPI0_MOSI), CS=P1.06 (SPI0_CS0),
    DC=P2.22, TOUCH_SCL=P1.28, TOUCH_SDA=P1.26, VCC=P1.14, GND=P1.16
  The module has no reset pin and no backlight control pin.

  NOTE: power-cycle the MODULE (unplug/replug its power) before judging
  a fix - the panel has no reset pin, so a latched bad state survives
  process restarts.
*/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lcd.h"
#include "touch.h"

#define SCREEN_W   LCD_Width     /* 240 (row buffer sizing) */
#define FPS_BAND   20            /* bottom rows reserved for the FPS text   */
#define BACK_COLOR LCD_BLACK
#define INFO_DY    13            /* info page line pitch                   */
#define MAX_SECONDS 60           /* demo cap                               */

volatile sig_atomic_t lcd_running = 1;
static volatile sig_atomic_t running = 1;

static void stop_handler(int sig)
{
    (void)sig;
    running = 0;
    lcd_running = 0;
}

static unsigned long tick_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

/* --------------------------------------------------------------------- */
/* Touch printout: polls the CST816D and prints state/X/Y (on touch-down,
 * and on release).                                                      */
static uint8_t s_touch_down;

static void touch_task(void)
{
    uint8_t buf[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint16_t x, y;

    Touch_Read(buf, 8);

    if (buf[3] == 0x80U && buf[4] > 1U)
    {
        x = buf[4];
        y = (uint16_t)(((buf[5] & 0x0FU) << 8) | buf[6]);

        if (s_touch_down == 0U)
        {
            printf("[TOUCH] down X=%u Y=%u\r\n",
                   (unsigned)x, (unsigned)y);
            s_touch_down = 1U;
        }
    }
    else if (s_touch_down != 0U)
    {
        printf("[TOUCH] release\r\n");
        s_touch_down = 0U;
    }
}

/* Runtime window geometry (follows LCD_SetWindow). */
static uint16_t anim_h(void)
{
    return (uint16_t)(LCD_H() - FPS_BAND);
}

/* --------------------------------------------------------------------- */
/* FPS counter.                                                          */
static volatile uint32_t g_frames;
static uint32_t         g_last_frames;
static unsigned long    g_fps_last_tick;
static uint32_t         g_fps_color = LCD_WHITE;   /* FPS glyph color     */

static void fps_frame(void)
{
    g_frames++;
}

static void fps_update(void)
{
    unsigned long now = tick_ms();
    if (now - g_fps_last_tick >= 1000)
    {
        uint32_t fps = g_frames - g_last_frames;
        g_last_frames = g_frames;
        g_fps_last_tick = now;

        char buf[8];
        buf[0] = 'F'; buf[1] = 'P'; buf[2] = 'S'; buf[3] = ':';
        buf[4] = (char)('0' + (fps / 100) % 10);
        buf[5] = (char)('0' + (fps / 10) % 10);
        buf[6] = (char)('0' + fps % 10);
        buf[7] = '\0';
        LCD_SetColor(g_fps_color);
        LCD_ShowTransparent(1);              /* no opaque box */
        LCD_DisplayString(1, anim_h() + 4, buf);
        LCD_ShowTransparent(0);
    }
}

static void paint_fps_band(void)
{
    LCD_SetColor(BACK_COLOR);
    LCD_SetBackColor(BACK_COLOR);
    LCD_FillRect(0, anim_h(), LCD_W(), FPS_BAND);
}

/* --------------------------------------------------------------------- */
/* Animated gradient: hue sweeps the full color wheel over `ms`.         */
static uint32_t hsv_to_rgb(int h, int s, int v)
{
    int region = (h / 600) % 6;
    int fpart  = h % 600;
    int p = v * (255 - s) / 255;
    int q = v * (255 - (s * fpart) / 600) / 255;
    int t = v * (255 - (s * (600 - fpart)) / 600) / 255;
    int r, g, b;
    switch (region)
    {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default:r = v; g = p; b = q; break;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void draw_gradient(int hue_a, int hue_b, uint16_t *row)
{
    for (int y = 0; y < anim_h() && lcd_running; y++)
    {
        int frac = y * 1000 / anim_h();
        int hue  = hue_a + (hue_b - hue_a) * frac / 1000;
        uint32_t c = hsv_to_rgb(hue, 255, 255);
        uint16_t rgb565 = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) |
                                     ((c >> 3) & 0x001F));
        for (int x = 0; x < LCD_W(); x++)
        {
            row[x] = rgb565;
        }
        LCD_CopyBuffer(0, (uint16_t)y, LCD_W(), 1, row);
    }
}

static void gradient_demo(uint32_t ms)
{
    static uint16_t row[SCREEN_W];
    LCD_SetBackColor(BACK_COLOR);
    paint_fps_band();

    unsigned long start = tick_ms();
    unsigned long t = 0;
    do
    {
        int hue_a = (int)(t * 3600 / (int)ms);
        int hue_b = hue_a + 1800;
        if (hue_b >= 3600) { hue_b -= 3600; }
        draw_gradient(hue_a, hue_b, row);
        fps_frame();
        fps_update();
        t = tick_ms() - start;
    } while (lcd_running && t < ms);
}

/* --------------------------------------------------------------------- */
/* Vendor TEST_STAND screens (240x240). The five solid-color fills are
 * timed (ms) - the durations belong to the current driving method only:
 * run_patterns() resets them each pass, before the fills run.          */
static uint32_t g_solid_ms[5];
static const char *const g_solid_name[5] =
{
    "RED", "GREEN", "BLUE", "WHITE", "BLACK"
};

/* kHz -> "16 MHz" / "2.1 MHz" text (shared by console + info page). */
static const char *mhz_text(unsigned long khz)
{
    static char t[16];
    if (khz % 1000UL == 0UL)
    {
        snprintf(t, sizeof t, "%lu MHz", khz / 1000UL);
    }
    else
    {
        snprintf(t, sizeof t, "%lu.%lu MHz",
                 khz / 1000UL, (khz % 1000UL) / 100UL);
    }
    return t;
}

static void TEST_STAND(void)
{
    const uint32_t solid_color[5] = { RED, GREEN, BLUE, WHITE, BLACK };

    DispFrame();
    StopDelay(Delay_Time);

    DispGrayHor16();
    StopDelay(Delay_Time);

    DispBand();
    StopDelay(Delay_Time);

    for (int i = 0; i < 5 && lcd_running; i++)
    {
        unsigned long t0 = tick_ms();
        DispColor(solid_color[i]);
        g_solid_ms[i] = tick_ms() - t0;
        StopDelay(Delay_Time);
    }

    printf("[LCD] solid fills (ms): RED=%lu GREEN=%lu BLUE=%lu "
           "WHITE=%lu BLACK=%lu\r\n",
           (unsigned long)g_solid_ms[0], (unsigned long)g_solid_ms[1],
           (unsigned long)g_solid_ms[2], (unsigned long)g_solid_ms[3],
           (unsigned long)g_solid_ms[4]);
}

/* --------------------------------------------------------------------- */
/* Info page: compiler, build date, drive method, the IO map. `invert`
 * swaps fg/bg (white background page).                                 */
static void info_demo(uint32_t ms, uint8_t invert)
{
    char buf[32];
    char comp[24];
    uint32_t fg = invert ? LCD_BLACK : LCD_WHITE;
    uint32_t bg = invert ? LCD_WHITE : LCD_BLACK;

#if defined(__GNUC__)
    snprintf(comp, sizeof comp, "GCC %d.%d.%d",
             __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    snprintf(comp, sizeof comp, "unknown");
#endif

    printf("[LCD] info%s: compiler=%s build=%s %s\r\n",
           invert ? " (inverted)" : "", comp, __DATE__, __TIME__);
    printf("[LCD] info: drive=HW SPI0 %s\r\n", mhz_text(lcd_kHz()));

    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_Clear();

    /* FPS band in the page background color, then restore fg/bg. */
    LCD_SetColor(bg);
    LCD_SetBackColor(bg);
    LCD_FillRect(0, anim_h(), LCD_W(), FPS_BAND);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    g_fps_color = fg;                     /* FPS glyph matches the page */

    const int ix = 2;
    int y = 2;
    snprintf(buf, sizeof buf, "%s", comp);
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "Build %s", __DATE__);
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "HW SPI0 %s", mhz_text(lcd_kHz()));
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "SCK=P1.08 SD=P1.12");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "CS=P1.06 DC=P2.22");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    snprintf(buf, sizeof buf, "TOUCH P1.28/P1.26");
    LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;

    /* Solid-color fill durations, measured earlier in this pass (the
     * TEST_STAND solids run before the info pages) - so they always
     * reflect the current driving method. */
    for (int i = 0; i < 5; i++)
    {
        snprintf(buf, sizeof buf, "%-5s : %4lu ms",
                 g_solid_name[i], (unsigned long)g_solid_ms[i]);
        LCD_DisplayString(ix, (uint16_t)y, buf);  y += INFO_DY;
    }

    g_fps_color = LCD_WHITE;              /* restore default FPS glyph color */

    unsigned long start = tick_ms();
    do
    {
        fps_update();
        touch_task();                     /* touch printout during waits */
        lcd_delay_ms(50);
    } while (lcd_running && tick_ms() - start < ms);
}

/* --------------------------------------------------------------------- */
/* Big-font banner page (8x16 font), lines centered on the panel width.  */
static void banner_page(const char *l1, const char *l2,
                        uint32_t fg, uint32_t bg, uint32_t ms)
{
    g_fps_color = fg;

    LCD_SetAsciiFont(&ASCII_Font16);      /* bigger than the normal 6x12  */
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);
    LCD_Clear();
    LCD_SetColor(bg);
    LCD_SetBackColor(bg);
    LCD_FillRect(0, anim_h(), LCD_W(), FPS_BAND);
    LCD_SetColor(fg);
    LCD_SetBackColor(bg);

    /* 8 px/glyph: center each line dynamically. */
    LCD_DisplayString((uint16_t)((LCD_W() - (int)strlen(l1) * 8) / 2), 40,
                      (char *)l1);
    LCD_DisplayString((uint16_t)((LCD_W() - (int)strlen(l2) * 8) / 2), 64,
                      (char *)l2);

    unsigned long start = tick_ms();
    do
    {
        fps_update();
        touch_task();
        lcd_delay_ms(50);
    } while (lcd_running && tick_ms() - start < ms);

    LCD_SetAsciiFont(&ASCII_Font12);      /* back to the normal font      */
    g_fps_color = LCD_WHITE;
}

/* --------------------------------------------------------------------- */
/* Full test-pattern set. The solid-color fills run FIRST (they are timed
 * for the current driving method), then the two info pages display the
 * measured durations.                                                   */
static void run_patterns(void)
{
    printf("[LCD] phase: TEST_STAND\r\n");
    memset(g_solid_ms, 0, sizeof g_solid_ms);   /* current method only */
    TEST_STAND();

    if (!lcd_running) return;
    printf("[LCD] phase: info\r\n");
    info_demo(5000, 0);

    if (!lcd_running) return;
    printf("[LCD] phase: info (inverted colors)\r\n");
    info_demo(5000, 1);

    if (!lcd_running) return;
    printf("[LCD] phase: gradient\r\n");
    gradient_demo(4000);
}

/* --------------------------------------------------------------------- */
int main(void)
{
    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

    printf("\r\n==== PocketBeagle (AM3358) jd9851_md140_240x240_cst816d ====\r\n");
    printf("JD9851 1.4\" 240x240 (DC-based 4-wire SPI, MADCTL 0x00):\r\n");
    printf("SCK=P1.08 MOSI=P1.12 CS=P1.06 DC=P2.22 (spidev0.0)\r\n");
    printf("TOUCH: CST816D I2C2 SCL=P1.28 SDA=P1.26\r\n");

    if (Touch_Init() != 0)
    {
        printf("[TOUCH] init failed - continuing without touch\r\n");
    }
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font12);
    paint_fps_band();

    unsigned long start = tick_ms();
    while (running)
    {
        printf("[LCD] phase: HARDWARE banner\r\n");
        LCD_Reinit();         /* re-frame the panel */
        banner_page("JD9851", "HW SPI0 test",
                    LCD_BLACK, LCD_CYAN, 3000);

        printf("[LCD] running patterns on HARDWARE SPI0 @ %s\r\n",
               mhz_text(lcd_kHz()));
        run_patterns();

        if (tick_ms() - start > MAX_SECONDS * 1000UL)
        {
            printf("%d s cap reached\r\n", MAX_SECONDS);
            break;
        }
    }

    printf("bye\r\n");
    return 0;
}
