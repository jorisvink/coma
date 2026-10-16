/*
 * MIT/X Consortium License
 *
 * © 2014-2022 Hiltjo Posthuma <hiltjo at codemadness dot org>
 * © 2018 Devin J. Pohly <djpohly at gmail dot com>
 * © 2014-2017 Quentin Rameau <quinq at fifth dot space>
 * © 2009-2012 Aurélien APTEL <aurelien dot aptel at gmail dot com>
 * © 2008-2017 Anselm R Garbe <garbeam at gmail dot com>
 * © 2012-2017 Roberto E. Vargas Caballero <k0ga at shike2 dot com>
 * © 2012-2016 Christoph Lohmann <20h at r-36 dot net>
 * © 2013 Eon S. Jeon <esjeon at hyunmu dot am>
 * © 2013 Alexander Sedov <alex0player at gmail dot com>
 * © 2013 Mark Edgar <medgar123 at gmail dot com>
 * © 2013-2014 Eric Pruitt <eric.pruitt at gmail dot com>
 * © 2013 Michael Forney <mforney at mforney dot org>
 * © 2013-2014 Markus Teich <markus dot teich at stusta dot mhn dot de>
 * © 2014-2015 Laslo Hunhold <dev at frign dot de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Hacked up st terminal code so it fits in coma.
 */

#include <sys/types.h>

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "coma.h"

/* Arbitrary sizes */
#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4
#define ESC_BUF_SIZ   (128*UTF_SIZ)
#define ESC_ARG_SIZ   16
#define STR_BUF_SIZ   ESC_BUF_SIZ
#define STR_BUF_MAX   (64*1024)
#define STR_ARG_SIZ   ESC_ARG_SIZ

#define TAB_SPACES    8
#define VT_IDEN       "\033[?6c"

/* macros */
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))
#define TRUECOLOR(r,g,b)	(COMA_VT_TRUECOLOR | (r) << 16 | (g) << 8 | (b))

#define IS_SET(flag)		((term->mode & (flag)) != 0)
#define IS_WSET(flag)		((term->wmode & (flag)) != 0)
#define ISCONTROLC0(c)		(BETWEEN(c, 0, 0x1f) || (c) == 0x7f)
#define ISCONTROLC1(c)		(BETWEEN(c, 0x80, 0x9f))
#define ISCONTROL(c)		(ISCONTROLC0(c) || ISCONTROLC1(c))

enum term_mode {
	MODE_WRAP        = 1 << 0,
	MODE_INSERT      = 1 << 1,
	MODE_ALTSCREEN   = 1 << 2,
	MODE_CRLF        = 1 << 3,
	MODE_UTF8        = 1 << 6,
};

enum cursor_movement {
	CURSOR_SAVE,
	CURSOR_LOAD
};

enum cursor_state {
	CURSOR_DEFAULT  = 0,
	CURSOR_WRAPNEXT = 1,
	CURSOR_ORIGIN   = 2
};

enum charset {
	CS_GRAPHIC0,
	CS_GRAPHIC1,
	CS_UK,
	CS_USA,
	CS_MULTI,
	CS_GER,
	CS_FIN
};

enum escape_state {
	ESC_START      = 1,
	ESC_CSI        = 2,
	ESC_STR        = 4,  /* DCS, OSC, PM, APC */
	ESC_ALTCHARSET = 8,
	ESC_STR_END    = 16, /* a final string was encountered */
	ESC_TEST       = 32, /* Enter in test mode */
	ESC_UTF8       = 64,
	ESC_IGNORE     = 128, /* unsupported intermediate */
};

typedef unsigned char uchar;
typedef unsigned int uint;
typedef u_int32_t Rune;
typedef struct vt_cell Glyph;
typedef Glyph *Line;

typedef struct {
	Glyph attr; /* current char attributes */
	int x;
	int y;
	char state;
	char trantbl[4]; /* saved charset table */
	int charset;     /* saved charset */
} TCursor;

/* CSI Escape sequence structs */
/* ESC '[' [[ [<priv>] <arg> [;]] <mode> [<mode>]] */
typedef struct {
	char buf[ESC_BUF_SIZ]; /* raw string */
	size_t len;            /* raw string length */
	char priv;
	int arg[ESC_ARG_SIZ];
	char sub[ESC_ARG_SIZ];
	int narg;              /* nb of args */
	char mode[2];
} CSIEscape;

/* STR Escape sequence structs */
/* ESC type [[ [<priv>] <arg> [;]] <mode>] ESC '\' */
typedef struct {
	char type;             /* ESC type ... */
	char *buf;             /* allocated raw string */
	size_t siz;            /* allocation size */
	size_t len;            /* raw string length */
	char *args[STR_ARG_SIZ];
	int narg;              /* nb of args */
	char bel;              /* terminated by BEL */
} STREscape;

struct vt {
	int row;      /* nb row */
	int col;      /* nb col */
	Line *line;   /* screen */
	Line *alt;    /* alternate screen */
	TCursor c;    /* cursor */
	int top;      /* top    scroll limit */
	int bot;      /* bottom scroll limit */
	int mode;     /* terminal mode flags */
	int esc;      /* escape state flags */
	char trantbl[4]; /* charset table translation */
	int charset;  /* current charset */
	int icharset; /* selected charset for sequence */
	int *tabs;
	Rune lastc;   /* last printed char outside of sequence, 0 if control */

	int wmode;
	int cursor;
	int buttons;
	int mx, my;

	TCursor saved[2];
	CSIEscape csiescseq;
	STREscape strescseq;

	const struct vt_callbacks *cb;
	void *arg;
};

static void csihandle(void);
static void csiparse(void);
static void csireset(void);
static void osc_color_response(int, u_int32_t, int);
static int eschandle(uchar);
static void strhandle(void);
static void strjoin(char *);
static void strparse(void);
static void strreset(void);

static void tclearregion(int, int, int, int);
static void tclearcomb(Glyph *);
static void tcombine(Rune);
static void tcursor(int);
static void tdeletechar(int);
static void tdeleteline(int);
static void tfixwide(int, int);
static void tinsertblank(int);
static void tinsertblankline(int);
static void tmoveto(int, int);
static int tmovevert(int);
static void tmoveato(int, int);
static void tnewline(int);
static void tputtab(int);
static void tputc(Rune);
static void treset(void);
static void tsoftreset(void);
static void tresize(int, int);
static void tslide(Line *, int *, int, int);
static void tscrollup(int, int);
static void tscrolldown(int, int);
static void tsetattr(const int *, int);
static void tsetsgr(const int *, const char *, int);
static void tsetsubattr(const int *, int);
static void tsetchar(Rune, const Glyph *, int, int);
static void tsetscroll(int, int);
static void tswapscreen(void);
static void tsetmode(int, int, const int *, int);
static int twrite(const char *, int);
static void tcontrolcode(uchar );
static void tdectest(char );
static void tdefutf8(char);
static int32_t tdefcolor(const int *, int *, int);
static void tdeftran(char);
static void tstrsequence(uchar);

static size_t utf8decode(const char *, Rune *, size_t);
static Rune utf8decodebyte(char, size_t *);
static size_t utf8encode(Rune, char *);
static char utf8encodebyte(Rune, size_t);
static size_t utf8validate(Rune *, size_t);

static void ttywrite(const char *, size_t);
static void ttywritef(const char *, ...)
    __attribute__((format (printf, 1, 2)));

static struct vt *term;

static const uchar utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const uchar utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const Rune utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const Rune utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

static const struct color vt_ansi[16] = {
	{   0,   0,   0 }, { 224,   0,   0 }, {   0, 224,   0 },
	{ 224, 224,   0 }, {   0,   0, 224 }, { 224,   0, 224 },
	{   0, 224, 224 }, { 224, 224, 224 }, { 128, 128, 128 },
	{ 255,  64,  64 }, {  64, 255,  64 }, { 255, 255,  64 },
	{  64,  64, 255 }, { 255,  64, 255 }, {  64, 255, 255 },
	{ 255, 255, 255 },
};

static const u_int8_t vt_ramp[6] = { 0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff };

static const struct color *vt_fg = NULL;
static const struct color *vt_bg = NULL;

static const struct {
	int		key;
	int		num;
	char		final;
} vt_keys[] = {
	{ COMA_VT_KEY_UP,	1,	'A' },
	{ COMA_VT_KEY_DOWN,	1,	'B' },
	{ COMA_VT_KEY_RIGHT,	1,	'C' },
	{ COMA_VT_KEY_LEFT,	1,	'D' },
	{ COMA_VT_KEY_HOME,	1,	'H' },
	{ COMA_VT_KEY_END,	1,	'F' },
	{ COMA_VT_KEY_INS,	2,	'~' },
	{ COMA_VT_KEY_DEL,	3,	'~' },
	{ COMA_VT_KEY_PAGEUP,	5,	'~' },
	{ COMA_VT_KEY_PAGEDOWN,	6,	'~' },
	{ COMA_VT_KEY_F(1),	1,	'P' },
	{ COMA_VT_KEY_F(2),	1,	'Q' },
	{ COMA_VT_KEY_F(3),	1,	'R' },
	{ COMA_VT_KEY_F(4),	1,	'S' },
	{ COMA_VT_KEY_F(5),	15,	'~' },
	{ COMA_VT_KEY_F(6),	17,	'~' },
	{ COMA_VT_KEY_F(7),	18,	'~' },
	{ COMA_VT_KEY_F(8),	19,	'~' },
	{ COMA_VT_KEY_F(9),	20,	'~' },
	{ COMA_VT_KEY_F(10),	21,	'~' },
	{ COMA_VT_KEY_F(11),	23,	'~' },
	{ COMA_VT_KEY_F(12),	24,	'~' },
	{ 0,			0,	0 },
};

