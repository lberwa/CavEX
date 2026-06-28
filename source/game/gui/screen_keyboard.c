/*
	Copyright (c) 2024 ffCavEX

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

/* On-screen keyboard. The key layout is taken from the libwiigui keyboard
 * (Dimok, GPLv3) but rendered natively with cavex' immediate-mode gutil API
 * (no external GUI framework, no extra textures). Usable with the
 * mouse / Wii IR pointer as well as the D-pad.
 *
 * Open it from any screen with:
 *   screen_keyboard_open(buffer, sizeof(buffer), "Title", &return_screen);
 * On "OK" the edited text is copied into buffer, on "Cancel" it is left
 * unchanged; afterwards return_screen is shown again.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../graphics/gfx_settings.h"
#include "../../graphics/gui_util.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../sound.h"
#include "../game_state.h"
#include "screen.h"

#define KB_ROWS 4
#define KB_MAX_KEYS 64
#define KB_BUF_MAX 128

/* character rows (lower case / shifted), taken from gui_keyboard.cpp */
static const char* KB_LOWER[KB_ROWS] = {
	"`1234567890-=",
	"qwertyuiop[]\\",
	"asdfghjkl;'",
	"zxcvbnm,./",
};
static const char* KB_UPPER[KB_ROWS] = {
	"~!@#$%^&*()_+",
	"QWERTYUIOP{}|",
	"ASDFGHJKL:\"",
	"ZXCVBNM<>?",
};

enum kb_type {
	KT_CHAR,
	KT_SPACE,
	KT_BACK,
	KT_SHIFT,
	KT_CAPS,
	KT_OK,
	KT_CANCEL,
};

struct kb_key {
	int x, y, w, h;
	enum kb_type type;
	char ch_lower, ch_upper;
	const char* label;
	int row, col;
};

/* target / state for the currently open keyboard */
static char* kb_target;
static size_t kb_max;
static char kb_title[64];
static struct screen* kb_return;

static char kb_buf[KB_BUF_MAX];
static size_t kb_len;
static int kb_caret;      /* text insertion position, 0..kb_len */
static int kb_view_start; /* first visible character in the text box */
static bool kb_shift;
static bool kb_caps;

/* navigation cursor (D-pad); only highlighted once the user navigates.
 * kb_sel_row == -1 means the text field is the active element. */
static int kb_sel_row, kb_sel_col;
static bool kb_dpad_active;

/* blinking text caret */
static float kb_blink;
#define KB_TEXTFIELD_ROW (-1)

/* mouse / Wii IR pointer */
static bool kb_ptr_available;
static float kb_ptr_x, kb_ptr_y, kb_ptr_angle;
static float kb_last_ptr_x, kb_last_ptr_y;
static int kb_hover; /* key under the pointer, or -1 */

#define KB_TEXT_PAD (4 + GFX_GUI_SCALE)

/* layout, recomputed each frame from the current size */
static struct kb_key kb_keys[KB_MAX_KEYS];
static int kb_nkeys;
static int kb_rowcount[8];
static int kb_box_x, kb_box_y, kb_box_w, kb_box_h;
static int kb_title_x, kb_title_y;
static int kb_font; /* glyph size in pixels */

/* description of the special (bottom) row: label, width in cells, type */
struct kb_special {
	const char* label;
	int cells;
	enum kb_type type;
};
static const struct kb_special KB_SPECIAL[] = {
	{"Caps", 2, KT_CAPS},   {"Shift", 2, KT_SHIFT}, {"Space", 5, KT_SPACE},
	{"Back", 2, KT_BACK},   {"OK", 2, KT_OK},       {"Cancel", 3, KT_CANCEL},
};
#define KB_SPECIAL_COUNT ((int)(sizeof(KB_SPECIAL) / sizeof(KB_SPECIAL[0])))

