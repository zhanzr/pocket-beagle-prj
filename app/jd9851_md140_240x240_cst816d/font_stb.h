/*
  font_stb.h - TrueType text rendering for the JD9851 LCD (stb_truetype,
  single header, no external libs). The TTF is loaded at runtime from the
  board's filesystem (Debian package fonts-dejavu-core).
*/
#ifndef __FONT_STB_H
#define __FONT_STB_H

#include <stdint.h>

/* Load a TTF and prepare glyph caches. Returns 0 on success.
 * Call once before font_stb_draw(). */
int  font_stb_init(const char *ttf_path);
int  font_stb_ready(void);

/* Pixel height (ascent) of a rendered string at size_px. */
int  font_stb_height(int size_px);
/* Advance width in pixels of a whole ASCII string at size_px. */
int  font_stb_string_width(const char *s, int size_px);

/* Draw an ASCII string with the top-left corner at (x, y), glyph pixel
 * height size_px, alpha-blended between fg and bg. With transparent != 0
 * only foreground pixels are drawn (background untouched). */
void font_stb_draw(int x, int y, const char *s, int size_px,
                   uint16_t fg, uint16_t bg, int transparent);

#endif /* __FONT_STB_H */