struct vt *
coma_vt_create(int col, int row, const struct vt_callbacks *cb, void *arg)
{
	struct vt	*vt;

	vt = coma_calloc(1, sizeof(*vt));

	vt->cb = cb;
	vt->arg = arg;
	vt->c.attr.fg = COMA_VT_FG_DEFAULT;
	vt->c.attr.bg = COMA_VT_BG_DEFAULT;

	term = vt;

	strreset();
	tresize(col, row);
	treset();

	return (vt);
}

void
coma_vt_free(struct vt *vt)
{
	int		i;

	for (i = 0; i < vt->row; i++) {
		free(vt->line[i]);
		free(vt->alt[i]);
	}

	free(vt->line);
	free(vt->alt);
	free(vt->tabs);
	free(vt->strescseq.buf);
	free(vt);

	if (term == vt)
		term = NULL;
}

size_t
coma_vt_write(struct vt *vt, const char *buf, size_t len)
{
	term = vt;

	if (len > INT_MAX)
		len = INT_MAX;

	return (twrite(buf, len));
}

void
coma_vt_resize(struct vt *vt, int col, int row)
{
	term = vt;
	tresize(col, row);
}

int
coma_vt_mode(struct vt *vt)
{
	int		mode;

	mode = vt->wmode;

	if (vt->mode & MODE_ALTSCREEN)
		mode |= COMA_VT_MODE_ALTSCREEN;

	return (mode);
}

struct vt_cell *
coma_vt_row(struct vt *vt, int row)
{
	if (row < 0 || row >= vt->row)
		return (NULL);

	return (vt->line[row]);
}

void
coma_vt_cursor(struct vt *vt, int *col, int *row, int *shape)
{
	*col = vt->c.x;
	*row = vt->c.y;

	if (*col > 0 && (vt->line[*row][*col].mode & COMA_VT_ATTR_WDUMMY))
		(*col)--;

	switch (vt->cursor) {
	case 3:
	case 4:
		*shape = COMA_VT_CURSOR_UNDERLINE;
		break;
	case 5:
	case 6:
		*shape = COMA_VT_CURSOR_BAR;
		break;
	default:
		*shape = COMA_VT_CURSOR_BLOCK;
		break;
	}
}

void
coma_vt_color(u_int32_t color, struct color *rgb)
{
	u_int32_t	idx;

	if (color & COMA_VT_TRUECOLOR) {
		rgb->r = (color >> 16) & 0xff;
		rgb->g = (color >> 8) & 0xff;
		rgb->b = color & 0xff;
	} else if (color == COMA_VT_FG_DEFAULT) {
		if (vt_fg == NULL)
			vt_fg = coma_wm_color("terminal-foreground");
		*rgb = *vt_fg;
	} else if (color == COMA_VT_BG_DEFAULT) {
		if (vt_bg == NULL)
			vt_bg = coma_wm_color("terminal-background");
		*rgb = *vt_bg;
	} else if (color < 16) {
		*rgb = vt_ansi[color];
	} else if (color < 232) {
		idx = color - 16;
		rgb->r = vt_ramp[(idx / 36) % 6];
		rgb->g = vt_ramp[(idx / 6) % 6];
		rgb->b = vt_ramp[idx % 6];
	} else {
		rgb->r = 8 + ((color - 232) * 10);
		rgb->g = rgb->r;
		rgb->b = rgb->r;
	}
}

void
coma_vt_key(struct vt *vt, int key, u_int32_t mods)
{
	size_t		i;
	int		m;

	term = vt;

	switch (key) {
	case COMA_VT_KEY_KP_ENTER:
		if (IS_WSET(COMA_VT_MODE_APPKEYPAD)) {
			ttywrite("\033OM", 3);
			return;
		}
		/* FALLTHROUGH */
	case COMA_VT_KEY_ENTER:
		if (mods & COMA_MOD_ALT)
			ttywrite("\033", 1);
		if (IS_SET(MODE_CRLF))
			ttywrite("\r\n", 2);
		else
			ttywrite("\r", 1);
		return;
	case COMA_VT_KEY_TAB:
		if (mods & COMA_MOD_SHIFT) {
			ttywrite("\033[Z", 3);
		} else {
			if (mods & COMA_MOD_ALT)
				ttywrite("\033", 1);
			ttywrite("\t", 1);
		}
		return;
	case COMA_VT_KEY_BACKSPACE:
		if (mods & COMA_MOD_ALT)
			ttywrite("\033", 1);
		ttywrite("\177", 1);
		return;
	case COMA_VT_KEY_ESCAPE:
		if (mods & COMA_MOD_ALT)
			ttywrite("\033", 1);
		ttywrite("\033", 1);
		return;
	}

	for (i = 0; vt_keys[i].key != 0; i++) {
		if (vt_keys[i].key == key)
			break;
	}

	if (vt_keys[i].key == 0)
		return;

	m = 1;

	if (mods & COMA_MOD_SHIFT)
		m += 1;
	if (mods & COMA_MOD_ALT)
		m += 2;
	if (mods & COMA_MOD_CTRL)
		m += 4;

	if (vt_keys[i].final == '~') {
		if (m == 1)
			ttywritef("\033[%d~", vt_keys[i].num);
		else
			ttywritef("\033[%d;%d~", vt_keys[i].num, m);
	} else if (m != 1) {
		ttywritef("\033[1;%d%c", m, vt_keys[i].final);
	} else if (vt_keys[i].num == 1 && vt_keys[i].final >= 'P') {
		ttywritef("\033O%c", vt_keys[i].final);
	} else if (IS_WSET(COMA_VT_MODE_APPCURSOR)) {
		ttywritef("\033O%c", vt_keys[i].final);
	} else {
		ttywritef("\033[%c", vt_keys[i].final);
	}
}

void
coma_vt_unichar(struct vt *vt, u_int32_t cp, u_int32_t mods)
{
	size_t		len;
	char		buf[UTF_SIZ];

	term = vt;

	if ((len = utf8encode(cp, buf)) == 0)
		return;

	if (mods & COMA_MOD_ALT)
		ttywrite("\033", 1);

	ttywrite(buf, len);
}

void
coma_vt_paste(struct vt *vt, const char *text)
{
	const char	*p;
	size_t		len;
	char		buf[256];
	int		bracketed;

	term = vt;

	len = 0;
	bracketed = IS_WSET(COMA_VT_MODE_BRCKTPASTE);

	if (bracketed)
		ttywrite("\033[200~", 6);

	for (p = text; *p != '\0'; p++) {
		if (len == sizeof(buf)) {
			ttywrite(buf, len);
			len = 0;
		}

		if (*p == '\n') {
			if (p == text || p[-1] != '\r')
				buf[len++] = '\r';
			continue;
		}

		if (bracketed && ISCONTROLC0((uchar)*p) &&
		    *p != '\t' && *p != '\r')
			continue;

		buf[len++] = *p;
	}

	if (len > 0)
		ttywrite(buf, len);

	if (bracketed)
		ttywrite("\033[201~", 6);
}

void
coma_vt_focus(struct vt *vt, int focus)
{
	term = vt;

	if (!IS_WSET(COMA_VT_MODE_FOCUS))
		return;

	if (focus)
		ttywrite("\033[I", 3);
	else
		ttywrite("\033[O", 3);
}

void
coma_vt_mouse(struct vt *vt, int type, int btn, int x, int y, u_int32_t mods)
{
	int code;

	term = vt;

	if (type == COMA_VT_MOUSE_PRESS && BETWEEN(btn, 1, 3))
		term->buttons |= 1 << (btn - 1);
	else if (type == COMA_VT_MOUSE_RELEASE && BETWEEN(btn, 1, 3))
		term->buttons &= ~(1 << (btn - 1));

	if (!IS_WSET(COMA_VT_MODE_MOUSE))
		return;

	if (type == COMA_VT_MOUSE_MOTION) {
		if (x == term->mx && y == term->my)
			return;
		if (!IS_WSET(COMA_VT_MODE_MOUSEMOTION) &&
		    !IS_WSET(COMA_VT_MODE_MOUSEMANY))
			return;
		if (IS_WSET(COMA_VT_MODE_MOUSEMOTION) && term->buttons == 0)
			return;
		for (btn = 1; btn <= 11 &&
		    !(term->buttons & (1<<(btn-1))); btn++)
			;
		code = 32;
	} else {
		if (btn < 1 || btn > 11)
			return;
		if (type == COMA_VT_MOUSE_RELEASE) {
			if (IS_WSET(COMA_VT_MODE_MOUSEX10))
				return;
			if (btn == 4 || btn == 5)
				return;
		}
		code = 0;
	}

	term->mx = x;
	term->my = y;

	if ((!IS_WSET(COMA_VT_MODE_MOUSESGR) &&
	    type == COMA_VT_MOUSE_RELEASE) || btn == 12)
		code += 3;
	else if (btn >= 8)
		code += 128 + btn - 8;
	else if (btn >= 4)
		code += 64 + btn - 4;
	else
		code += btn - 1;

	if (!IS_WSET(COMA_VT_MODE_MOUSEX10)) {
		if (mods & COMA_MOD_SHIFT)
			code += 4;
		if (mods & COMA_MOD_ALT)
			code += 8;
		if (mods & COMA_MOD_CTRL)
			code += 16;
	}

	if (IS_WSET(COMA_VT_MODE_MOUSESGR)) {
		ttywritef("\033[<%d;%d;%d%c", code, x+1, y+1,
		    type == COMA_VT_MOUSE_RELEASE ? 'm' : 'M');
	} else if (x < 223 && y < 223) {
		ttywritef("\033[M%c%c%c", 32+code, 32+x+1, 32+y+1);
	}
}