static void kb_layout(int width, int height, int scale) {
	int cell = 16 * scale;
	int gap = 2 * scale;
	int pitch = cell + gap;

	kb_font = 8 * scale;
	kb_nkeys = 0;
	for(int i = 0; i < 8; i++)
		kb_rowcount[i] = 0;

	/* total block = text box + 4 char rows + 1 special row */
	int rows = KB_ROWS + 1;
	int rows_h = rows * pitch - gap;
	kb_box_w = 13 * pitch - gap; /* as wide as the longest char row */
	kb_box_h = cell;
	int block_h = kb_box_h + gap * 2 + rows_h;

	int top = (height - block_h) / 2;
	if(top < kb_font * 2)
		top = kb_font * 2; /* leave room for the title */

	kb_box_x = (width - kb_box_w) / 2;
	kb_box_y = top;
	kb_title_x = kb_box_x;
	kb_title_y = kb_box_y - kb_font - gap;

	int grid_y = kb_box_y + kb_box_h + gap * 2;

	/* character rows */
	for(int r = 0; r < KB_ROWS; r++) {
		int n = (int)strlen(KB_LOWER[r]);
		int row_w = n * pitch - gap;
		int row_x = (width - row_w) / 2;
		int row_y = grid_y + r * pitch;
		for(int i = 0; i < n; i++) {
			struct kb_key* k = &kb_keys[kb_nkeys++];
			k->x = row_x + i * pitch;
			k->y = row_y;
			k->w = cell;
			k->h = cell;
			k->type = KT_CHAR;
			k->ch_lower = KB_LOWER[r][i];
			k->ch_upper = KB_UPPER[r][i];
			k->label = NULL;
			k->row = r;
			k->col = i;
		}
		kb_rowcount[r] = n;
	}

	/* special row */
	int total_cells = 0;
	for(int i = 0; i < KB_SPECIAL_COUNT; i++)
		total_cells += KB_SPECIAL[i].cells;
	int srow_w = total_cells * pitch - gap;
	int srow_x = (width - srow_w) / 2;
	int srow_y = grid_y + KB_ROWS * pitch;
	int cx = srow_x;
	for(int i = 0; i < KB_SPECIAL_COUNT; i++) {
		struct kb_key* k = &kb_keys[kb_nkeys++];
		k->x = cx;
		k->y = srow_y;
		k->w = KB_SPECIAL[i].cells * pitch - gap;
		k->h = cell;
		k->type = KB_SPECIAL[i].type;
		k->ch_lower = k->ch_upper = 0;
		k->label = KB_SPECIAL[i].label;
		k->row = KB_ROWS;
		k->col = i;
		cx += KB_SPECIAL[i].cells * pitch;
	}
	kb_rowcount[KB_ROWS] = KB_SPECIAL_COUNT;
}

static int kb_index(int row, int col) {
	if(row < 0)
		row = 0;
	if(row > KB_ROWS)
		row = KB_ROWS;
	if(col < 0)
		col = 0;
	if(col >= kb_rowcount[row])
		col = kb_rowcount[row] - 1;
	for(int i = 0; i < kb_nkeys; i++)
		if(kb_keys[i].row == row && kb_keys[i].col == col)
			return i;
	return 0;
}

static char kb_char_of(const struct kb_key* k) {
	bool upper = kb_shift;
	if(isalpha((unsigned char)k->ch_lower))
		upper = kb_shift ^ kb_caps;
	return upper ? k->ch_upper : k->ch_lower;
}

/* pixel width of kb_buf[from..to) at the current font size */
static int kb_sub_width(int from, int to) {
	if(to <= from)
		return 0;
	char tmp[KB_BUF_MAX];
	int n = to - from;
	memcpy(tmp, kb_buf + from, n);
	tmp[n] = 0;
	return gutil_font_width(tmp, kb_font);
}

/* scroll the text box so the caret stays visible */
static void kb_update_view(void) {
	int avail = kb_box_w - 2 * KB_TEXT_PAD;
	if(kb_caret < kb_view_start)
		kb_view_start = kb_caret;
	while(kb_view_start < kb_caret
		  && kb_sub_width(kb_view_start, kb_caret) > avail)
		kb_view_start++;
}

