/*
	Copyright (c) 2023 ByteBit/xtreme8000

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

#include <math.h>
#include <string.h>
#include <stdio.h>

#include "../platform/gfx.h"
#include "gui_util.h"
#include "gfx_settings.h"
#include "render_block.h"
#include "texture_atlas.h"
#include "../game/game_state.h"

static int gutil_text_collor = 15;
static int gutil_gui_scale = GFX_GUI_SCALE;

void gutil_set_gui_scale(int scale) {
	gutil_gui_scale = scale > 0 ? scale : 1;
}

int gutil_get_gui_scale(void) {
	return gutil_gui_scale;
}

int gutil_control_icon(int x, enum input_button b, const char* str) {
	int symbol, symbol_help;
	enum input_category category;

	if(!input_symbol(b, &symbol, &symbol_help, &category, 1))
		return 0;

	gfx_bind_texture_virtual(&texture_controls);
	int scale = 16 * gutil_gui_scale;
	int text_scale = 5 * gutil_gui_scale;

	gutil_texquad(x, gfx_height() - scale * 8 / 5, (symbol_help % 8) * 32,
				  (symbol_help / 8) * 32 * 2, 32, 32 * 2, scale, scale);
	gutil_text(x + scale + text_scale / 2,
			   gfx_height() - scale * 8 / 5 + (scale - text_scale) / 2, str,
			   text_scale, true);
	return scale + text_scale + gutil_font_width(str, text_scale);
}

void gutil_texquad_col(int x, int y, int tx, int ty, int sx, int sy, int width,
					   int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	gfx_draw_quads(
		4,
		(int16_t[]) {x, y, -2, x + width, y, -2, x + width, y + height, -2, x,
					 y + height, -2},
		(uint8_t[]) {r, g, b, a, r, g, b, a, r, g, b, a, r, g, b, a},
		(uint16_t[]) {tx, ty, tx + sx, ty, tx + sx, ty + sy, tx, ty + sy});
}

void gutil_texquad(int x, int y, int tx, int ty, int sx, int sy, int width,
				   int height) {
	gutil_texquad_col(x, y, tx, ty, sx, sy, width, height, 0xFF, 0xFF, 0xFF,
					  0xFF);
}

void gutil_texquad_rt(int x, int y, int tx, int ty, int sx, int sy, int width,
					  int height) {
	gfx_draw_quads(
		4,
		(int16_t[]) {x, y, -2, x + width, y, -2, x + width, y + height, -2, x,
					 y + height, -2},
		(uint8_t[]) {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
		(uint16_t[]) {tx, ty + sy, tx, ty, tx + sx, ty, tx + sx, ty + sy});
}

void gutil_texquad_rt_any(int x, int y, float angle, int tx, int ty, int sx,
						  int sy, float width, float height) {
	width *= 0.707107F; // 1 / sqrt(2)
	height *= 0.707107F;
	angle -= glm_rad(45.0F);

	gfx_draw_quads(
		4,
		(int16_t[]) {x + sinf(angle) * width, y - cosf(angle) * height, -2,
					 x + cosf(angle) * width, y + sinf(angle) * height, -2,
					 x - sinf(angle) * width, y + cosf(angle) * height, -2,
					 x - cosf(angle) * width, y - sinf(angle) * height, -2},
		(uint8_t[]) {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
		(uint16_t[]) {tx, ty, tx + sx, ty, tx + sx, ty + sy, tx, ty + sy});
}

void gutil_bg_block(int texat_id) {
	gfx_bind_texture_pixels(&texture_terrain);

	int scale = 16 * 4;
	int w = gfx_width();
	int h = gfx_height();
	int cx = (w + scale - 1) / scale;
	int cy = (h + scale - 1) / scale;

	uint16_t tex = tex_atlas_lookup(texat_id);
	uint16_t s = TEX_OFFSET(TEXTURE_X(tex));
	uint16_t t = TEX_OFFSET(TEXTURE_Y(tex));

	for(int y = 0; y < cy; y++) {
		for(int x = 0; x < cx; x++) {
			gutil_texquad_col(x * scale, y * scale, s, t, 16, 16, scale, scale,
							  0x40, 0x40, 0x40, 0xFF);
		}
	}

}

void gutil_bg() {
	gutil_bg_block(TEXAT_DIRT);
}

static uint8_t font_char_width[256];

void gutil_reset_font(struct tex_gfx* tex) {
	assert(tex);

	int char_width = tex->width / 16;
	int char_height = tex->height / 16;

	for(int y = 0; y < 16; y++) {
		for(int x = 0; x < 16; x++) {
			int width = 0;

			for(int i = 0; i < char_width; i++) {
				bool has_pixel = false;

				for(int j = 0; j < char_height; j++) {
					uint8_t col[4];
					tex_gfx_lookup(tex, x * char_width + i, y * char_height + j,
								   col);

					if(col[0] || col[1] || col[2]) {
						has_pixel = true;
						break;
					}
				}

				if(!has_pixel)
					break;

				width++;
			}

			font_char_width[x + y * 16] = width * 8 / char_width;
		}
	}

	font_char_width[' '] = 4;
}

int gutil_font_width(const char* str, int scale) {
	int x = 0;

	int skip = 0;

	while(*str) {
		if(*str == '\247')
			skip = 2;

		if(skip > 0) {
			skip--;
		} else {
			x += (font_char_width[(int)*str] + 1) * scale / 8;
		}

		str++;
	}

	return x;
}

static const uint8_t chat_colors[16][3] = {
	{0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00},
	{0x00, 0xAA, 0xAA}, {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA},
	{0xFF, 0xAA, 0x00}, {0xAA, 0xAA, 0xAA}, {0x55, 0x55, 0x55},
	{0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
	{0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55},
	{0xFF, 0xFF, 0xFF},
};

int gutil_text_col(int col) {
	if (col < 0 || col > 15)
		return gutil_text_collor;
	else {
		gutil_text_collor = col;
		return gutil_text_collor;
	}
}

void gutil_text(int x, int y, const char* str, int scale, bool shadow) {
	gfx_bind_texture_virtual(&texture_font);

	int skip = 0;
	int col = gutil_text_collor;//15

	while(*str) {
		if(*str == '\247')
			skip = 2;

		if(skip > 0) {
			skip--;

			if(*str >= '0' && *str <= '9')
				col = *str - '0';

			if(*str >= 'a' && *str <= 'f')
				col = *str - 'a' + 10;
		} else {
			uint16_t tex_x = *str % 16 * 16;
			uint16_t tex_y = *str / 16 * 16;
			uint8_t width = (font_char_width[(int)*str] + 1) * scale / 8;

			if(shadow) {
				gfx_draw_quads(
					4,
					(int16_t[]) {x + scale / 8, y + scale / 8, -2,
								 x + scale + scale / 8, y + scale / 8, -2,
								 x + scale + scale / 8, y + scale + scale / 8,
								 -2, x + scale / 8, y + scale + scale / 8, -2},
					(uint8_t[]) {
						chat_colors[col][0] / 4, chat_colors[col][1] / 4,
						chat_colors[col][2] / 4, 0xFF, chat_colors[col][0] / 4,
						chat_colors[col][1] / 4, chat_colors[col][2] / 4, 0xFF,
						chat_colors[col][0] / 4, chat_colors[col][1] / 4,
						chat_colors[col][2] / 4, 0xFF, chat_colors[col][0] / 4,
						chat_colors[col][1] / 4, chat_colors[col][2] / 4, 0xFF},
					(uint16_t[]) {tex_x, tex_y, tex_x + 16, tex_y, tex_x + 16,
								  tex_y + 16, tex_x, tex_y + 16});
			}

			gfx_draw_quads(
				4,
				(int16_t[]) {x, y, -1, x + scale, y, -1, x + scale, y + scale,
							 -1, x, y + scale, -1},
				(uint8_t[]) {chat_colors[col][0], chat_colors[col][1],
							 chat_colors[col][2], 0xFF, chat_colors[col][0],
							 chat_colors[col][1], chat_colors[col][2], 0xFF,
							 chat_colors[col][0], chat_colors[col][1],
							 chat_colors[col][2], 0xFF, chat_colors[col][0],
							 chat_colors[col][1], chat_colors[col][2], 0xFF},
				(uint16_t[]) {tex_x, tex_y, tex_x + 16, tex_y, tex_x + 16,
							  tex_y + 16, tex_x, tex_y + 16});

			x += width;
		}

		str++;
	}
}

void gutil_draw_item(struct item_data* item, int x, int y, int layer) {
	assert(item);
	int scale = gutil_gui_scale;

	struct item* it = item_get(item);

	if(it) {
		mat4 model;
		glm_translate_make(model, (vec3) {x, y, 0});

		gfx_depth_range(0.1F * layer, 0.1F * (layer + 1));
		gfx_write_buffers(true, true, true);
		it->renderItem(it, item, model, true, R_ITEM_ENV_INVENTORY);
		gfx_write_buffers(true, false, false);
		gfx_depth_range(0.0F, 1.0F);

		if(it->has_damage && item->durability > 0) {
			gfx_texture(false);
			gutil_texquad_col(x + 2 * scale, y + 13 * scale, 0, 0, 0, 0,
			                  13 * scale, 2 * scale, 0, 0, 0, 255);
			gutil_texquad_col(
				x + 2 * scale, y + 13 * scale, 0, 0, 0, 0,
				(int)(13 * scale * (1.0F - (float)item->durability
				                    / (float)it->max_damage)),
				scale, 4, 251, 0, 255);
			gfx_texture(true);
		}

		if(item->count > 1) {
			char count[4];
			snprintf(count, sizeof(count), "%u", item->count);
			gutil_text(17 * scale - gutil_font_width(count, 8 * scale) + x,
			           y + 9 * scale, count, 8 * scale, true);
		}
	} else {
		char tmp[16];
		snprintf(tmp, sizeof(tmp), "%u", item->id);
		gutil_text(17 * scale - gutil_font_width(tmp, 8 * scale) + x,
		           y + scale, tmp, 8 * scale, true);

		snprintf(tmp, sizeof(tmp), "%u", item->count);
		gutil_text(17 * scale - gutil_font_width(tmp, 8 * scale) + x,
		           y + 9 * scale, tmp, 8 * scale, true);
	}
}

void gutil_window(int x, int y, int width, int height, char title[]) {
	gfx_bind_texture(&texture_gui2);

	//black background
	gfx_texture(false);
	gutil_texquad_col(x+5, y+5, 0, 0, 0, 0, width-10, height-10, 0, 0, 0, 180);
	gfx_texture(true);


	gutil_texquad(x, y + 117-82, 0, 137, 12, 1, 12, height - ((196-183) + (117-82)));//   	←

	gutil_texquad(x + 12, y+height-(196-183), 42, 183, 1, 196-183,
				  width - ((110-96) + 12), 196-183); //		 			    				↓

	gutil_texquad(x + width - (110-96), y + 117-82, 96, 138, 110-96, 1,
				  110-96, height - ((196-183) + (117-82))); //		 				 		→

	gutil_texquad(x + 12, y, 36, 82, 1, 117 - 82, width - ((110-96) + 12), 117-82);//		↑



	gutil_texquad(x, y, 0, 82, 13, 118-82, 13, 118-82);// 						 	 		↑←
	
	gutil_texquad(x, y+height-(196-182), 0, 182, 13, 196-182, 13, 
				  y+height - (y+height - (196-182)));//								 		↓←
	
	gutil_texquad(x+width-(110-95), y+height-(196-182), 95, 182, 110-95, 196-182,
				  x+width - (x+width-(110-95)), y+height - (y+height-(196-182)));//	 		↓→
	
	gutil_texquad(x+width-(110-95), y, 95, 82, 110-95 ,118-82,
				  x+width - (x+width-(110-95)), 118-82);//					 		 		↑→

				  
	int skale = 16;
	int middle = x + width / 2;

	int w = gutil_font_width(title, skale);

	int bfor_collor = gutil_text_col(100);
	gutil_text_col(0);
	gutil_text(middle - w/2, y + 10, title, skale, false);
	gutil_text_col(bfor_collor);
}

float scroll_x = 0.0f;
float speed = 0.2f;

int w[12] = {256,256,256,256,256,59,
             256,256,256,256,256,59};

int h[12] = {256,256,256,256,256,256,
             194,194,194,194,194,194};

int total_width;


void gutil_bg_panorama() {
	total_width = 0;
	//			-------1------
	for (int i=0; i<6; i++) {
		total_width += w[i];
	}

    float x = -scroll_x;

	int hp = 20;
    
	for(int i=0; i<6; i++) {
        gfx_bind_texture(&texture_bg[i]);

        gutil_texquad(
            (int)x, 0,      		// Position auf dem Bildschirm
            0, 0,           		// Texture-Start
            w[i], h[i],     		// Texture-Dimension
            w[i], h[i] + hp 		// Quad-Dimension
        );

        // Wrap-Kopie:
        gutil_texquad(
            (int)(x + total_width), 0,
            0, 0,
            w[i], h[i],
            w[i], h[i] + hp
        );

        x += w[i];
    }

	//		--------2---------
	total_width = 0;
	for (int i=0; i<6; i++) {
		total_width += w[i+6];
	}

    x = -scroll_x;

	int hp2 = 12;

    for(int i=0; i<6; i++) {
		int i6 = i + 6;
        gfx_bind_texture(&texture_bg[i6]);

        gutil_texquad(
            (int)x, h[1] + hp,      // Position auf dem Bildschirm
            0, 0,           		// Texture-Start
            w[i6], h[i6],     		// Texture-Dimension
            w[i6], h[i6] + hp2     	// Quad-Dimension
        );

        // Wrap-Kopie:
        gutil_texquad(
            (int)(x + total_width), h[1] + hp,
            0, 0,
            w[i6], h[i6],
            w[i6], h[i6] + hp2
        );

        x += w[i6];
    }

    scroll_x += speed;

    if(scroll_x >= total_width)
        scroll_x -= total_width;
}

void gutil_license(int width, int height) {
	size_t size = 6 * GFX_GUI_SCALE;
	gutil_text(width - gutil_font_width(LICENSE, size) - 5, 
			height - 2 * GFX_GUI_SCALE - (9 * GFX_GUI_SCALE) * 2 + GFX_GUI_SCALE, 
            LICENSE, size, true); 
	gutil_text(width - gutil_font_width(COPYRIGHT, size) - 5, 
			height - 2 * GFX_GUI_SCALE - (9 * GFX_GUI_SCALE) * 1 + GFX_GUI_SCALE*2, 
            COPYRIGHT, size, true);
	
	char str[64];
	snprintf(str, sizeof(str),
	         GAME_NAME " Alpha %i.%i.%i_f%i (impl. %s)",
	         VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_FORK, VERSION_IMPL);
	gutil_text(5, height - 2 * GFX_GUI_SCALE - (9 * GFX_GUI_SCALE) * 1 + GFX_GUI_SCALE*2, 
				str, size, true);
}

#define b_size 16
#define x_max  3
#define y_max  6
#define toggle_width 90
#define toggle_height 40

static struct button buttons[b_size];
static int8_t enable_buttons = 0;
static int choosen_pos[2] = {-1, -1};
static bool buttons_pos[x_max][y_max] = { false };

// Pointer-Status (einmal pro Frame in gutil_button_reset ermittelt)
static bool ptr_available;
static float ptr_x, ptr_y;
static float last_ptr_x, last_ptr_y;
// true = D-Pad-Navigation aktiv, false = Maus/Pointer steuert die Auswahl
static bool dpad_active;
// gui_click einmal pro Frame abfragen: input_native_key_status() markiert die
// Taste bei der ERSTEN Abfrage als "held", spaetere Abfragen im selben Frame
// liefern dann faelschlich false. Pro Button abzufragen wuerde also nur den
// ersten Button klickbar machen.
static bool button_click;
static int button_player;

void gutil_button_reset(int player, bool ptr_avail, float px, float py) {
	button_player = player;
	memset(buttons, 0, sizeof(buttons));
	memset(buttons_pos, 0, sizeof(buttons_pos));
	enable_buttons = 0;

	button_click = input_pressed(IB_GUI_CLICK, player);

	ptr_available = ptr_avail;
	if (ptr_available) {
		ptr_x = px;
		ptr_y = py;
		// eine echte Mausbewegung (> 2 px) schaltet zurueck in den Pointer-Modus
		if (fabsf(ptr_x - last_ptr_x) > 2.0F
		    || fabsf(ptr_y - last_ptr_y) > 2.0F) {
			dpad_active = false;
			// Maus uebernimmt -> D-Pad-Auswahl verwerfen, naechster
			// Pfeildruck startet wieder bei (0,0)
			choosen_pos[0] = -1;
			choosen_pos[1] = -1;
		}
		last_ptr_x = ptr_x;
		last_ptr_y = ptr_y;
	}
}

void gutil_button(int x, int y, int width, int height,
					const char* text, void (*func)(int),
					int arg, int pos_x, int pos_y)
{
	buttons[enable_buttons].x = x;
	buttons[enable_buttons].y = y;
	buttons[enable_buttons].width  = width;
	buttons[enable_buttons].height = height;
	buttons[enable_buttons].text = text;
	buttons[enable_buttons].type = BUTTON_BUTTON;

	bool hover = ptr_available
		&& ptr_x >= x && ptr_x < x + width
		&& ptr_y >= y && ptr_y < y + height;
	bool selected = choosen_pos[0] == pos_x && choosen_pos[1] == pos_y;

	buttons[enable_buttons].choosen = dpad_active ? selected : hover;

	if (button_click && buttons[enable_buttons].choosen) {
		func(arg);
	}

	if (pos_x >= 0 && pos_x < x_max && pos_y >= 0 && pos_y < y_max)
		buttons_pos[pos_x][pos_y] = true;

	enable_buttons++;
}

void gutil_button_update() {
	int dx = 0, dy = 0;
	if (input_pressed(IB_GUI_RIGHT, button_player))
		dx = 1;
	else if (input_pressed(IB_GUI_LEFT, button_player))
		dx = -1;
	else if (input_pressed(IB_GUI_DOWN, button_player))
		dy = 1;
	else if (input_pressed(IB_GUI_UP, button_player))
		dy = -1;

	if (dx == 0 && dy == 0)
		return;

	dpad_active = true; // ein Pfeildruck aktiviert die D-Pad-Navigation

	// Noch nichts ausgewählt -> ersten vorhandenen Button (links oben) wählen
	if (choosen_pos[0] < 0 || choosen_pos[1] < 0) {
		for (int y = 0; y < y_max; y++) {
			for (int x = 0; x < x_max; x++) {
				if (buttons_pos[x][y]) {
					choosen_pos[0] = x;
					choosen_pos[1] = y;
					return;
				}
			}
		}
		return;
	}

	// Zickzack-Suche: Ziel ist die Nachbar-Spalte/-Zeile in Richtung (dx,dy).
	// Quer dazu wird ausgehend von der aktuellen Position gesucht:
	// zuerst gleiche Linie, dann +1, -1, +2, -2, ... (unten/rechts zuerst).
	if (dx != 0) {
		int col = choosen_pos[0] + dx; // feste Zielspalte
		if (col < 0 || col >= x_max)
			return; // am Rand -> Auswahl bleibt
		int base = choosen_pos[1];
		for (int i = 0; i < 2 * y_max; i++) {
			int delta = (i + 1) / 2;
			int row = (i % 2 == 1) ? base + delta : base - delta;
			if (row < 0 || row >= y_max)
				continue;
			if (buttons_pos[col][row]) {
				choosen_pos[0] = col;
				choosen_pos[1] = row;
				return;
			}
		}
	} else {
		int row = choosen_pos[1] + dy; // feste Zielzeile
		if (row < 0 || row >= y_max)
			return;
		int base = choosen_pos[0];
		for (int i = 0; i < 2 * x_max; i++) {
			int delta = (i + 1) / 2;
			int col = (i % 2 == 1) ? base + delta : base - delta;
			if (col < 0 || col >= x_max)
				continue;
			if (buttons_pos[col][row]) {
				choosen_pos[0] = col;
				choosen_pos[1] = row;
				return;
			}
		}
	}
}

void gutil_button_render() {
	for (int i=0; i<enable_buttons; i++) {
	  if (buttons[i].type == BUTTON_BUTTON) {
    	int tex_x = 0;
    	int tex_y = buttons[i].choosen ? 62 : 42; // hell : dunkel
    	int tex_w = 200;
    	int tex_h = 20;

		gfx_bind_texture(&texture_gui2);
    	gutil_texquad(buttons[i].x, buttons[i].y, tex_x, tex_y, 
					  tex_w, tex_h, buttons[i].width, buttons[i].height);


    	gutil_text(buttons[i].x + buttons[i].width/2 - (gutil_font_width(buttons[i].text, 20)/2),
				   buttons[i].y, buttons[i].text, 20, true);
		
	  } else if (buttons[i].type == BUTTON_TOGGLE) {
		int tex_x = 0;
    	int tex_y = 22;
    	int tex_w = 200;
    	int tex_h = 20;

		int btex_x = 0;
    	int btex_y = buttons[i].choosen ? 62 : 42; // hell : dunkel
    	int btex_w = 200;
    	int btex_h = 20;

		int x = buttons[i].x + (buttons[i].enable ? toggle_width - toggle_height/2 : 0);

		gfx_bind_texture(&texture_gui2);
		gutil_texquad(buttons[i].x, buttons[i].y, tex_x, tex_y,
					  tex_w, tex_h, toggle_width, toggle_height);
		gutil_texquad(x, buttons[i].y - toggle_height/4, btex_x, btex_y, btex_w, btex_h, 
					  toggle_height/2, toggle_height + toggle_height/2);
		gutil_texquad(buttons[i].x + toggle_width/2 - 2, buttons[i].y + 2, btex_x, 42, btex_w, btex_h, 
					  4, toggle_height - 4);
	  }
	}
}

void gutil_button_new_menu() {
	memset(choosen_pos, -1, sizeof(choosen_pos));
}

void gutil_button_toggle(int x, int y, bool enable, void (*func)(int),
						   int arg, int pos_x, int pos_y) 
{
	buttons[enable_buttons].x = x;
	buttons[enable_buttons].y = y;
	buttons[enable_buttons].width  = -1;
	buttons[enable_buttons].height = -1;
	buttons[enable_buttons].text = NULL;
	buttons[enable_buttons].enable = enable;
	buttons[enable_buttons].type = BUTTON_TOGGLE;	

	bool hover = ptr_available
		&& ptr_x >= x && ptr_x < x + toggle_width
		&& ptr_y >= y && ptr_y < y + toggle_height;
	bool selected = choosen_pos[0] == pos_x && choosen_pos[1] == pos_y;

	buttons[enable_buttons].choosen = dpad_active ? selected : hover;

	if (button_click && buttons[enable_buttons].choosen) {
		func(arg);
	}

	if (pos_x >= 0 && pos_x < x_max && pos_y >= 0 && pos_y < y_max)
		buttons_pos[pos_x][pos_y] = true;

	enable_buttons++;
}