static void
ttywrite(const char *s, size_t n)
{
	term->cb->output(term->arg, s, n);
}

static void
ttywritef(const char *fmt, ...)
{
	int		len;
	va_list		args;
	char		buf[64];

	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len < 0 || (size_t)len >= sizeof(buf))
		return;

	ttywrite(buf, len);
}

static size_t
utf8decode(const char *c, Rune *u, size_t clen)
{
	size_t i, j, len, type;
	Rune udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if (type != 0)
			return j;
	}
	if (j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);

	return len;
}

static Rune
utf8decodebyte(char c, size_t *i)
{
	for (*i = 0; *i < LEN(utfmask); ++(*i))
		if (((uchar)c & utfmask[*i]) == utfbyte[*i])
			return (uchar)c & ~utfmask[*i];

	return 0;
}

static size_t
utf8encode(Rune u, char *c)
{
	size_t len, i;

	len = utf8validate(&u, 0);
	if (len > UTF_SIZ)
		return 0;

	for (i = len - 1; i != 0; --i) {
		c[i] = utf8encodebyte(u, 0);
		u >>= 6;
	}
	c[0] = utf8encodebyte(u, len);

	return len;
}

static char
utf8encodebyte(Rune u, size_t i)
{
	return utfbyte[i] | (u & ~utfmask[i]);
}

static size_t
utf8validate(Rune *u, size_t i)
{
	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i)
		;

	return i;
}

static void
tcursor(int mode)
{
	int alt = IS_SET(MODE_ALTSCREEN);

	if (mode == CURSOR_SAVE) {
		term->saved[alt] = term->c;
		memcpy(term->saved[alt].trantbl, term->trantbl,
		    sizeof(term->trantbl));
		term->saved[alt].charset = term->charset;
	} else if (mode == CURSOR_LOAD) {
		term->c = term->saved[alt];
		memcpy(term->trantbl, term->saved[alt].trantbl,
		    sizeof(term->trantbl));
		term->charset = term->saved[alt].charset;
		tmoveto(term->saved[alt].x, term->saved[alt].y);
		if ((term->saved[alt].state & CURSOR_WRAPNEXT) &&
		    term->c.x == term->saved[alt].x &&
		    (term->c.x == term->col-1 || (term->c.x == term->col-2 &&
		    (term->line[term->c.y][term->c.x].mode &
		    COMA_VT_ATTR_WIDE))))
			term->c.state |= CURSOR_WRAPNEXT;
	}
}

static void
treset(void)
{
	uint i;

	term->c = (TCursor){{
		.mode = 0,
		.fg = COMA_VT_FG_DEFAULT,
		.bg = COMA_VT_BG_DEFAULT
	}, .x = 0, .y = 0, .state = CURSOR_DEFAULT};

	memset(term->tabs, 0, term->col * sizeof(*term->tabs));
	for (i = TAB_SPACES; i < (uint)term->col; i += TAB_SPACES)
		term->tabs[i] = 1;
	term->top = 0;
	term->bot = term->row - 1;
	term->mode = MODE_WRAP|MODE_UTF8;
	memset(term->trantbl, CS_USA, sizeof(term->trantbl));
	term->charset = 0;

	for (i = 0; i < 2; i++) {
		tmoveto(0, 0);
		tcursor(CURSOR_SAVE);
		tclearregion(0, 0, term->col-1, term->row-1);
		tswapscreen();
	}
}

static void
tsoftreset(void)
{
	term->mode &= ~MODE_INSERT;
	term->mode |= MODE_WRAP;
	term->wmode &= ~(COMA_VT_MODE_HIDE | COMA_VT_MODE_APPKEYPAD |
	    COMA_VT_MODE_APPCURSOR);
	term->c.state &= ~CURSOR_ORIGIN;
	term->c.attr.mode = 0;
	term->c.attr.fg = COMA_VT_FG_DEFAULT;
	term->c.attr.bg = COMA_VT_BG_DEFAULT;
	memset(term->trantbl, CS_USA, sizeof(term->trantbl));
	term->charset = 0;
	tsetscroll(0, term->row-1);
	term->cursor = 0;

	tcursor(CURSOR_SAVE);
	term->saved[IS_SET(MODE_ALTSCREEN)].x = 0;
	term->saved[IS_SET(MODE_ALTSCREEN)].y = 0;
}

static void
tswapscreen(void)
{
	Line *tmp = term->line;

	term->line = term->alt;
	term->alt = tmp;
	term->mode ^= MODE_ALTSCREEN;

	if (term->cb->damage != NULL)
		term->cb->damage(term->arg, 0, 0, term->col-1, term->row-1);
}

static void
tscrolldown(int orig, int n)
{
	int i;
	Line temp;

	LIMIT(n, 0, term->bot-orig+1);

	if (term->cb->scroll != NULL)
		term->cb->scroll(term->arg, orig, term->bot, n);

	for (i = term->bot; i >= orig+n; i--) {
		temp = term->line[i];
		term->line[i] = term->line[i-n];
		term->line[i-n] = temp;
	}

	tclearregion(0, orig, term->col-1, orig+n-1);
}

static void
tscrollup(int orig, int n)
{
	int i;
	Line temp;

	LIMIT(n, 0, term->bot-orig+1);

	if (orig == 0 && !IS_SET(MODE_ALTSCREEN) &&
	    term->cb->scrollout != NULL) {
		for (i = 0; i < n; i++) {
			term->cb->scrollout(term->arg, term->line[i],
			    term->col);
		}
	}

	if (term->cb->scroll != NULL)
		term->cb->scroll(term->arg, orig, term->bot, -n);

	for (i = orig; i <= term->bot-n; i++) {
		temp = term->line[i];
		term->line[i] = term->line[i+n];
		term->line[i+n] = temp;
	}

	tclearregion(0, term->bot-n+1, term->col-1, term->bot);
}

static void
tnewline(int first_col)
{
	int y = term->c.y;

	if (y == term->bot) {
		tscrollup(term->top, 1);
	} else {
		y++;
	}
	tmoveto(first_col ? 0 : term->c.x, y);
}

static void
csiparse(void)
{
	char *p = term->csiescseq.buf, *np;
	long int v;

	term->csiescseq.narg = 0;
	if (*p == '?') {
		term->csiescseq.priv = 1;
		p++;
	}

	term->csiescseq.buf[term->csiescseq.len] = '\0';
	while (p < term->csiescseq.buf+term->csiescseq.len) {
		np = NULL;
		v = strtol(p, &np, 10);
		if (np == p)
			v = 0;
		if (v < 0)
			v = 0;
		if (v > USHRT_MAX)
			v = USHRT_MAX;
		term->csiescseq.arg[term->csiescseq.narg++] = v;
		p = np;
		if ((*p != ';' && *p != ':') ||
		    term->csiescseq.narg == ESC_ARG_SIZ)
			break;
		term->csiescseq.sub[term->csiescseq.narg] = (*p == ':');
		p++;
	}
	term->csiescseq.mode[0] = *p++;
	term->csiescseq.mode[1] =
	    (p < term->csiescseq.buf+term->csiescseq.len) ? *p : '\0';
}

/* for absolute user moves, when decom is set */
static void
tmoveato(int x, int y)
{
	tmoveto(x, y + ((term->c.state & CURSOR_ORIGIN) ? term->top: 0));
}

static int
tmovevert(int n)
{
	int y = term->c.y + n;

	if (n < 0 && term->c.y >= term->top && y < term->top)
		y = term->top;
	if (n > 0 && term->c.y <= term->bot && y > term->bot)
		y = term->bot;
	return y;
}

static void
tmoveto(int x, int y)
{
	int miny, maxy;

	if (term->c.state & CURSOR_ORIGIN) {
		miny = term->top;
		maxy = term->bot;
	} else {
		miny = 0;
		maxy = term->row - 1;
	}
	term->c.state &= ~CURSOR_WRAPNEXT;
	term->c.x = LIMIT(x, 0, term->col-1);
	term->c.y = LIMIT(y, miny, maxy);
}

