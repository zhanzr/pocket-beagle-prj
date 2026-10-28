/*
  font_stb.c - TrueType text rendering via stb_truetype for the JD9851
  LCD. ASCII 0x20..0x7E glyphs are rasterized on demand per pixel size
  and cached; drawing composes alpha-blended RGB565 into a line buffer
  and blits it through the normal LCD window write.
*/
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "font_stb.h"
#include "lcd.h"

#define FONT_FIRST  0x20
#define FONT_LAST   0x7E
#define FONT_COUNT  (FONT_LAST - FONT_FIRST + 1)
#define MAX_SIZES   8

typedef struct
{
    int size;
    int ascent;
    uint8_t *bitmap[FONT_COUNT];   /* 8bpp alpha */
    int w[FONT_COUNT];
    int h[FONT_COUNT];
    int xoff[FONT_COUNT];
    int yoff[FONT_COUNT];
    int advance[FONT_COUNT];
} font_size_cache;

static stbtt_fontinfo s_font;
static unsigned char *s_ttf;
static int s_ready;
static font_size_cache s_cache[MAX_SIZES];
static int s_cache_used;

int font_stb_ready(void)
{
    return s_ready;
}

int font_stb_init(const char *ttf_path)
{
    FILE *f = fopen(ttf_path, "rb");
    long len;

    if (f == NULL)
    {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    s_ttf = malloc((size_t)len);
    if (s_ttf == NULL)
    {
        fclose(f);
        return -1;
    }
    if (fread(s_ttf, 1, (size_t)len, f) != (size_t)len)
    {
        fclose(f);
        free(s_ttf);
        return -1;
    }
    fclose(f);

    if (stbtt_InitFont(&s_font, s_ttf, 0) == 0)
    {
        free(s_ttf);
        return -1;
    }
    s_ready = 1;
    printf("[FONT] loaded %s\n", ttf_path);
    return 0;
}

static font_size_cache *get_size(int size_px)
{
    int i, ascent, descent, linegap;
    font_size_cache *c;

    for (i = 0; i < s_cache_used; i++)
    {
        if (s_cache[i].size == size_px)
        {
            return &s_cache[i];
        }
    }
    if (s_cache_used >= MAX_SIZES)
    {
        return &s_cache[0];
    }
    c = &s_cache[s_cache_used];
    memset(c, 0, sizeof(*c));

    {
        float scale = stbtt_ScaleForPixelHeight(&s_font, (float)size_px);
        stbtt_GetFontVMetrics(&s_font, &ascent, &descent, &linegap);
        c->ascent = (int)(ascent * scale);
        c->size = size_px;

        for (i = 0; i < FONT_COUNT; i++)
        {
            int cp = FONT_FIRST + i;
            int aw, lsb;

            stbtt_GetCodepointHMetrics(&s_font, cp, &aw, &lsb);
            c->advance[i] = (int)(aw * scale);

            c->bitmap[i] = stbtt_GetCodepointBitmap(&s_font, 0, scale, cp,
                                                    &c->w[i], &c->h[i],
                                                    &c->xoff[i], &c->yoff[i]);
        }
    }
    s_cache_used++;
    return c;
}

int font_stb_height(int size_px)
{
    if (!s_ready)
        return size_px;
    return get_size(size_px)->ascent;
}

int font_stb_string_width(const char *s, int size_px)
{
    font_size_cache *c;
    int w = 0;

    if (!s_ready)
        return (int)strlen(s) * size_px / 2;
    c = get_size(size_px);
    while (*s != '\0')
    {
        if (*s >= FONT_FIRST && *s <= FONT_LAST)
        {
            w += c->advance[*s - FONT_FIRST];
        }
        s++;
    }
    return w;
}

/* Blend one alpha pixel between fg and bg (0..255). */
static uint16_t blend(uint16_t fg, uint16_t bg, int a)
{
    int fr = (fg >> 11) & 0x1F, fgr = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    int br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    int r = (br * (255 - a) + fr * a) / 255;
    int g = (bgc * (255 - a) + fgr * a) / 255;
    int b = (bb * (255 - a) + fb * a) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void font_stb_draw(int x, int y, const char *s, int size_px,
                   uint16_t fg, uint16_t bg, int transparent)
{
    font_size_cache *c;
    const char *p;

    if (!s_ready)
        return;
    c = get_size(size_px);

    for (p = s; *p != '\0'; p++)
    {
        int idx = (unsigned char)*p - FONT_FIRST;
        uint8_t *bm;
        int gw, gh;
        int gx = x, gy;

        if (idx < 0 || idx >= FONT_COUNT)
        {
            x += size_px / 3;
            continue;
        }
        bm = c->bitmap[idx];
        gw = c->w[idx];
        gh = c->h[idx];
        gy = y + c->ascent + c->yoff[idx];

        if (!transparent && bm != NULL)
        {
            /* compose the whole glyph rectangle blended, one blit */
            static uint16_t line[240];
            int row;
            for (row = 0; row < gh; row++)
            {
                int col;
                if (gy + row < 0 || gy + row >= 240)
                    continue;
                for (col = 0; col < gw && gx + col < 240; col++)
                {
                    line[col] = blend(fg, bg, bm[row * gw + col]);
                }
                LCD_CopyBuffer((uint16_t)gx, (uint16_t)(gy + row),
                               (uint16_t)gw, 1, line);
            }
        }
        else if (transparent && bm != NULL)
        {
            int row, col;
            for (row = 0; row < gh; row++)
            {
                for (col = 0; col < gw; col++)
                {
                    int a = bm[row * gw + col];
                    if (a > 160 && gx + col < 240 && gy + row >= 0 &&
                        gy + row < 240)
                    {
                        LCD_DrawPoint((uint16_t)(gx + col),
                                      (uint16_t)(gy + row), fg);
                    }
                }
            }
        }

        x += c->advance[idx];
        if (x >= 240)
            break;
    }
}
