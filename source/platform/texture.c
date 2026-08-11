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

#include <assert.h>
#include "../m-lib/m-string.h"
#include <stdlib.h>

#include "../config.h"
#include "../game/game_state.h"
#include "../graphics/gui_util.h"
#include "../graphics/font_fallback.h"
#include "../lodepng/lodepng.h"
#include "texture.h"
#include "gfx.h"

/* siehe texture.h: true, sobald default.png oder gui_2.png fehlt. */
bool g_missing_resources = false;

	struct tex_gfx texture_fog;
	struct tex_gfx texture_terrain;
	struct tex_gfx texture_terrain2;
	struct tex_gfx texture_particles;
	struct tex_gfx texture_particles_raw;
	struct tex_gfx texture_items;
	struct tex_gfx texture_mobs;
	struct tex_gfx texture_minecart;

struct tex_gfx texture_creeper;
struct tex_gfx texture_pig;
struct tex_gfx texture_sheep;
struct tex_gfx texture_sheep_fur;

struct tex_gfx texture_font;
struct tex_gfx texture_black_font;
struct tex_gfx texture_anim;
struct tex_gfx texture_gui_inventory;
struct tex_gfx texture_gui_crafting;
struct tex_gfx texture_gui_furnace;
struct tex_gfx texture_gui_brewing_stand;
struct tex_gfx texture_gui_chest;
struct tex_gfx texture_gui_iron_chest;
struct tex_gfx texture_gui_enchanting_table;
struct tex_gfx texture_gui2;
struct tex_gfx texture_controls;
struct tex_gfx texture_pointer;
struct tex_gfx texture_clouds;
struct tex_gfx texture_sun;
struct tex_gfx texture_moon;
struct tex_gfx texture_shadow;
struct tex_gfx texture_water;

struct tex_gfx texture_mob_char;

struct tex_gfx texture_armor_chain1;
struct tex_gfx texture_armor_chain2;
struct tex_gfx texture_armor_cloth1;
struct tex_gfx texture_armor_cloth2;
struct tex_gfx texture_armor_gold1;
struct tex_gfx texture_armor_gold2;
struct tex_gfx texture_armor_iron1;
struct tex_gfx texture_armor_iron2;
struct tex_gfx texture_armor_diamond1;
struct tex_gfx texture_armor_diamond2;
struct tex_gfx texture_bg[12];
struct tex_gfx texture_server[12];

//struct tex_gfx texture_button;
//struct tex_gfx texture_buttonlight;
struct tex_gfx texture_bg2;

#define distance_2d(x1, y1, x2, y2)                                            \
	(((x1) - (x2)) * ((x1) - (x2)) + ((y1) - (y2)) * ((y1) - (y2)))

static void gen_texture_fog(uint8_t* img, size_t size) {
	for(size_t y = 0; y < size; y++) {
		for(size_t x = 0; x < size; x++) {
			float d = (sqrt(distance_2d(size / 2.0F, size / 2.0F, x + 0.5F,
										y + 0.5F))
					   - (size / 2.0F - 9.0F))
				/ 8.0F;

			uint8_t* pixel = img + (x + y * size) * 4;
			pixel[0] = pixel[1] = pixel[2]
				= roundf(glm_clamp(d * 255.0F, 0.0F, 255.0F));
			pixel[3] = 255;
		}
	}
}

// #define PARTICLES_PNG_DEBUG

/* Laedt ein "missing texture"-Muster in tex: durchgehend lila, mit einem
   zufaellig verteilten Anteil dunkel-lila Pixel (deterministisch aus x/y
   gehasht, also ohne rand() und bei jedem Start gleich). Ersetzt fehlende PNGs,
   damit nichts auf einen leeren tex_gfx zugreift und der Fehler sichtbar ist.
   Groesse 16 ist durch 8/4 teilbar -> gueltig fuer alle Wii-Texturformate. */
static void tex_gfx_load_missing(struct tex_gfx* tex, enum tex_format type,
								 bool linear) {
	const int size = 16;
	uint8_t* img = malloc(size * size * 4);
	if(!img)
		return;

	for(int y = 0; y < size; y++) {
		for(int x = 0; x < size; x++) {
			/* einfacher per-Pixel-Hash -> pseudozufaelliges Rauschen */
			uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			bool dark = ((h >> 15) % 5) < 2; // ~40% dunkle Pixel

			uint8_t* px = img + (x + y * size) * 4;
			if(dark) {
				px[0] = 90;  // dunkel-lila
				px[1] = 0;
				px[2] = 110;
			} else {
				px[0] = 170; // lila
				px[1] = 0;
				px[2] = 210;
			}
			px[3] = 0xFF;
		}
	}

	/* Jede fehlende Textur -> Nachladen ueber das Server-Menue anstossen. */
	g_missing_resources = true;

	tex_gfx_load(tex, img, size, size, type, linear);
}