static void
tsetchar(Rune u, const Glyph *attr, int x, int y)
{
	static const char *vt100_0[62] = { /* 0x41 - 0x7e */
		"↑", "↓", "→", "←", "█", "▚", "☃", /* A - G */
		0, 0, 0, 0, 0, 0, 0, 0, /* H - O */
		0, 0, 0, 0, 0, 0, 0, 0, /* P - W */
		0, 0, 0, 0, 0, 0, 0, " ", /* X - _ */
		"◆", "▒", "␉", "␌", "␍", "␊", "°", "±", /* ` - g */
		"␤", "␋", "┘", "┐", "┌", "└", "┼", "⎺", /* h - o */
		"⎻", "─", "⎼", "⎽", "├", "┤", "┴", "┬", /* p - w */
		"│", "≤", "≥", "π", "≠", "£", "·", /* x - ~ */
	};

	/*
	 * The table is proudly stolen from rxvt.
	 */
	if (term->trantbl[term->charset] == CS_GRAPHIC0 &&
	    BETWEEN(u, 0x41, 0x7e) && vt100_0[u - 0x41])
		utf8decode(vt100_0[u - 0x41], &u, UTF_SIZ);

	if (term->line[y][x].mode & COMA_VT_ATTR_WIDE) {
		if (x+1 < term->col) {
			term->line[y][x+1].u = ' ';
			term->line[y][x+1].mode &= ~COMA_VT_ATTR_WDUMMY;
			tclearcomb(&term->line[y][x+1]);
			if (term->cb->damage != NULL)
				term->cb->damage(term->arg, x+1, y, x+1, y);
		}
	} else if (x > 0 && (term->line[y][x].mode & COMA_VT_ATTR_WDUMMY)) {
		term->line[y][x-1].u = ' ';
		term->line[y][x-1].mode &= ~COMA_VT_ATTR_WIDE;
		tclearcomb(&term->line[y][x-1]);
		if (term->cb->damage != NULL)
			term->cb->damage(term->arg, x-1, y, x-1, y);
	}

	if (term->cb->damage != NULL)
		term->cb->damage(term->arg, x, y, x, y);

	term->line[y][x] = *attr;
	term->line[y][x].u = u;
	tclearcomb(&term->line[y][x]);
}

static void
tclearcomb(Glyph *gp)
{
	memset(gp->comb, 0, sizeof(gp->comb));
}

static void
tcombine(Rune u)
{
	int i, x;
	Glyph *gp;

	x = term->c.x;
	if (!(term->c.state & CURSOR_WRAPNEXT))
		x--;
	if (x > 0 && (term->line[term->c.y][x].mode & COMA_VT_ATTR_WDUMMY))
		x--;
	if (x < 0)
		return;

	gp = &term->line[term->c.y][x];
	for (i = 0; i < COMA_VT_COMB_MAX; i++) {
		if (gp->comb[i] == 0) {
			gp->comb[i] = u;
			break;
		}
	}

	if (term->cb->damage != NULL)
		term->cb->damage(term->arg, x, term->c.y, x, term->c.y);
}

static void
tclearregion(int x1, int y1, int x2, int y2)
{
	int x, y, temp;
	Glyph *gp;

	if (x1 > x2)
		temp = x1, x1 = x2, x2 = temp;
	if (y1 > y2)
		temp = y1, y1 = y2, y2 = temp;

	LIMIT(x1, 0, term->col-1);
	LIMIT(x2, 0, term->col-1);
	LIMIT(y1, 0, term->row-1);
	LIMIT(y2, 0, term->row-1);

	if (term->cb->damage != NULL)
		term->cb->damage(term->arg, x1, y1, x2, y2);

	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			gp = &term->line[y][x];
			gp->fg = term->c.attr.fg;
			gp->bg = term->c.attr.bg;
			gp->mode = 0;
			gp->u = ' ';
			tclearcomb(gp);
		}
		tfixwide(x1, y);
		tfixwide(x2+1, y);
	}
}

static void
tfixwide(int x, int y)
{
	Glyph *line;

	if (x < 0 || x > term->col)
		return;

	line = term->line[y];

	if (x > 0 && (line[x-1].mode & COMA_VT_ATTR_WIDE) &&
	    (x == term->col || !(line[x].mode & COMA_VT_ATTR_WDUMMY))) {
		line[x-1].u = ' ';
		line[x-1].mode &= ~COMA_VT_ATTR_WIDE;
		tclearcomb(&line[x-1]);
		if (term->cb->damage != NULL)
			term->cb->damage(term->arg, x-1, y, x-1, y);
	}

	if (x < term->col && (line[x].mode & COMA_VT_ATTR_WDUMMY) &&
	    (x == 0 || !(line[x-1].mode & COMA_VT_ATTR_WIDE))) {
		line[x].u = ' ';
		line[x].mode &= ~COMA_VT_ATTR_WDUMMY;
		if (term->cb->damage != NULL)
			term->cb->damage(term->arg, x, y, x, y);
	}
}

static void
tdeletechar(int n)
{
	int dst, src, size;
	Glyph *line;

	LIMIT(n, 0, term->col - term->c.x);

	dst = term->c.x;
	src = term->c.x + n;
	size = term->col - src;
	line = term->line[term->c.y];

	if (term->cb->damage != NULL) {
		term->cb->damage(term->arg, term->c.x, term->c.y,
		    term->col-1, term->c.y);
	}

	memmove(&line[dst], &line[src], size * sizeof(Glyph));
	tfixwide(term->c.x, term->c.y);
	tclearregion(term->col-n, term->c.y, term->col-1, term->c.y);
}

static void
tinsertblank(int n)
{
	int dst, src, size;
	Glyph *line;

	LIMIT(n, 0, term->col - term->c.x);

	dst = term->c.x + n;
	src = term->c.x;
	size = term->col - dst;
	line = term->line[term->c.y];

	if (term->cb->damage != NULL) {
		term->cb->damage(term->arg, term->c.x, term->c.y,
		    term->col-1, term->c.y);
	}

	memmove(&line[dst], &line[src], size * sizeof(Glyph));
	tfixwide(term->col, term->c.y);
	tclearregion(src, term->c.y, dst - 1, term->c.y);
}

static void
tinsertblankline(int n)
{
	if (BETWEEN(term->c.y, term->top, term->bot)) {
		tmoveto(0, term->c.y);
		tscrolldown(term->c.y, n);
	}
}

static void
tdeleteline(int n)
{
	if (BETWEEN(term->c.y, term->top, term->bot)) {
		tmoveto(0, term->c.y);
		tscrollup(term->c.y, n);
	}
}

static int32_t
tdefcolor(const int *attr, int *npar, int l)
{
	int32_t idx = -1;
	uint r, g, b;

	if (*npar + 1 >= l)
		return idx;

	switch (attr[*npar + 1]) {
	case 2: /* direct color in RGB space */
		if (*npar + 4 >= l)
			break;
		r = attr[*npar + 2];
		g = attr[*npar + 3];
		b = attr[*npar + 4];
		*npar += 4;
		if (BETWEEN(r, 0, 255) && BETWEEN(g, 0, 255) &&
		    BETWEEN(b, 0, 255))
			idx = TRUECOLOR(r, g, b);
		break;
	case 5: /* indexed color */
		if (*npar + 2 >= l)
			break;
		*npar += 2;
		if (BETWEEN(attr[*npar], 0, 255))
			idx = attr[*npar];
		break;
	case 0: /* implemented defined (only foreground) */
	case 1: /* transparent */
	case 3: /* direct color in CMY space */
	case 4: /* direct color in CMYK space */
	default:
		break;
	}

	return idx;
}