/* map a click x (logical coords) inside the text box to a caret position */
static int kb_caret_from_x(float clickx) {
	int rel = (int)clickx - (kb_box_x + KB_TEXT_PAD);
	if(rel <= 0)
		return kb_view_start;
	int prev = 0;
	for(int i = kb_view_start; i < (int)kb_len; i++) {
		int w = kb_sub_width(kb_view_start, i + 1);
		if(w > rel)
			return (rel < (prev + w) / 2) ? i : i + 1;
		prev = w;
	}
	return (int)kb_len;
}

/* insert a character at the caret */
static void kb_insert(char c) {
	if(kb_len + 1 >= kb_max || kb_len + 1 >= KB_BUF_MAX)
		return;
	memmove(kb_buf + kb_caret + 1, kb_buf + kb_caret, kb_len - kb_caret + 1);
	kb_buf[kb_caret] = c;
	kb_caret++;
	kb_len++;
	kb_blink = 0.0F; /* show the caret right after typing */
}

static void kb_press(int idx) {
	if(idx < 0 || idx >= kb_nkeys)
		return;
	struct kb_key* k = &kb_keys[idx];

	switch(k->type) {
		case KT_CHAR:
			kb_insert(kb_char_of(k));
			kb_shift = false;
			break;
		case KT_SPACE:
			kb_insert(' ');
			kb_shift = false;
			break;
		case KT_BACK:
			if(kb_caret > 0) {
				memmove(kb_buf + kb_caret - 1, kb_buf + kb_caret,
						kb_len - kb_caret + 1);
				kb_caret--;
				kb_len--;
				kb_blink = 0.0F;
			}
			break;
		case KT_SHIFT: kb_shift = !kb_shift; break;
		case KT_CAPS: kb_caps = !kb_caps; break;
		case KT_OK:
			if(kb_target && kb_max > 0) {
				strncpy(kb_target, kb_buf, kb_max);
				kb_target[kb_max - 1] = 0;
			}
			sound_play(pcm_click);
			screen_set(kb_return);
			return;
		case KT_CANCEL:
			sound_play(pcm_click);
			screen_set(kb_return);
			return;
	}
	sound_play(pcm_click);
}

static void screen_keyboard_reset(struct screen* s, int width, int height) {
	(void)s;
	(void)width;
	(void)height;
	input_pointer_enable(true);
	kb_sel_row = 0;
	kb_sel_col = 0;
}