/* Baut aus der eingebetteten 8x8-Bitmap-Schrift (font_fallback.h) einen
   128x128-Font-Atlas (16x16 Zellen a 8px) und laedt ihn als I8 in tex --
   identisches Layout wie default.png, sodass gutil_text() unveraendert
   funktioniert. Fallback, wenn default.png fehlt. Sehr RAM-schonend. */
static void build_fallback_font(struct tex_gfx* tex) {
	const int cell = 8;
	const int atlas = 16 * cell; // 128x128
	uint8_t* img = malloc(atlas * atlas * 4);
	if(!img)
		return;

	memset(img, 0, atlas * atlas * 4); // transparenter/schwarzer Hintergrund

	for(int c = FONT_FALLBACK_FIRST;
		c < FONT_FALLBACK_FIRST + FONT_FALLBACK_COUNT; c++) {
		const uint8_t* glyph = font_fallback_8x8[c - FONT_FALLBACK_FIRST];
		int cx = (c % 16) * cell;
		int cy = (c / 16) * cell;

		for(int row = 0; row < 8; row++) {
			uint8_t bits = glyph[row];
			for(int colb = 0; colb < 8; colb++) {
				if(bits & (1 << colb)) {
					uint8_t* px = img + ((cx + colb) + (cy + row) * atlas) * 4;
					px[0] = px[1] = px[2] = px[3] = 0xFF; // weisses Pixel
				}
			}
		}
	}

	tex_gfx_load(tex, img, atlas, atlas, TEX_FMT_I8, false);
}