static void
tsetattr(const int *attr, int l)
{
	int i;
	int32_t idx;

	for (i = 0; i < l; i++) {
		switch (attr[i]) {
		case 0:
			term->c.attr.mode &= ~(COMA_VT_ATTR_BOLD |
			    COMA_VT_ATTR_FAINT | COMA_VT_ATTR_ITALIC |
			    COMA_VT_ATTR_UNDERLINE | COMA_VT_ATTR_BLINK |
			    COMA_VT_ATTR_REVERSE | COMA_VT_ATTR_INVISIBLE |
			    COMA_VT_ATTR_STRUCK);
			term->c.attr.fg = COMA_VT_FG_DEFAULT;
			term->c.attr.bg = COMA_VT_BG_DEFAULT;
			break;
		case 1:
			term->c.attr.mode |= COMA_VT_ATTR_BOLD;
			break;
		case 2:
			term->c.attr.mode |= COMA_VT_ATTR_FAINT;
			break;
		case 3:
			term->c.attr.mode |= COMA_VT_ATTR_ITALIC;
			break;
		case 4:
		case 21:
			term->c.attr.mode |= COMA_VT_ATTR_UNDERLINE;
			break;
		case 5: /* slow blink */
			/* FALLTHROUGH */
		case 6: /* rapid blink */
			term->c.attr.mode |= COMA_VT_ATTR_BLINK;
			break;
		case 7:
			term->c.attr.mode |= COMA_VT_ATTR_REVERSE;
			break;
		case 8:
			term->c.attr.mode |= COMA_VT_ATTR_INVISIBLE;
			break;
		case 9:
			term->c.attr.mode |= COMA_VT_ATTR_STRUCK;
			break;
		case 22:
			term->c.attr.mode &= ~(COMA_VT_ATTR_BOLD |
			    COMA_VT_ATTR_FAINT);
			break;
		case 23:
			term->c.attr.mode &= ~COMA_VT_ATTR_ITALIC;
			break;
		case 24:
			term->c.attr.mode &= ~COMA_VT_ATTR_UNDERLINE;
			break;
		case 25:
			term->c.attr.mode &= ~COMA_VT_ATTR_BLINK;
			break;
		case 27:
			term->c.attr.mode &= ~COMA_VT_ATTR_REVERSE;
			break;
		case 28:
			term->c.attr.mode &= ~COMA_VT_ATTR_INVISIBLE;
			break;
		case 29:
			term->c.attr.mode &= ~COMA_VT_ATTR_STRUCK;
			break;
		case 38:
			if ((idx = tdefcolor(attr, &i, l)) >= 0)
				term->c.attr.fg = idx;
			break;
		case 39: /* set foreground color to default */
			term->c.attr.fg = COMA_VT_FG_DEFAULT;
			break;
		case 48:
			if ((idx = tdefcolor(attr, &i, l)) >= 0)
				term->c.attr.bg = idx;
			break;
		case 49: /* set background color to default */
			term->c.attr.bg = COMA_VT_BG_DEFAULT;
			break;
		case 58:
			/* This starts a sequence to change the color of
			 * "underline" pixels. We don't support that and
			 * instead eat up a following "5;n" or "2;r;g;b". */
			tdefcolor(attr, &i, l);
			break;
		default:
			if (BETWEEN(attr[i], 30, 37)) {
				term->c.attr.fg = attr[i] - 30;
			} else if (BETWEEN(attr[i], 40, 47)) {
				term->c.attr.bg = attr[i] - 40;
			} else if (BETWEEN(attr[i], 90, 97)) {
				term->c.attr.fg = attr[i] - 90 + 8;
			} else if (BETWEEN(attr[i], 100, 107)) {
				term->c.attr.bg = attr[i] - 100 + 8;
			}
			break;
		}
	}
}

static void
tsetsgr(const int *arg, const char *sub, int narg)
{
	int i, j;

	for (i = 0; i < narg; i = j) {
		for (j = i + 1; j < narg && sub[j]; j++)
			;
		if (j - i > 1) {
			tsetsubattr(&arg[i], j - i);
			continue;
		}
		while (j < narg && (j + 1 == narg || !sub[j + 1]))
			j++;
		tsetattr(&arg[i], j - i);
	}
}

static void
tsetsubattr(const int *arg, int n)
{
	int attr[5];

	switch (arg[0]) {
	case 4:
		if (arg[1] == 0)
			term->c.attr.mode &= ~COMA_VT_ATTR_UNDERLINE;
		else
			term->c.attr.mode |= COMA_VT_ATTR_UNDERLINE;
		break;
	case 38:
	case 48:
	case 58:
		if (arg[1] == 2 && n == 6) {
			attr[0] = arg[0];
			attr[1] = arg[1];
			memcpy(&attr[2], &arg[3], 3 * sizeof(int));
			tsetattr(attr, 5);
		} else {
			tsetattr(arg, MIN(n, 5));
		}
		break;
	default:
		tsetattr(arg, 1);
		break;
	}
}

static void
tsetscroll(int t, int b)
{
	int temp;

	LIMIT(t, 0, term->row-1);
	LIMIT(b, 0, term->row-1);
	if (t > b) {
		temp = t;
		t = b;
		b = temp;
	}
	term->top = t;
	term->bot = b;
}

static void
tsetmode(int priv, int set, const int *args, int narg)
{
	int alt; const int *lim;

	for (lim = args + narg; args < lim; ++args) {
		if (priv) {
			switch (*args) {
			case 1: /* DECCKM -- Cursor key */
				MODBIT(term->wmode, set,
				    COMA_VT_MODE_APPCURSOR);
				break;
			case 5: /* DECSCNM -- Reverse video */
				MODBIT(term->wmode, set, COMA_VT_MODE_REVERSE);
				break;
			case 6: /* DECOM -- Origin */
				MODBIT(term->c.state, set, CURSOR_ORIGIN);
				tmoveato(0, 0);
				break;
			case 7: /* DECAWM -- Auto wrap */
				if (set && !IS_SET(MODE_WRAP))
					term->c.state &= ~CURSOR_WRAPNEXT;
				MODBIT(term->mode, set, MODE_WRAP);
				break;
			case 0:  /* Error (IGNORED) */
			case 2:  /* DECANM -- ANSI/VT52 (IGNORED) */
			case 3:  /* DECCOLM -- Column  (IGNORED) */
			case 4:  /* DECSCLM -- Scroll (IGNORED) */
			case 8:  /* DECARM -- Auto repeat (IGNORED) */
			case 18: /* DECPFF -- Printer feed (IGNORED) */
			case 19: /* DECPEX -- Printer extent (IGNORED) */
			case 42: /* DECNRCM -- National characters (IGNORED) */
			case 12: /* att610 -- Start blinking cursor (IGNORED) */
				break;
			case 25: /* DECTCEM -- Text Cursor Enable Mode */
				MODBIT(term->wmode, !set, COMA_VT_MODE_HIDE);
				break;
			case 9:    /* X10 mouse compatibility mode */
				term->wmode &= ~COMA_VT_MODE_MOUSE;
				MODBIT(term->wmode, set, COMA_VT_MODE_MOUSEX10);
				break;
			case 1000: /* 1000: report button press */
				term->wmode &= ~COMA_VT_MODE_MOUSE;
				MODBIT(term->wmode, set, COMA_VT_MODE_MOUSEBTN);
				break;
			case 1002: /* 1002: report motion on button press */
				term->wmode &= ~COMA_VT_MODE_MOUSE;
				MODBIT(term->wmode, set,
				    COMA_VT_MODE_MOUSEMOTION);
				break;
			case 1003: /* 1003: enable all mouse motions */
				term->wmode &= ~COMA_VT_MODE_MOUSE;
				MODBIT(term->wmode, set,
				    COMA_VT_MODE_MOUSEMANY);
				break;
			case 1004: /* 1004: send focus events to tty */
				MODBIT(term->wmode, set, COMA_VT_MODE_FOCUS);
				break;
			case 1006: /* 1006: extended reporting mode */
				MODBIT(term->wmode, set, COMA_VT_MODE_MOUSESGR);
				break;
			case 1034: /* 1034: enable 8-bit mode (IGNORED) */
				break;
			case 1049: /* swap screen & set/restore cursor as xterm */
				tcursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
				/* FALLTHROUGH */
			case 47: /* swap screen buffer */
			case 1047: /* swap screen buffer */
				alt = IS_SET(MODE_ALTSCREEN);
				if (alt) {
					tclearregion(0, 0, term->col-1,
					    term->row-1);
				}
				if (set ^ alt) /* set is always 1 or 0 */
					tswapscreen();
				if (*args != 1049)
					break;
				/* FALLTHROUGH */
			case 1048: /* save/restore cursor (like DECSC/DECRC) */
				tcursor((set) ? CURSOR_SAVE : CURSOR_LOAD);
				break;
			case 2004: /* 2004: bracketed paste mode */
				MODBIT(term->wmode, set,
				    COMA_VT_MODE_BRCKTPASTE);
				break;
			/* Not implemented mouse modes. See comments there. */
			case 1001: /* mouse highlight mode; can hang the
				      terminal by design when implemented. */
			case 1005: /* UTF-8 mouse mode; will confuse
				      applications not supporting UTF-8
				      and luit. */
			case 1015: /* urxvt mangled mouse mode; incompatible
				      and can be mistaken for other control
				      codes. */
				break;
			default:
				break;
			}
		} else {
			switch (*args) {
			case 0:  /* Error (IGNORED) */
				break;
			case 2:  /* KAM -- keyboard action (IGNORED) */
				break;
			case 4:  /* IRM -- Insertion-replacement */
				MODBIT(term->mode, set, MODE_INSERT);
				break;
			case 12: /* SRM -- Send/Receive (IGNORED) */
				break;
			case 20: /* LNM -- Linefeed/new line */
				MODBIT(term->mode, set, MODE_CRLF);
				break;
			default:
				break;
			}
		}
	}
}