static void screen_keyboard_update(struct screen* s, float dt) {
	(void)s;
	kb_blink += dt;
	int player = gstate_active_player();

	/* use the logical GUI size (constant regardless of the current render pass)
	 * so the layout matches render2D and the pointer lands on the right key */
	int view_w = gfx_gui_width();
	int view_h = gfx_gui_height();
	kb_layout(view_w, view_h, GFX_GUI_SCALE);

	if(input_pressed(IB_BACK, player)) {
		sound_play(pcm_click);
		screen_set(kb_return);
		return;
	}

	/* pointer: find the hovered key. A real mouse movement (not tiny jitter)
	 * switches back to pointer mode; otherwise the current mode is kept, so the
	 * D-pad selection is never glued to where the mouse happens to rest. */
	kb_ptr_available = screen_pointer_local(player, view_w, view_h, &kb_ptr_x,
											&kb_ptr_y, &kb_ptr_angle);
	kb_hover = -1;
	if(kb_ptr_available) {
		for(int i = 0; i < kb_nkeys; i++) {
			struct kb_key* k = &kb_keys[i];
			if(kb_ptr_x >= k->x && kb_ptr_x < k->x + k->w && kb_ptr_y >= k->y
			   && kb_ptr_y < k->y + k->h) {
				kb_hover = i;
				break;
			}
		}
		if(fabsf(kb_ptr_x - kb_last_ptr_x) > 2.0F
		   || fabsf(kb_ptr_y - kb_last_ptr_y) > 2.0F)
			kb_dpad_active = false;
		kb_last_ptr_x = kb_ptr_x;
		kb_last_ptr_y = kb_ptr_y;
	}

	/* D-pad navigation: a direction press enters (and keeps) D-pad mode and
	 * stays active until the mouse is actually moved. Works with the mouse on
	 * screen, so you never have to move the cursor out of the window. */
	bool left = input_pressed(IB_GUI_LEFT, player);
	bool right = input_pressed(IB_GUI_RIGHT, player);
	bool up = input_pressed(IB_GUI_UP, player);
	bool down = input_pressed(IB_GUI_DOWN, player);
	if(left || right || up || down) {
		if(!kb_dpad_active) {
			/* start navigating from the key under the cursor, if any */
			kb_dpad_active = true;
			if(kb_hover >= 0) {
				kb_sel_row = kb_keys[kb_hover].row;
				kb_sel_col = kb_keys[kb_hover].col;
			}
		} else if(kb_sel_row == KB_TEXTFIELD_ROW) {
			/* text field is active: arrows move the caret, down leaves it */
			if(left && kb_caret > 0)
				kb_caret--;
			if(right && kb_caret < (int)kb_len)
				kb_caret++;
			if(down)
				kb_sel_row = 0; /* back to the top key row */
			kb_blink = 0.0F;
		} else {
			/* on the keys */
			if(left)
				kb_sel_col--;
			if(right)
				kb_sel_col++;
			if(up)
				kb_sel_row = (kb_sel_row == 0) ? KB_TEXTFIELD_ROW
											   : kb_sel_row - 1;
			if(down)
				kb_sel_row++;

			if(kb_sel_row > KB_ROWS)
				kb_sel_row = KB_ROWS;
			if(kb_sel_row >= 0) {
				if(kb_sel_col < 0)
					kb_sel_col = 0;
				if(kb_sel_col >= kb_rowcount[kb_sel_row])
					kb_sel_col = kb_rowcount[kb_sel_row] - 1;
			}
		}
	}

	if(input_pressed(IB_GUI_CLICK, player)) {
		if(kb_dpad_active) {
			/* D-pad mode: press the selected key (text field has no action) */
			if(kb_sel_row >= 0)
				kb_press(kb_index(kb_sel_row, kb_sel_col));
		} else if(kb_ptr_available) {
			if(kb_hover >= 0) {
				kb_press(kb_hover);
			} else if(kb_ptr_x >= kb_box_x && kb_ptr_x < kb_box_x + kb_box_w
					  && kb_ptr_y >= kb_box_y
					  && kb_ptr_y < kb_box_y + kb_box_h) {
				/* click on the text -> move the caret to that position */
				kb_update_view();
				kb_caret = kb_caret_from_x(kb_ptr_x);
				kb_blink = 0.0F;
				sound_play(pcm_click);
			}
		}
	}
}

/* button atlas regions in texture_gui2 (same as screen_mainmenu.c) */
#define KB_BTN_SX 0
#define KB_BTN_SW 200
#define KB_BTN_SH 20
#define KB_BTN_SY_DARK 42
#define KB_BTN_SY_BRIGHT 62