void tex_init() {
	tex_init_pre();

	size_t w, h;
	void* output  = tex_atlas_block( "terrain.png", &w, &h);
	//void* output2 = tex_atlas_block2("terrain.png", &w, &h);
	if(output /*&& output2*/){
		tex_gfx_load(&texture_terrain, output, w, h, TEX_FMT_RGBA16, false);
		gfx_set_block_atlas_size(w);
		//tex_gfx_load(&texture_terrain2, output2, w, h, TEX_FMT_RGBA16, false);
	} else {
		// terrain.png fehlt -> Karomuster, damit die Block-Textur nicht leer ist
		tex_gfx_load_missing(&texture_terrain, TEX_FMT_RGBA16, false);
	}


	// Font (default.png) ist eine WICHTIGE Textur. Fehlt sie, wird die
	// eingebettete Fallback-Schrift genutzt und das Nachladen angestossen.
	{
		size_t fw, fh;
		void* fimg = tex_read("default.png", &fw, &fh);
		if(fimg) {
			tex_gfx_load(&texture_font, fimg, fw, fh, TEX_FMT_I8, false);
		} else {
			build_fallback_font(&texture_font);
			g_missing_resources = true;
		}
	}
	gutil_reset_font(&texture_font);

	tex_gfx_load_file(&texture_anim, "anim.png", TEX_FMT_RGBA32, false);

    size_t pw, ph;
    void* pout = tex_atlas_particles("particles.png", &pw, &ph);
#ifdef PARTICLES_PNG_DEBUG
	printf("\n?\n\n");
#endif
	    if(pout) {
	#ifdef PARTICLES_PNG_DEBUG
			printf("pout is not NULL \n");
	#endif
	        tex_gfx_load(&texture_particles, pout, pw, ph, TEX_FMT_RGBA16, false);
	    } else {
	        // particles.png fehlt -> lila Muster statt uninitialisierter Textur
	        tex_gfx_load_missing(&texture_particles, TEX_FMT_RGBA16, false);
	    }
		// Keep a raw copy as well for code that needs pixel-precise UVs.
		tex_gfx_load_file(&texture_particles_raw, "particles.png", TEX_FMT_RGBA16, false);


	tex_gfx_load_file(&texture_gui_inventory, "gui/inventory.png",
					  TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_gui_crafting, "gui/crafting.png", TEX_FMT_RGBA16,
					  false);
	tex_gfx_load_file(&texture_gui_furnace, "gui/furnace.png", TEX_FMT_RGBA16,
					  false);
	tex_gfx_load_file(&texture_gui_brewing_stand, "gui/brewing_stand.png",
					  TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_gui_chest, "gui/chest.png", TEX_FMT_RGBA16,
					  false);
	tex_gfx_load_file(&texture_gui_iron_chest, "gui/iron_chest.png", TEX_FMT_RGBA16,
					  false);
	tex_gfx_load_file(&texture_gui_enchanting_table, "gui/enchanting_table.png",
					  TEX_FMT_RGBA16, false);
	// Fehlt gui_2.png (oder eine andere Textur), setzt tex_gfx_load_missing()
	// bereits g_missing_resources -> Nachladen ueber das Server-Menue.
	tex_gfx_load_file(&texture_gui2, "gui_2.png", TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_items, "items.png", TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_mobs, "mobs.png", TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_minecart, "entity/minecart.png", TEX_FMT_RGBA16, false);

	tex_gfx_load_file(&texture_creeper,   "entity/creeper.png",   TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_pig,       "entity/pig.png",       TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_sheep,     "entity/sheep.png",     TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_sheep_fur, "entity/sheep_fur.png", TEX_FMT_RGBA16, false);

	tex_gfx_load_file(&texture_controls, "controls.png", TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_pointer, "pointer.png", TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_clouds, "environment/clouds.png", TEX_FMT_IA4,
					  false);
	tex_gfx_load_file(&texture_sun, "terrain/sun.png", TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_moon, "terrain/moon.png", TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_shadow, "misc/shadow.png", TEX_FMT_IA4, false);
	tex_gfx_load_file(&texture_water, "misc/water.png", TEX_FMT_RGBA16, false);
	tex_gfx_wrap_mode(&texture_water, true);

	tex_gfx_load_file(&texture_armor_chain1, "armor/chain_1.png",
					  TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_armor_chain2, "armor/chain_2.png",
					  TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_armor_cloth1, "armor/cloth_1.png",
					  TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_armor_cloth2, "armor/cloth_2.png",
					  TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_armor_gold1, "armor/gold_1.png", TEX_FMT_RGBA16,
					  false);
	tex_gfx_load_file(&texture_armor_gold2, "armor/gold_2.png", TEX_FMT_RGBA16,
					  false);
	tex_gfx_load_file(&texture_armor_iron1, "armor/iron_1.png", TEX_FMT_RGBA16,
					  false);
	tex_gfx_load_file(&texture_armor_iron2, "armor/iron_2.png", TEX_FMT_RGBA16,
					  false);
	tex_gfx_load_file(&texture_armor_diamond1, "armor/diamond_1.png",
					  TEX_FMT_RGBA16, false);
	tex_gfx_load_file(&texture_armor_diamond2, "armor/diamond_2.png",
					  TEX_FMT_RGBA16, false);

	tex_gfx_load_file(&texture_mob_char, "mob/char.png", TEX_FMT_RGBA16, false);
	
	tex_gfx_load_file(&texture_bg[0],  "bg/bg1.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[1],  "bg/bg2.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[2],  "bg/bg3.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[3],  "bg/bg4.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[4],  "bg/bg5.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[5],  "bg/bg6.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[6],  "bg/bg7.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[7],  "bg/bg8.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[8],  "bg/bg9.png",  TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[9],  "bg/bg10.png", TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[10], "bg/bg11.png", TEX_FMT_RGB16, false);
	tex_gfx_load_file(&texture_bg[11], "bg/bg12.png", TEX_FMT_RGB16, false);



	size_t fog_size = 128;
	uint8_t* fog = malloc(fog_size * fog_size * 4);
	gen_texture_fog(fog, fog_size);
	tex_gfx_load(&texture_fog, fog, fog_size, fog_size, TEX_FMT_I8, true);
}

uint8_t* tex_read(const char* filename, size_t* width, size_t* height) {
	assert(filename && width && height);

	string_t tmp;
	string_init_printf(
		tmp, "%s/%s",
		config_read_string(&gstate.config_user, "paths.texturepack", "assets"),
		filename);

	uint8_t* img;
	unsigned w, h;
	if(lodepng_decode32_file(&img, &w, &h, string_get_cstr(tmp))) {
		string_clear(tmp);
		*width = 0; /* definierte Dimensionen bei Fehlschlag (Atlas prueft sie) */
		*height = 0;
		return NULL;
	}

	string_clear(tmp);

	*width = w;
	*height = h;

	return img;
}

bool tex_gfx_load_file(struct tex_gfx* tex, const char* filename,
					   enum tex_format type, bool linear) {
	assert(filename);

	size_t width, height;
	void* img = tex_read(filename, &width, &height);

	if(!img) {
		// Datei fehlt -> magenta/schwarzes Karomuster als Ersatz laden.
		tex_gfx_load_missing(tex, type, linear);
		return false;
	}

	tex_gfx_load(tex, img, width, height, type, linear);
	return true;
}

#ifdef PLATFORM_WII
#include "wii/texture.c"
#endif

#ifdef PLATFORM_PC
#include "pc/texture.c"
#endif