static void
csihandle(void)
{
	CSIEscape *csi = &term->csiescseq;

	switch (csi->mode[0]) {
	default:
	unknown:
		break;
	case '@': /* ICH -- Insert <n> blank char */
		DEFAULT(csi->arg[0], 1);
		tinsertblank(csi->arg[0]);
		break;
	case 'A': /* CUU -- Cursor <n> Up */
		DEFAULT(csi->arg[0], 1);
		tmoveto(term->c.x, tmovevert(-csi->arg[0]));
		break;
	case 'B': /* CUD -- Cursor <n> Down */
		DEFAULT(csi->arg[0], 1);
		tmoveto(term->c.x, tmovevert(csi->arg[0]));
		break;
	case 'e': /* VPR --Cursor <n> Down */
		DEFAULT(csi->arg[0], 1);
		tmoveto(term->c.x, term->c.y+csi->arg[0]);
		break;
	case 'c': /* DA -- Device Attributes */
		if (csi->arg[0] == 0)
			ttywrite(VT_IDEN, strlen(VT_IDEN));
		break;
	case 'b': /* REP -- if last char is printable print it <n> more times */
		LIMIT(csi->arg[0], 1, 65535);
		if (term->lastc)
			while (csi->arg[0]-- > 0)
				tputc(term->lastc);
		break;
	case 'C': /* CUF -- Cursor <n> Forward */
	case 'a': /* HPR -- Cursor <n> Forward */
		DEFAULT(csi->arg[0], 1);
		tmoveto(term->c.x+csi->arg[0], term->c.y);
		break;
	case 'D': /* CUB -- Cursor <n> Backward */
		DEFAULT(csi->arg[0], 1);
		tmoveto(term->c.x-csi->arg[0], term->c.y);
		break;
	case 'E': /* CNL -- Cursor <n> Down and first col */
		DEFAULT(csi->arg[0], 1);
		tmoveto(0, tmovevert(csi->arg[0]));
		break;
	case 'F': /* CPL -- Cursor <n> Up and first col */
		DEFAULT(csi->arg[0], 1);
		tmoveto(0, tmovevert(-csi->arg[0]));
		break;
	case 'g': /* TBC -- Tabulation clear */
		switch (csi->arg[0]) {
		case 0: /* clear current tab stop */
			term->tabs[term->c.x] = 0;
			break;
		case 3: /* clear all the tabs */
			memset(term->tabs, 0, term->col * sizeof(*term->tabs));
			break;
		default:
			goto unknown;
		}
		break;
	case 'G': /* CHA -- Move to <col> */
	case '`': /* HPA */
		DEFAULT(csi->arg[0], 1);
		tmoveto(csi->arg[0]-1, term->c.y);
		break;
	case 'H': /* CUP -- Move to <row> <col> */
	case 'f': /* HVP */
		DEFAULT(csi->arg[0], 1);
		DEFAULT(csi->arg[1], 1);
		tmoveato(csi->arg[1]-1, csi->arg[0]-1);
		break;
	case 'I': /* CHT -- Cursor Forward Tabulation <n> tab stops */
		DEFAULT(csi->arg[0], 1);
		tputtab(csi->arg[0]);
		break;
	case 'J': /* ED -- Clear screen */
		switch (csi->arg[0]) {
		case 0: /* below */
			tclearregion(term->c.x, term->c.y,
			    term->col-1, term->c.y);
			if (term->c.y < term->row-1) {
				tclearregion(0, term->c.y+1, term->col-1,
				    term->row-1);
			}
			break;
		case 1: /* above */
			if (term->c.y > 0)
				tclearregion(0, 0, term->col-1, term->c.y-1);
			tclearregion(0, term->c.y, term->c.x, term->c.y);
			break;
		case 2: /* all */
			tclearregion(0, 0, term->col-1, term->row-1);
			break;
		case 3: /* scrollback */
			if (term->cb->sbclear != NULL)
				term->cb->sbclear(term->arg);
			break;
		default:
			goto unknown;
		}
		break;
	case 'K': /* EL -- Clear line */
		switch (csi->arg[0]) {
		case 0: /* right */
			tclearregion(term->c.x, term->c.y, term->col-1,
			    term->c.y);
			break;
		case 1: /* left */
			tclearregion(0, term->c.y, term->c.x, term->c.y);
			break;
		case 2: /* all */
			tclearregion(0, term->c.y, term->col-1, term->c.y);
			break;
		}
		break;
	case 'S': /* SU -- Scroll <n> line up */
		if (csi->priv) break;
		DEFAULT(csi->arg[0], 1);
		tscrollup(term->top, csi->arg[0]);
		break;
	case 'T': /* SD -- Scroll <n> line down */
		DEFAULT(csi->arg[0], 1);
		tscrolldown(term->top, csi->arg[0]);
		break;
	case 'L': /* IL -- Insert <n> blank lines */
		DEFAULT(csi->arg[0], 1);
		tinsertblankline(csi->arg[0]);
		break;
	case 'l': /* RM -- Reset Mode */
		tsetmode(csi->priv, 0, csi->arg, csi->narg);
		break;
	case 'M': /* DL -- Delete <n> lines */
		DEFAULT(csi->arg[0], 1);
		tdeleteline(csi->arg[0]);
		break;
	case 'X': /* ECH -- Erase <n> char */
		DEFAULT(csi->arg[0], 1);
		tclearregion(term->c.x, term->c.y,
		    term->c.x + csi->arg[0] - 1, term->c.y);
		break;
	case 'P': /* DCH -- Delete <n> char */
		DEFAULT(csi->arg[0], 1);
		tdeletechar(csi->arg[0]);
		break;
	case 'Z': /* CBT -- Cursor Backward Tabulation <n> tab stops */
		DEFAULT(csi->arg[0], 1);
		tputtab(-csi->arg[0]);
		break;
	case 'd': /* VPA -- Move to <row> */
		DEFAULT(csi->arg[0], 1);
		tmoveato(term->c.x, csi->arg[0]-1);
		break;
	case 'h': /* SM -- Set terminal mode */
		tsetmode(csi->priv, 1, csi->arg, csi->narg);
		break;
	case 'm': /* SGR -- Terminal attribute (color) */
		tsetsgr(csi->arg, csi->sub, csi->narg);
		break;
	case 'n': /* DSR -- Device Status Report */
		switch (csi->arg[0]) {
		case 5: /* Status Report "OK" `0n` */
			ttywrite("\033[0n", sizeof("\033[0n") - 1);
			break;
		case 6: /* Report Cursor Position (CPR) "<row>;<column>R" */
			if (term->c.state & CURSOR_ORIGIN) {
				ttywritef("\033[%s%i;%iR", csi->priv ? "?" : "",
				    term->c.y - term->top + 1, term->c.x+1);
			} else {
				ttywritef("\033[%s%i;%iR", csi->priv ? "?" : "",
				    term->c.y+1, term->c.x+1);
			}
			break;
		default:
			goto unknown;
		}
		break;
	case 'r': /* DECSTBM -- Set Scrolling Region */
		if (csi->priv) {
			goto unknown;
		} else {
			DEFAULT(csi->arg[0], 1);
			DEFAULT(csi->arg[1], term->row);
			if (csi->arg[1] > term->row)
				csi->arg[1] = term->row;
			if (csi->arg[0] >= csi->arg[1])
				goto unknown;
			tsetscroll(csi->arg[0]-1, csi->arg[1]-1);
			tmoveato(0, 0);
		}
		break;
	case 's': /* DECSC -- Save cursor position (ANSI.SYS) */
		tcursor(CURSOR_SAVE);
		break;
	case 'u': /* DECRC -- Restore cursor position (ANSI.SYS) */
		if (csi->priv) {
			goto unknown;
		} else {
			tcursor(CURSOR_LOAD);
		}
		break;
	case '!':
		switch (csi->mode[1]) {
		case 'p': /* DECSTR -- Soft terminal reset */
			tsoftreset();
			break;
		default:
			goto unknown;
		}
		break;
	case ' ':
		switch (csi->mode[1]) {
		case 'q': /* DECSCUSR -- Set Cursor Style */
			if (!BETWEEN(csi->arg[0], 0, 6))
				goto unknown;
			term->cursor = csi->arg[0];
			break;
		default:
			goto unknown;
		}
		break;
	}
}

static void
csireset(void)
{
	memset(&term->csiescseq, 0, sizeof(term->csiescseq));
}

static void
osc_color_response(int num, u_int32_t color, int is_osc4)
{
	struct color rgb;

	coma_vt_color(color, &rgb);

	ttywritef("\033]%s%d;rgb:%02x%02x/%02x%02x/%02x%02x%s",
	    is_osc4 ? "4;" : "", num, rgb.r, rgb.r, rgb.g, rgb.g,
	    rgb.b, rgb.b, term->strescseq.bel ? "\007" : "\033\\");
}

static void
strhandle(void)
{
	int j, k, narg, par;
	STREscape *str = &term->strescseq;
	const u_int32_t osc_table[] = {
		COMA_VT_FG_DEFAULT,	/* foreground */
		COMA_VT_BG_DEFAULT,	/* background */
		COMA_VT_FG_DEFAULT,	/* cursor */
	};

	term->esc &= ~(ESC_STR_END|ESC_STR);
	strparse();
	par = (narg = str->narg) ? atoi(str->args[0]) : 0;

	switch (str->type) {
	case ']': /* OSC -- Operating System Command */
		switch (par) {
		case 0:
		case 2:
			if (narg > 1 && term->cb->title != NULL) {
				strjoin(str->args[1]);
				term->cb->title(term->arg, str->args[1]);
			}
			return;
		case 1:
			return;
		case 52: /* manipulate selection data (IGNORED) */
			return;
		case 10: /* set dynamic VT100 text foreground color */
		case 11: /* set dynamic VT100 text background color */
		case 12: /* set dynamic text cursor color */
			if (narg < 2)
				break;
			for (j = 1; j < narg && par + j - 1 <= 12; j++) {
				if (!strcmp(str->args[j], "?"))
					osc_color_response(par + j - 1,
					    osc_table[par + j - 11], 0);
			}
			return;
		case 4: /* color set */
			if (narg < 3)
				break;
			for (k = 1; k + 1 < narg; k += 2) {
				j = atoi(str->args[k]);
				if (!strcmp(str->args[k + 1], "?") &&
				    BETWEEN(j, 0, 255))
					osc_color_response(j, j, 1);
			}
			return;
		case 104: /* color reset */
		case 110: /* reset dynamic VT100 text foreground color */
		case 111: /* reset dynamic VT100 text background color */
		case 112: /* reset dynamic text cursor color */
			return;
		}
		break;
	case 'k': /* old title set compatibility */
		if (narg > 0 && term->cb->title != NULL) {
			strjoin(str->args[0]);
			term->cb->title(term->arg, str->args[0]);
		}
		return;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
		return;
	}
}