static void screen_keyboard_render2D(struct screen* s, int width, int height) {
	(void)s;
	kb_layout(width, height, GFX_GUI_SCALE);

	gutil_bg();

	/* title */
	if(kb_title[0])
		gutil_text(kb_title_x, kb_title_y, kb_title, kb_font, true);

	/* text box: bright when it is the active (D-pad selected) element */
	bool textfield_active = kb_dpad_active && kb_sel_row == KB_TEXTFIELD_ROW;
	gfx_bind_texture(&texture_gui2);
	gutil_texquad(kb_box_x, kb_box_y, KB_BTN_SX,
				  textfield_active ? KB_BTN_SY_BRIGHT : KB_BTN_SY_DARK,
				  KB_BTN_SW, KB_BTN_SH, kb_box_w, kb_box_h);

	/* text: scrolled so the caret stays visible, with a caret bar */
	kb_update_view();
	int avail = kb_box_w - 2 * KB_TEXT_PAD;
	int view_end = kb_view_start;
	while(view_end < (int)kb_len
		  && kb_sub_width(kb_view_start, view_end + 1) <= avail)
		view_end++;

	char shown[KB_BUF_MAX];
	int n = view_end - kb_view_start;
	memcpy(shown, kb_buf + kb_view_start, n);
	shown[n] = 0;

	int text_y = kb_box_y + (kb_box_h - kb_font) / 2;
	gutil_text(kb_box_x + KB_TEXT_PAD, text_y, shown, kb_font, true);

	/* blinking caret (always visible right after an edit; resets via kb_blink) */
	if(fmodf(kb_blink, 1.0F) < 0.5F) {
		int caret_x
			= kb_box_x + KB_TEXT_PAD + kb_sub_width(kb_view_start, kb_caret);
		gfx_texture(false);
		gutil_texquad_col(caret_x, text_y, 0, 0, 0, 0,
						  GFX_GUI_SCALE < 1 ? 1 : GFX_GUI_SCALE, kb_font, 255,
						  255, 255, 255);
		gfx_texture(true);
	}

	/* which key is highlighted: pointer mode -> only the hovered key (none if
	 * the pointer is not over a key); otherwise the D-pad selection, but only
	 * once the user navigates and not while the text field is the active element
	 * (nothing highlighted by default) */
	int sel = -1;
	if(kb_dpad_active) {
		if(kb_sel_row >= 0)
			sel = kb_index(kb_sel_row, kb_sel_col);
	} else if(kb_ptr_available) {
		sel = kb_hover;
	}

	for(int i = 0; i < kb_nkeys; i++) {
		struct kb_key* k = &kb_keys[i];

		/* bright when highlighted or toggled on (Shift/Caps) */
		bool active = (k->type == KT_SHIFT && kb_shift)
			|| (k->type == KT_CAPS && kb_caps);
		bool bright = (i == sel) || active;

		gfx_bind_texture(&texture_gui2);
		gutil_texquad(k->x, k->y, KB_BTN_SX,
					  bright ? KB_BTN_SY_BRIGHT : KB_BTN_SY_DARK, KB_BTN_SW,
					  KB_BTN_SH, k->w, k->h);

		char label[8];
		const char* txt;
		if(k->type == KT_CHAR) {
			label[0] = kb_char_of(k);
			label[1] = 0;
			txt = label;
		} else {
			txt = k->label;
		}

		int tw = gutil_font_width(txt, kb_font);
		gutil_text(k->x + (k->w - tw) / 2, k->y + (k->h - kb_font) / 2, txt,
				   kb_font, true);
	}

	if(kb_ptr_available) {
		gfx_bind_texture_virtual(&texture_pointer);
		gutil_texquad_rt_any(kb_ptr_x, kb_ptr_y, glm_rad(kb_ptr_angle), 0, 0,
							 256, 256, 48 * GFX_GUI_SCALE, 48 * GFX_GUI_SCALE);
	}
}

void screen_keyboard_open(char* target, size_t max_len, const char* title,
						  struct screen* on_done) {
	kb_target = target;
	kb_max = (max_len < KB_BUF_MAX) ? max_len : KB_BUF_MAX;
	kb_return = on_done;

	snprintf(kb_title, sizeof(kb_title), "%s", title ? title : "");

	if(target && kb_max > 0) {
		strncpy(kb_buf, target, kb_max);
		kb_buf[kb_max - 1] = 0;
	} else {
		kb_buf[0] = 0;
	}
	kb_len = strlen(kb_buf);
	kb_caret = (int)kb_len;
	kb_view_start = 0;
	kb_blink = 0.0F;

	kb_shift = false;
	kb_caps = false;
	kb_sel_row = 0;
	kb_sel_col = 0;
	kb_dpad_active = false;
	kb_hover = -1;

	screen_set(&screen_keyboard);
}

struct screen screen_keyboard = {
	.reset = screen_keyboard_reset,
	.update = screen_keyboard_update,
	.render2D = screen_keyboard_render2D,
	.render3D = NULL,
	.render_world = false,
};
