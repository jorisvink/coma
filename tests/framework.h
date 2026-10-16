/*
 * Copyright (c) 2026 Joris Vink <joris@coders.se>
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

#ifndef __H_TEST_FRAMEWORK_H
#define __H_TEST_FRAMEWORK_H

#include <SDL.h>

#include "coma.h"

#define TEST_TIMEOUT		5000

void	test_entry(void);
void	test_framework_run(void);
void	test_framework_register(const char *, void (*cb)(void));

void	test_coma_start(void);
void	test_coma_config(char *);
void	test_pump(u_int32_t);
int	test_clients(void);
int	test_find(const char *);
void	test_type(const char *);
void	test_wait(const char *);
void	test_prefix(SDL_Keycode);
void	test_resize(int, int);
void	test_until(int (*)(void), const char *);
void	test_key(SDL_Keycode, u_int16_t);
void	test_cell_pos(int, int, int *, int *);
void	test_click(int, int, u_int8_t, u_int8_t);
void	test_mouse(u_int32_t, int, int, u_int8_t, u_int8_t);

#endif