static void
strjoin(char *p)
{
	STREscape *str = &term->strescseq;

	for (; p < str->buf + str->len; p++) {
		if (*p == '\0')
			*p = ';';
	}
}

static void
strparse(void)
{
	int c;
	STREscape *str = &term->strescseq;
	char *p = str->buf;

	str->narg = 0;
	str->buf[str->len] = '\0';

	if (*p == '\0')
		return;

	while (str->narg < STR_ARG_SIZ) {
		str->args[str->narg++] = p;
		while ((c = *p) != ';' && c != '\0')
			++p;
		if (c == '\0')
			return;
		*p++ = '\0';
	}
}

static void
strreset(void)
{
	char *buf;

	if ((buf = realloc(term->strescseq.buf, STR_BUF_SIZ)) == NULL)
		fatal("realloc: %s", errno_s);

	term->strescseq = (STREscape){
		.buf = buf,
		.siz = STR_BUF_SIZ,
	};
}

static void
tputtab(int n)
{
	int x = term->c.x;

	if (n > 0) {
		while (x < term->col && n--)
			for (++x; x < term->col && !term->tabs[x]; ++x)
				/* nothing */ ;
	} else if (n < 0) {
		while (x > 0 && n++)
			for (--x; x > 0 && !term->tabs[x]; --x)
				/* nothing */ ;
	}
	LIMIT(x, 0, term->col-1);
	if (x != term->c.x)
		tmoveto(x, term->c.y);
}

static void
tdefutf8(char ascii)
{
	if (ascii == 'G')
		term->mode |= MODE_UTF8;
	else if (ascii == '@')
		term->mode &= ~MODE_UTF8;
}

static void
tdeftran(char ascii)
{
	static char cs[] = "0B";
	static int vcs[] = {CS_GRAPHIC0, CS_USA};
	char *p;

	if (ascii != '\0' && (p = strchr(cs, ascii)) != NULL)
		term->trantbl[term->icharset] = vcs[p - cs];
}

static void
tdectest(char c)
{
	int x, y;

	if (c == '8') { /* DEC screen alignment test. */
		term->c.state &= ~CURSOR_ORIGIN;
		term->c.attr.mode = 0;
		term->c.attr.fg = COMA_VT_FG_DEFAULT;
		term->c.attr.bg = COMA_VT_BG_DEFAULT;
		tsetscroll(0, term->row-1);
		tmoveto(0, 0);
		for (x = 0; x < term->col; ++x) {
			for (y = 0; y < term->row; ++y)
				tsetchar('E', &term->c.attr, x, y);
		}
	}
}

static void
tstrsequence(uchar c)
{
	switch (c) {
	case 0x90:   /* DCS -- Device Control String */
		c = 'P';
		break;
	case 0x9f:   /* APC -- Application Program Command */
		c = '_';
		break;
	case 0x9e:   /* PM -- Privacy Message */
		c = '^';
		break;
	case 0x9d:   /* OSC -- Operating System Command */
		c = ']';
		break;
	}
	strreset();
	term->strescseq.type = c;
	term->esc |= ESC_STR;
}

static void
tcontrolcode(uchar ascii)
{
	switch (ascii) {
	case '\t':   /* HT */
		tputtab(1);
		return;
	case '\b':   /* BS */
		tmoveto(term->c.x-1, term->c.y);
		return;
	case '\r':   /* CR */
		tmoveto(0, term->c.y);
		return;
	case '\f':   /* LF */
	case '\v':   /* VT */
	case '\n':   /* LF */
		/* go to first col if the mode is set */
		tnewline(IS_SET(MODE_CRLF));
		return;
	case '\a':   /* BEL */
		if (term->esc & ESC_STR_END) {
			/* backwards compatibility to xterm */
			term->strescseq.bel = 1;
			strhandle();
		}
		break;
	case '\033': /* ESC */
		csireset();
		term->esc &= ~(ESC_CSI|ESC_ALTCHARSET|ESC_TEST);
		term->esc |= ESC_START;
		return;
	case '\016': /* SO (LS1 -- Locking shift 1) */
	case '\017': /* SI (LS0 -- Locking shift 0) */
		term->charset = 1 - (ascii - '\016');
		return;
	case '\032': /* SUB */
		tsetchar('?', &term->c.attr, term->c.x, term->c.y);
		/* FALLTHROUGH */
	case '\030': /* CAN */
		csireset();
		break;
	case '\005': /* ENQ (IGNORED) */
	case '\000': /* NUL (IGNORED) */
	case '\021': /* XON (IGNORED) */
	case '\023': /* XOFF (IGNORED) */
	case 0177:   /* DEL (IGNORED) */
		return;
	case 0x85:   /* NEL -- Next line */
		tnewline(1); /* always go to first col */
		break;
	case 0x88:   /* HTS -- Horizontal tab stop */
		term->tabs[term->c.x] = 1;
		break;
	case 0x9a:   /* DECID -- Identify Terminal */
		ttywrite(VT_IDEN, strlen(VT_IDEN));
		break;
	case 0x90:   /* DCS -- Device Control String */
	case 0x9d:   /* OSC -- Operating System Command */
	case 0x9e:   /* PM -- Privacy Message */
	case 0x9f:   /* APC -- Application Program Command */
		tstrsequence(ascii);
		return;
	default:     /* the other C1 codes, not implemented */
		break;
	}
	/* only CAN, SUB, \a and C1 chars interrupt a sequence */
	term->esc &= ~(ESC_STR_END|ESC_STR);
}

/*
 * returns 1 when the sequence is finished and it hasn't to read
 * more characters for this sequence, otherwise 0
 */
static int
eschandle(uchar ascii)
{
	switch (ascii) {
	case '[':
		term->esc |= ESC_CSI;
		return 0;
	case '#':
		term->esc |= ESC_TEST;
		return 0;
	case '%':
		term->esc |= ESC_UTF8;
		return 0;
	case 'P': /* DCS -- Device Control String */
	case '_': /* APC -- Application Program Command */
	case '^': /* PM -- Privacy Message */
	case ']': /* OSC -- Operating System Command */
	case 'k': /* old title set compatibility */
		tstrsequence(ascii);
		return 0;
	case 'n': /* LS2 -- Locking shift 2 */
	case 'o': /* LS3 -- Locking shift 3 */
		term->charset = 2 + (ascii - 'n');
		break;
	case '(': /* GZD4 -- set primary charset G0 */
	case ')': /* G1D4 -- set secondary charset G1 */
	case '*': /* G2D4 -- set tertiary charset G2 */
	case '+': /* G3D4 -- set quaternary charset G3 */
		term->icharset = ascii - '(';
		term->esc |= ESC_ALTCHARSET;
		return 0;
	case 'D': /* IND -- Linefeed */
		if (term->c.y == term->bot) {
			tscrollup(term->top, 1);
		} else {
			tmoveto(term->c.x, term->c.y+1);
		}
		break;
	case 'E': /* NEL -- Next line */
		tnewline(1); /* always go to first col */
		break;
	case 'H': /* HTS -- Horizontal tab stop */
		term->tabs[term->c.x] = 1;
		break;
	case 'M': /* RI -- Reverse index */
		if (term->c.y == term->top) {
			tscrolldown(term->top, 1);
		} else {
			tmoveto(term->c.x, term->c.y-1);
		}
		break;
	case 'Z': /* DECID -- Identify Terminal */
		ttywrite(VT_IDEN, strlen(VT_IDEN));
		break;
	case 'c': /* RIS -- Reset to initial state */
		treset();
		term->wmode = 0;
		term->cursor = 0;
		break;
	case '=': /* DECPAM -- Application keypad */
		term->wmode |= COMA_VT_MODE_APPKEYPAD;
		break;
	case '>': /* DECPNM -- Normal keypad */
		term->wmode &= ~COMA_VT_MODE_APPKEYPAD;
		break;
	case '7': /* DECSC -- Save Cursor */
		tcursor(CURSOR_SAVE);
		break;
	case '8': /* DECRC -- Restore Cursor */
		tcursor(CURSOR_LOAD);
		break;
	case '\\': /* ST -- String Terminator */
		if (term->esc & ESC_STR_END) {
			term->strescseq.bel = 0;
			strhandle();
		}
		break;
	default:
		if (BETWEEN(ascii, 0x20, 0x2f)) {
			term->esc |= ESC_IGNORE;
			return 0;
		}
		break;
	}
	return 1;
}

