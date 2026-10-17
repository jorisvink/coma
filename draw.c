/*
 * Copyright (c) 2019-2026 Joris Vink <joris@sanctorum.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <SDL.h>
#include <SDL_ttf.h>
#include <fontconfig/fontconfig.h>

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "coma.h"

#define DRAW_UI			COMA_DRAW_UI
#define DRAW_TERM		COMA_DRAW_TERM
#define DRAW_TERM_BOLD		COMA_DRAW_TERM_BOLD
#define DRAW_FALLBACK		3
#define DRAW_FONT_MAX		4

#define GLYPH_BUCKETS		1024
#define GLYPH_MAX		8192

struct font {
	TTF_Font		*ttf;
	int			bold;
	int			scale;
	float			ascent;
};

struct glyph {
	u_int32_t		cp;
	int			style;
	int			missing;

	float			w;
	float			h;
	float			minx;
	float			yoff;
	float			advance;

	SDL_Texture		*tex;
	struct glyph		*next;
};

static int		draw_scale_get(void);
static void		draw_fonts_close(void);
static void		draw_glyph_flush(void);
static struct glyph	*draw_glyph_get(int, u_int32_t);
static int		draw_font_open(struct font *, const char *);
static void		draw_glyph_copy(struct glyph *, float, float,
			    const struct color *);

static u_int16_t	cell_w = 0;
static u_int16_t	cell_h = 0;
static int		draw_scale = 1;
static int		glyph_count = 0;
static SDL_Renderer	*renderer = NULL;
static struct font	fonts[DRAW_FONT_MAX];
static struct glyph	*glyphs[GLYPH_BUCKETS];

void
coma_draw_init(void *r)
{
	renderer = r;

	if (TTF_Init() == -1)
		fatal("TTF_Init: %s", TTF_GetError());

	if (!FcInit())
		fatal("FcInit failed");

	memset(fonts, 0, sizeof(fonts));
	memset(glyphs, 0, sizeof(glyphs));

	draw_scale = draw_scale_get();
	SDL_RenderSetScale(renderer, draw_scale, draw_scale);

	coma_log("display scale is %d", draw_scale);
}

int
coma_draw_rescale(void)
{
	int		scale;

	if ((scale = draw_scale_get()) == draw_scale)
		return (0);

	draw_scale = scale;
	SDL_RenderSetScale(renderer, draw_scale, draw_scale);

	coma_draw_fonts();
	coma_log("display scale is %d", draw_scale);

	return (1);
}

void
coma_draw_fonts(void)
{
	char		spec[64];
	int		len, scale, advance;

	draw_glyph_flush();
	draw_fonts_close();

	if (draw_font_open(&fonts[DRAW_UI], font_name) == -1) {
		coma_log("failed to open %s, falling back to default",
		    font_name);
		if (draw_font_open(&fonts[DRAW_UI], COMA_WM_FONT) == -1)
			fatal("failed to open %s", COMA_WM_FONT);
	}

	if (draw_font_open(&fonts[DRAW_TERM], term_font_name) == -1) {
		coma_log("failed to open %s, falling back to default",
		    term_font_name);
		if (draw_font_open(&fonts[DRAW_TERM], COMA_TERM_FONT) == -1)
			fatal("failed to open %s", COMA_TERM_FONT);
	}

	if (term_font_bold_name == NULL ||
	    draw_font_open(&fonts[DRAW_TERM_BOLD], term_font_bold_name) == -1)
		fonts[DRAW_TERM_BOLD].ttf = NULL;

	if (TTF_GlyphMetrics32(fonts[DRAW_TERM].ttf, 'M',
	    NULL, NULL, NULL, NULL, &advance) == -1)
		fatal("cannot determine terminal font cell width");

	scale = fonts[DRAW_TERM].scale;
	cell_w = (advance + scale - 1) / scale;
	cell_h = (TTF_FontHeight(fonts[DRAW_TERM].ttf) + scale - 1) / scale;

	if (cell_w == 0 || cell_h == 0)
		fatal("terminal font has no usable cell size");

	len = snprintf(spec, sizeof(spec), "monospace:pixelsize=%u", cell_h);
	if (len < 0 || (size_t)len >= sizeof(spec))
		fatal("failed to construct fallback font name");

	if (draw_font_open(&fonts[DRAW_FALLBACK], spec) == -1)
		fonts[DRAW_FALLBACK].ttf = NULL;

	coma_log("terminal cell size is %ux%u", cell_w, cell_h);
}

void
coma_draw_cleanup(void)
{
	draw_glyph_flush();
	draw_fonts_close();

	TTF_Quit();
}

void
coma_draw_cell_size(u_int16_t *w, u_int16_t *h)
{
	*w = cell_w;
	*h = cell_h;
}

void
coma_draw_clip(int x, int y, int w, int h)
{
	SDL_Rect	rect;

	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;

	SDL_RenderSetClipRect(renderer, &rect);
}

void
coma_draw_unclip(void)
{
	SDL_RenderSetClipRect(renderer, NULL);
}

void
coma_draw_fill(int x, int y, int w, int h, const struct color *color)
{
	SDL_Rect	rect;

	if (w <= 0 || h <= 0)
		return;

	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;

	SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, 0xff);
	SDL_RenderFillRect(renderer, &rect);
}

void
coma_draw_box(int x, int y, int w, int h, int bw, const struct color *color)
{
	if (bw <= 0)
		return;

	coma_draw_fill(x, y, w, bw, color);
	coma_draw_fill(x, y + h - bw, w, bw, color);
	coma_draw_fill(x, y + bw, bw, h - (bw * 2), color);
	coma_draw_fill(x + w - bw, y + bw, bw, h - (bw * 2), color);
}

int
coma_draw_text(int x, int baseline, const struct color *color,
    const char *str, size_t len)
{
	size_t		n;
	u_int32_t	cp;
	float		y, pos;
	struct glyph	*glyph;

	pos = x;
	y = baseline - fonts[DRAW_UI].ascent;

	while (len > 0) {
		if ((n = coma_utf8_decode(str, &cp)) == 0 || n > len)
			break;

		str += n;
		len -= n;

		if ((glyph = draw_glyph_get(DRAW_UI, cp)) == NULL)
			continue;

		draw_glyph_copy(glyph, pos, y, color);
		pos += glyph->advance;
	}

	return ((int)(pos - x + 0.5f));
}

int
coma_draw_text_width(const char *str, size_t len)
{
	size_t		n;
	u_int32_t	cp;
	float		width;
	struct glyph	*glyph;

	width = 0;

	while (len > 0) {
		if ((n = coma_utf8_decode(str, &cp)) == 0 || n > len)
			break;

		str += n;
		len -= n;

		if ((glyph = draw_glyph_get(DRAW_UI, cp)) != NULL)
			width += glyph->advance;
	}

	return ((int)(width + 0.5f));
}

void
coma_draw_glyph(int style, int x, int y, u_int32_t cp,
    const struct color *color)
{
	struct glyph	*glyph;

	if ((glyph = draw_glyph_get(style, cp)) == NULL)
		return;

	draw_glyph_copy(glyph, x, y, color);
}

void
coma_draw_mark(int style, int x, int y, int width, u_int32_t cp,
    const struct color *color)
{
	float		pos;
	struct glyph	*glyph;

	if ((glyph = draw_glyph_get(style, cp)) == NULL || glyph->missing)
		return;

	if (glyph->advance > 0)
		pos = x;
	else
		pos = x + width;

	if (glyph->minx < 0)
		pos += glyph->minx;

	draw_glyph_copy(glyph, pos, y, color);
}

static struct glyph *
draw_glyph_get(int style, u_int32_t cp)
{
	SDL_Color	white;
	SDL_Surface	*surf;
	struct font	*font;
	struct glyph	*glyph;
	int		minx, bucket, advance, synth;

	bucket = (cp ^ (style << 8)) % GLYPH_BUCKETS;

	for (glyph = glyphs[bucket]; glyph != NULL; glyph = glyph->next) {
		if (glyph->cp == cp && glyph->style == style)
			return (glyph);
	}

	synth = 0;

	switch (style) {
	case DRAW_TERM_BOLD:
		if (fonts[DRAW_TERM_BOLD].ttf != NULL) {
			font = &fonts[DRAW_TERM_BOLD];
		} else {
			synth = 1;
			font = &fonts[DRAW_TERM];
		}
		break;
	case DRAW_TERM:
		font = &fonts[DRAW_TERM];
		break;
	case DRAW_UI:
	default:
		font = &fonts[DRAW_UI];
		break;
	}

	if (!TTF_GlyphIsProvided32(font->ttf, cp) &&
	    fonts[DRAW_FALLBACK].ttf != NULL &&
	    TTF_GlyphIsProvided32(fonts[DRAW_FALLBACK].ttf, cp))
		font = &fonts[DRAW_FALLBACK];

	if (TTF_GlyphMetrics32(font->ttf, cp,
	    &minx, NULL, NULL, NULL, &advance) == -1)
		return (NULL);

	white.r = 0xff;
	white.g = 0xff;
	white.b = 0xff;
	white.a = 0xff;

	if (synth || font->bold)
		TTF_SetFontStyle(font->ttf, TTF_STYLE_BOLD);

	surf = TTF_RenderGlyph32_Blended(font->ttf, cp, white);

	if (synth || font->bold)
		TTF_SetFontStyle(font->ttf, TTF_STYLE_NORMAL);

	if (surf == NULL)
		return (NULL);

	if (glyph_count >= GLYPH_MAX)
		draw_glyph_flush();

	glyph_count++;
	glyph = coma_calloc(1, sizeof(*glyph));

	glyph->cp = cp;
	glyph->style = style;
	glyph->w = (float)surf->w / font->scale;
	glyph->h = (float)surf->h / font->scale;
	glyph->minx = (float)minx / font->scale;
	glyph->advance = (float)advance / font->scale;
	glyph->missing = !TTF_GlyphIsProvided32(font->ttf, cp);

	if (style == DRAW_UI)
		glyph->yoff = fonts[DRAW_UI].ascent - font->ascent;
	else
		glyph->yoff = fonts[DRAW_TERM].ascent - font->ascent;

	if ((glyph->tex = SDL_CreateTextureFromSurface(renderer,
	    surf)) == NULL)
		fatal("SDL_CreateTextureFromSurface: %s", SDL_GetError());

	SDL_SetTextureBlendMode(glyph->tex, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(glyph->tex, SDL_ScaleModeNearest);
	SDL_FreeSurface(surf);

	glyph->next = glyphs[bucket];
	glyphs[bucket] = glyph;

	return (glyph);
}

static void
draw_glyph_copy(struct glyph *glyph, float x, float y,
    const struct color *color)
{
	SDL_FRect	dst;

	dst.x = x;
	dst.w = glyph->w;
	dst.h = glyph->h;
	dst.y = y + glyph->yoff;

	SDL_SetTextureColorMod(glyph->tex, color->r, color->g, color->b);
	SDL_RenderCopyF(renderer, glyph->tex, NULL, &dst);
}

static void
draw_glyph_flush(void)
{
	int		i;
	struct glyph	*glyph, *next;

	for (i = 0; i < GLYPH_BUCKETS; i++) {
		for (glyph = glyphs[i]; glyph != NULL; glyph = next) {
			next = glyph->next;
			SDL_DestroyTexture(glyph->tex);
			free(glyph);
		}
		glyphs[i] = NULL;
	}

	glyph_count = 0;
}

static void
draw_fonts_close(void)
{
	int		i;

	for (i = 0; i < DRAW_FONT_MAX; i++) {
		if (fonts[i].ttf != NULL)
			TTF_CloseFont(fonts[i].ttf);
		fonts[i].ttf = NULL;
	}
}

static int
draw_scale_get(void)
{
	SDL_Window	*win;
	int		scale, pw, ww;

	if ((win = SDL_RenderGetWindow(renderer)) == NULL)
		return (1);

	SDL_GetWindowSize(win, &ww, NULL);

	if (SDL_GetRendererOutputSize(renderer, &pw, NULL) != 0 || ww <= 0)
		return (1);

	if ((scale = pw / ww) < 1)
		scale = 1;

	return (scale);
}

static int
draw_font_open(struct font *font, const char *spec)
{
	FcResult	res;
	FILE		*fp;
	SDL_RWops	*rw;
	FcChar8		*fcfile;
	FcBool		embolden;
	double		pixelsize;
	FcPattern	*pat, *match;
	char		*p, *ep, path[PATH_MAX];
	int		fd, len, index, size, height;

	memset(font, 0, sizeof(*font));

	index = 0;
	size = 13;
	embolden = FcFalse;

	if (spec[0] == '/' || spec[0] == '~') {
		if (spec[0] == '~') {
			len = snprintf(path, sizeof(path), "%s%s",
			    homedir, spec + 1);
		} else {
			len = snprintf(path, sizeof(path), "%s", spec);
		}

		if (len < 0 || (size_t)len >= sizeof(path))
			return (-1);

		if ((p = strrchr(path, ':')) != NULL) {
			*(p)++ = '\0';
			errno = 0;
			size = strtol(p, &ep, 10);
			if (errno != 0 || *ep != '\0' || size <= 0)
				return (-1);
		}
	} else {
		if ((pat = FcNameParse((const FcChar8 *)spec)) == NULL)
			return (-1);

		FcConfigSubstitute(NULL, pat, FcMatchPattern);
		FcDefaultSubstitute(pat);

		if ((match = FcFontMatch(NULL, pat, &res)) == NULL) {
			FcPatternDestroy(pat);
			return (-1);
		}

		if (FcPatternGetString(match, FC_FILE, 0,
		    &fcfile) != FcResultMatch) {
			FcPatternDestroy(match);
			FcPatternDestroy(pat);
			return (-1);
		}

		if (FcPatternGetInteger(match, FC_INDEX, 0,
		    &index) != FcResultMatch)
			index = 0;

		if (FcPatternGetDouble(match, FC_PIXEL_SIZE, 0,
		    &pixelsize) == FcResultMatch ||
		    FcPatternGetDouble(pat, FC_PIXEL_SIZE, 0,
		    &pixelsize) == FcResultMatch)
			size = (int)(pixelsize + 0.5);

		if (FcPatternGetBool(match, FC_EMBOLDEN, 0,
		    &embolden) != FcResultMatch)
			embolden = FcFalse;

		len = snprintf(path, sizeof(path), "%s", (const char *)fcfile);

		FcPatternDestroy(match);
		FcPatternDestroy(pat);

		if (len < 0 || (size_t)len >= sizeof(path))
			return (-1);
	}

	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) == -1) {
		coma_log("open(%s): %s", path, errno_s);
		return (-1);
	}

	if ((fp = fdopen(fd, "rb")) == NULL) {
		coma_log("fdopen(%s): %s", path, errno_s);
		(void)close(fd);
		return (-1);
	}

	if ((rw = SDL_RWFromFP(fp, SDL_TRUE)) == NULL) {
		coma_log("SDL_RWFromFP(%s): %s", path, SDL_GetError());
		(void)fclose(fp);
		return (-1);
	}

	if ((font->ttf = TTF_OpenFontIndexRW(rw, 1, size, index)) == NULL) {
		coma_log("TTF_OpenFont(%s): %s", path, TTF_GetError());
		return (-1);
	}

	font->scale = 1;
	height = TTF_FontHeight(font->ttf);

	if (draw_scale > 1 &&
	    TTF_SetFontSize(font->ttf, size * draw_scale) == 0) {
		if (TTF_FontHeight(font->ttf) > height)
			font->scale = draw_scale;
		else
			(void)TTF_SetFontSize(font->ttf, size);
	}

	font->bold = (embolden == FcTrue);
	font->ascent = (float)TTF_FontAscent(font->ttf) / font->scale;

	coma_log("font '%s' -> %s (%dpx, scale %d%s)", spec, path, size,
	    font->scale, font->bold ? ", synthetic bold" : "");

	return (0);
}