static void
tputc(Rune u)
{
	char c[UTF_SIZ];
	int control;
	int width, len;
	Glyph *gp;
	CSIEscape *csi = &term->csiescseq;
	STREscape *str = &term->strescseq;

	control = ISCONTROL(u);
	if (u < 127 || !IS_SET(MODE_UTF8)) {
		c[0] = u;
		width = len = 1;
	} else {
		len = utf8encode(u, c);
		if (!control && (width = wcwidth(u)) == -1)
			width = 1;
	}

	/*
	 * STR sequence must be checked before anything else
	 * because it uses all following characters until it
	 * receives a ESC, a SUB, a ST or any other C1 control
	 * character.
	 */
	if (term->esc & ESC_STR) {
		if (u == '\a' || u == 030 || u == 032 || u == 033 ||
		    ISCONTROLC1(u)) {
			term->esc &= ~(ESC_START|ESC_STR);
			term->esc |= ESC_STR_END;
			goto check_control_code;
		}

		if (str->len+len >= str->siz) {
			/*
			 * Here is a bug in terminals. If the user never sends
			 * some code to stop the str or esc command, then st
			 * will stop responding. But this is better than
			 * silently failing with unknown characters. At least
			 * then users will report back.
			 */
			if (str->siz >= STR_BUF_MAX)
				return;
			str->siz *= 2;
			if ((str->buf = realloc(str->buf, str->siz)) == NULL)
				fatal("realloc: %s", errno_s);
		}

		memmove(&str->buf[str->len], c, len);
		str->len += len;
		return;
	}

check_control_code:
	/*
	 * Actions of control codes must be performed as soon they arrive
	 * because they can be embedded inside a control sequence, and
	 * they must not cause conflicts with sequences.
	 */
	if (control) {
		/* in UTF-8 mode ignore handling C1 control characters */
		if (IS_SET(MODE_UTF8) && ISCONTROLC1(u))
			return;
		tcontrolcode(u);
		/*
		 * control codes are not shown ever
		 */
		if (!term->esc)
			term->lastc = 0;
		return;
	} else if (term->esc & ESC_START) {
		if (term->esc & ESC_CSI) {
			csi->buf[csi->len++] = u;
			if (BETWEEN(u, 0x40, 0x7E) ||
			    csi->len >= sizeof(csi->buf)-1) {
				term->esc = 0;
				csiparse();
				csihandle();
			}
			return;
		} else if (term->esc & ESC_UTF8) {
			tdefutf8(u);
		} else if (term->esc & ESC_ALTCHARSET) {
			tdeftran(u);
		} else if (term->esc & ESC_TEST) {
			tdectest(u);
		} else if (term->esc & ESC_IGNORE) {
			if (BETWEEN(u, 0x20, 0x2f))
				return;
		} else {
			if (!eschandle(u))
				return;
			/* sequence already finished */
		}
		term->esc = 0;
		/*
		 * All characters which form part of a sequence are not
		 * printed
		 */
		return;
	}

	if (width == 0) {
		tcombine(u);
		return;
	}

	gp = &term->line[term->c.y][term->c.x];
	if (IS_SET(MODE_WRAP) && (term->c.state & CURSOR_WRAPNEXT)) {
		gp->mode |= COMA_VT_ATTR_WRAP;
		tnewline(1);
		gp = &term->line[term->c.y][term->c.x];
	}

	if (IS_SET(MODE_INSERT) && term->c.x+width < term->col) {
		if (term->cb->damage != NULL) {
			term->cb->damage(term->arg, term->c.x, term->c.y,
			    term->col-1, term->c.y);
		}
		memmove(gp+width, gp,
		    (term->col - term->c.x - width) * sizeof(Glyph));
		gp->mode &= ~COMA_VT_ATTR_WIDE;
		tfixwide(term->c.x + width, term->c.y);
		tfixwide(term->col, term->c.y);
	}

	if (term->c.x+width > term->col) {
		if (IS_SET(MODE_WRAP)) {
			gp->u = '\0';
			gp->mode |= COMA_VT_ATTR_WRAP;
			tnewline(1);
		} else {
			tmoveto(term->col - width, term->c.y);
		}
		gp = &term->line[term->c.y][term->c.x];
	}

	tsetchar(u, &term->c.attr, term->c.x, term->c.y);
	term->lastc = u;

	if (width == 2) {
		gp->mode |= COMA_VT_ATTR_WIDE;
		if (term->c.x+1 < term->col) {
			if (term->cb->damage != NULL) {
				term->cb->damage(term->arg, term->c.x+1,
				    term->c.y, term->c.x+1, term->c.y);
			}
			if ((gp[1].mode & COMA_VT_ATTR_WIDE) &&
			    term->c.x+2 < term->col) {
				if (term->cb->damage != NULL) {
					term->cb->damage(term->arg,
					    term->c.x+2, term->c.y,
					    term->c.x+2, term->c.y);
				}
				gp[2].u = ' ';
				gp[2].mode &= ~COMA_VT_ATTR_WDUMMY;
				tclearcomb(&gp[2]);
			}
			gp[1].u = '\0';
			gp[1].mode = COMA_VT_ATTR_WDUMMY;
			tclearcomb(&gp[1]);
		}
	}
	if (term->c.x+width < term->col) {
		tmoveto(term->c.x+width, term->c.y);
	} else {
		term->c.state |= CURSOR_WRAPNEXT;
	}
}

static int
twrite(const char *buf, int buflen)
{
	int charsize;
	Rune u;
	int n;

	for (n = 0; n < buflen; n += charsize) {
		if (IS_SET(MODE_UTF8)) {
			/* process a complete utf8 char */
			charsize = utf8decode(buf + n, &u, buflen - n);
			if (charsize == 0)
				break;
		} else {
			u = buf[n] & 0xFF;
			charsize = 1;
		}
		tputc(u);
	}
	return n;
}

static void
tslide(Line *line, int *y, int row, int push)
{
	int i, n;

	n = MIN(*y, term->row-1) - row + 1;
	if (n < 0)
		n = 0;

	for (i = 0; i < n; i++) {
		if (push && term->cb->scrollout != NULL)
			term->cb->scrollout(term->arg, line[i], term->col);
		free(line[i]);
	}
	/* ensure that both src and dst are not NULL */
	if (n > 0)
		memmove(line, line + n, row * sizeof(Line));
	for (i = n + row; i < term->row; i++)
		free(line[i]);

	*y -= n;
}

static void
tresize(int col, int row)
{
	int i, j, alt;
	int minrow = MIN(row, term->row);
	int mincol = MIN(col, term->col);
	int *bp;
	TCursor c;

	if (col < 1 || row < 1)
		return;

	/*
	 * slide screen to keep cursor where we expect it -
	 * tscrollup would work here, but we can optimize to
	 * memmove because we're freeing the earlier lines
	 */
	alt = IS_SET(MODE_ALTSCREEN);
	tslide(term->line, &term->c.y, row, !alt);
	tslide(term->alt, &term->saved[!alt].y, row, alt);

	/* resize to new height */
	if ((term->line = realloc(term->line, row * sizeof(Line))) == NULL)
		fatal("realloc: %s", errno_s);
	if ((term->alt = realloc(term->alt, row * sizeof(Line))) == NULL)
		fatal("realloc: %s", errno_s);
	if ((term->tabs = realloc(term->tabs,
	    col * sizeof(*term->tabs))) == NULL)
		fatal("realloc: %s", errno_s);

	/* resize each row to new width, zero-pad if needed */
	for (i = 0; i < minrow; i++) {
		if ((term->line[i] = realloc(term->line[i],
		    col * sizeof(Glyph))) == NULL ||
		    (term->alt[i] = realloc(term->alt[i],
		    col * sizeof(Glyph))) == NULL)
			fatal("realloc: %s", errno_s);
	}

	/* allocate any new rows */
	for (/* i = minrow */; i < row; i++) {
		term->line[i] = coma_malloc(col * sizeof(Glyph));
		term->alt[i] = coma_malloc(col * sizeof(Glyph));
	}
	if (col > term->col) {
		bp = term->tabs + term->col;

		memset(bp, 0, sizeof(*term->tabs) * (col - term->col));
		while (--bp > term->tabs && !*bp)
			/* nothing */ ;
		for (bp += TAB_SPACES; bp < term->tabs + col; bp += TAB_SPACES)
			*bp = 1;
	}
	/* update terminal size */
	term->col = col;
	term->row = row;
	/* reset scrolling region */
	tsetscroll(0, row-1);
	/* make use of the LIMIT in tmoveto */
	tmoveto(term->c.x, term->c.y);
	/* Clearing both screens (it makes dirty all lines) */
	c = term->c;
	memcpy(c.trantbl, term->trantbl, sizeof(term->trantbl));
	c.charset = term->charset;
	for (i = 0; i < 2; i++) {
		for (j = 0; j < minrow; j++)
			tfixwide(col, j);
		if (mincol < col && 0 < minrow) {
			tclearregion(mincol, 0, col - 1, minrow - 1);
		}
		if (0 < col && minrow < row) {
			tclearregion(0, minrow, col - 1, row - 1);
		}
		tswapscreen();
		tcursor(CURSOR_LOAD);
	}
	term->c = c;
	memcpy(term->trantbl, c.trantbl, sizeof(term->trantbl));
	term->charset = c.charset;
}
