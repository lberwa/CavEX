/*
	Copyright (c) 2022 ByteBit/xtreme8000

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
#include <stdint.h>
#include <stdio.h>

#include "../block/blocks.h"
#include "../chunk.h"
#include "../game/game_state.h"
#include "../platform/gfx.h"
#include "../platform/time.h"
#include "../util.h"
#include "render_block.h"

#define BLK_LEN 256

static inline uint8_t MIN_U8(uint8_t a, uint8_t b) {
	return a < b ? a : b;
}

static inline uint8_t MAX_U8(uint8_t a, uint8_t b) {
	return a > b ? a : b;
}

static uint8_t level_table_0[16] = {
	0, 0, 0, 0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12,
};

static uint8_t level_table_1[16] = {
	0, 0, 0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
};

static uint8_t level_table_2[16] = {
	0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
};

static inline uint8_t DIM_LIGHT(uint8_t l, uint8_t* table, bool shade_sides,
								uint8_t luminance) {
	return shade_sides ?
		(table[MAX_U8(l >> 4, luminance)] << 4) | table[l & 0x0F] :
		(MAX_U8(l >> 4, luminance) << 4) | (l & 0x0F);
}

static inline void render_block_side_adv_v2(
	struct displaylist* d, int16_t x, int16_t y, int16_t z, uint16_t width,
	uint16_t height, int16_t inset_bottom, int16_t inset_top, uint8_t tex_x,
	uint8_t tex_y, bool tex_flip_h, int tex_rotate, bool shade_sides,
	enum side side, const uint8_t* vertex_light, uint8_t luminance) {
	const uint8_t tex_coords[2][4][2] = {
		{
			{tex_x, tex_y},
			{tex_x + MIN_U8(16, width / 16), tex_y},
			{tex_x + MIN_U8(16, width / 16), tex_y + MIN_U8(16, height / 16)},
			{tex_x, tex_y + MIN_U8(16, height / 16)},
		},
		{
			{tex_x + MIN_U8(16, width / 16), tex_y},
			{tex_x, tex_y},
			{tex_x, tex_y + MIN_U8(16, height / 16)},
			{tex_x + MIN_U8(16, width / 16), tex_y + MIN_U8(16, height / 16)},
		},
	};

	switch(side) {
		case SIDE_LEFT: { // x minus
			displaylist_pos(d, x + inset_bottom, y, z);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[8], level_table_1,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 3) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 3) % 4][1]);
			displaylist_pos(d, x + inset_top, y + height, z);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[9], level_table_1,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 0) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 0) % 4][1]);
			displaylist_pos(d, x + inset_top, y + height, z + width);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[10], level_table_1,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 1) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 1) % 4][1]);
			displaylist_pos(d, x + inset_bottom, y, z + width);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[11], level_table_1,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 2) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 2) % 4][1]);
		} break;
		case SIDE_RIGHT: { // x positive
			displaylist_pos(d, x - inset_bottom, y, z);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[12], level_table_1,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 2) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 2) % 4][1]);
			displaylist_pos(d, x - inset_bottom, y, z + width);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[15], level_table_1,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 3) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 3) % 4][1]);
			displaylist_pos(d, x - inset_top, y + height, z + width);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[14], level_table_1,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 0) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 0) % 4][1]);
			displaylist_pos(d, x - inset_top, y + height, z);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[13], level_table_1,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 1) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 1) % 4][1]);
		} break;
		case SIDE_TOP: { // y positive
			displaylist_pos(d, x, y, z);
			displaylist_color(
				d, DIM_LIGHT(vertex_light[4], NULL, false, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 0) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 0) % 4][1]);
			displaylist_pos(d, x + width, y, z);
			displaylist_color(
				d, DIM_LIGHT(vertex_light[5], NULL, false, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 1) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 1) % 4][1]);
			displaylist_pos(d, x + width, y, z + height);
			displaylist_color(
				d, DIM_LIGHT(vertex_light[6], NULL, false, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 2) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 2) % 4][1]);
			displaylist_pos(d, x, y, z + height);
			displaylist_color(
				d, DIM_LIGHT(vertex_light[7], NULL, false, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 3) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 3) % 4][1]);
		} break;
		case SIDE_BOTTOM: { // y negative
			displaylist_pos(d, x, y + inset_bottom, z);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[0], level_table_0,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 3) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 3) % 4][1]);
			displaylist_pos(d, x, y + inset_top, z + height);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[3], level_table_0,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 0) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 0) % 4][1]);
			displaylist_pos(d, x + width, y + inset_top, z + height);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[2], level_table_0,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 1) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 1) % 4][1]);
			displaylist_pos(d, x + width, y + inset_bottom, z);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[1], level_table_0,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 2) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 2) % 4][1]);
		} break;
		case SIDE_FRONT: { // z minus
			displaylist_pos(d, x, y, z + inset_bottom);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[16], level_table_2,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 2) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 2) % 4][1]);
			displaylist_pos(d, x + width, y, z + inset_bottom);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[17], level_table_2,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 3) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 3) % 4][1]);
			displaylist_pos(d, x + width, y + height, z + inset_top);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[18], level_table_2,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 0) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 0) % 4][1]);
			displaylist_pos(d, x, y + height, z + inset_top);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[19], level_table_2,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 1) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 1) % 4][1]);
		} break;
		case SIDE_BACK: { // z positive
			displaylist_pos(d, x, y, z - inset_bottom);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[20], level_table_2,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 3) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 3) % 4][1]);
			displaylist_pos(d, x, y + height, z - inset_top);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[23], level_table_2,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 0) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 0) % 4][1]);
			displaylist_pos(d, x + width, y + height, z - inset_top);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[22], level_table_2,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 1) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 1) % 4][1]);
			displaylist_pos(d, x + width, y, z - inset_bottom);
			displaylist_color(d,
							  DIM_LIGHT(vertex_light[21], level_table_2,
										shade_sides, luminance));
			displaylist_texcoord(
				d, tex_coords[tex_flip_h][(tex_rotate + 2) % 4][0],
				tex_coords[tex_flip_h][(tex_rotate + 2) % 4][1]);
		} break;
		default: break;
	}
}

static inline void
render_block_side_adv(struct displaylist* d, int16_t x, int16_t y, int16_t z,
					  uint16_t width, uint16_t height, uint8_t tex_x,
					  uint8_t tex_y, bool tex_flip_h, int tex_rotate,
					  bool shade_sides, enum side side,
					  const uint8_t* vertex_light, uint8_t luminance) {
	render_block_side_adv_v2(d, x, y, z, width, height, 0, 0, tex_x, tex_y,
							 tex_flip_h, tex_rotate, shade_sides, side,
							 vertex_light, luminance);
}

static inline void render_block_side(struct displaylist* d, int16_t x,
									 int16_t y, int16_t z, int16_t yoffset,
									 uint16_t height, uint8_t tex,
									 uint8_t luminance, bool shade_sides,
									 uint16_t inset, bool tex_flip_h,
									 int tex_rotate, enum side side,
									 const uint8_t* vertex_light) {
	uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
	uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));

	switch(side) {
		case SIDE_LEFT: // x minus
			render_block_side_adv(
				d, x * BLK_LEN + inset, y * BLK_LEN + yoffset, z * BLK_LEN,
				BLK_LEN, height, tex_x, tex_y + (16 - height / 16), tex_flip_h,
				tex_rotate, shade_sides, SIDE_LEFT, vertex_light, luminance);
			break;
		case SIDE_RIGHT: // x positive
			render_block_side_adv(d, x * BLK_LEN + BLK_LEN - inset,
								  y * BLK_LEN + yoffset, z * BLK_LEN, BLK_LEN,
								  height, tex_x, tex_y + (16 - height / 16),
								  tex_flip_h, tex_rotate, shade_sides,
								  SIDE_RIGHT, vertex_light, luminance);
			break;
		case SIDE_BOTTOM: // y minus
			render_block_side_adv(d, x * BLK_LEN, y * BLK_LEN + yoffset + inset,
								  z * BLK_LEN, BLK_LEN, BLK_LEN, tex_x, tex_y,
								  tex_flip_h, tex_rotate, shade_sides,
								  SIDE_BOTTOM, vertex_light, luminance);
			break;
		case SIDE_TOP: // y positive
			render_block_side_adv(
				d, x * BLK_LEN, y * BLK_LEN + yoffset + height - inset,
				z * BLK_LEN, BLK_LEN, BLK_LEN, tex_x, tex_y, tex_flip_h,
				tex_rotate, shade_sides, SIDE_TOP, vertex_light, luminance);
			break;
		case SIDE_FRONT: // z minus
			render_block_side_adv(
				d, x * BLK_LEN, y * BLK_LEN + yoffset, z * BLK_LEN + inset,
				BLK_LEN, height, tex_x, tex_y + (16 - height / 16), tex_flip_h,
				tex_rotate, shade_sides, SIDE_FRONT, vertex_light, luminance);
			break;
		case SIDE_BACK: // z positive
			render_block_side_adv(d, x * BLK_LEN, y * BLK_LEN + yoffset,
								  z * BLK_LEN + BLK_LEN - inset, BLK_LEN,
								  height, tex_x, tex_y + (16 - height / 16),
								  tex_flip_h, tex_rotate, shade_sides,
								  SIDE_BACK, vertex_light, luminance);
			break;
		default: break;
	}
}

static size_t render_cuboid_side(struct displaylist* d, struct block_info* this,
								 enum side side, uint8_t* vertex_light,
								 bool count_only, int16_t x0, int16_t y0,
								 int16_t z0, int16_t x1, int16_t y1,
								 int16_t z1, uint8_t tex, int tex_rotate) {
	if(x0 == x1 || y0 == y1 || z0 == z1)
		return 0;

	if(!count_only) {
		uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
		uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
		int16_t bx = W2C_COORD(this->x) * BLK_LEN;
		int16_t by = W2C_COORD(this->y) * BLK_LEN;
		int16_t bz = W2C_COORD(this->z) * BLK_LEN;
		uint16_t width;
		uint16_t height;

		switch(side) {
			case SIDE_LEFT:
				width = z1 - z0;
				height = y1 - y0;
				render_block_side_adv(d, bx + x0, by + y0, bz + z0, width, height,
									  tex_x, tex_y + (16 - height / 16), false,
									  tex_rotate,
									  true, side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_RIGHT:
				width = z1 - z0;
				height = y1 - y0;
				render_block_side_adv(d, bx + x1, by + y0, bz + z0, width, height,
									  tex_x, tex_y + (16 - height / 16), false,
									  tex_rotate,
									  true, side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_BOTTOM:
				width = x1 - x0;
				height = z1 - z0;
				render_block_side_adv(d, bx + x0, by + y0, bz + z0, width, height,
									  tex_x, tex_y, false, tex_rotate, true, side,
									  vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_TOP:
				width = x1 - x0;
				height = z1 - z0;
				render_block_side_adv(d, bx + x0, by + y1, bz + z0, width, height,
									  tex_x, tex_y, false, tex_rotate, true, side,
									  vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_FRONT:
				width = x1 - x0;
				height = y1 - y0;
				render_block_side_adv(d, bx + x0, by + y0, bz + z0, width, height,
									  tex_x, tex_y + (16 - height / 16), false,
									  tex_rotate,
									  true, side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_BACK:
				width = x1 - x0;
				height = y1 - y0;
				render_block_side_adv(d, bx + x0, by + y0, bz + z1, width, height,
									  tex_x, tex_y + (16 - height / 16), false,
									  tex_rotate,
									  true, side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			default: break;
		}
	}

	return 1;
}

static size_t render_cuboid_side_tex(struct displaylist* d,
									 struct block_info* this, enum side side,
									 uint8_t* vertex_light, bool count_only,
									 int16_t x0, int16_t y0, int16_t z0,
									 int16_t x1, int16_t y1, int16_t z1,
									 uint8_t tex, int tex_rotate,
									 uint8_t tex_off_x, uint8_t tex_off_y) {
	if(x0 == x1 || y0 == y1 || z0 == z1)
		return 0;

	if(!count_only) {
		uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex)) + tex_off_x;
		uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex)) + tex_off_y;
		int16_t bx = W2C_COORD(this->x) * BLK_LEN;
		int16_t by = W2C_COORD(this->y) * BLK_LEN;
		int16_t bz = W2C_COORD(this->z) * BLK_LEN;
		uint16_t width;
		uint16_t height;

		switch(side) {
			case SIDE_LEFT:
				width = z1 - z0;
				height = y1 - y0;
				render_block_side_adv(d, bx + x0, by + y0, bz + z0, width, height,
									  tex_x, tex_y + (16 - height / 16), false,
									  tex_rotate, true, side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_RIGHT:
				width = z1 - z0;
				height = y1 - y0;
				render_block_side_adv(d, bx + x1, by + y0, bz + z0, width, height,
									  tex_x, tex_y + (16 - height / 16), false,
									  tex_rotate, true, side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_BOTTOM:
				width = x1 - x0;
				height = z1 - z0;
				render_block_side_adv(d, bx + x0, by + y0, bz + z0, width, height,
									  tex_x, tex_y, false, tex_rotate, true,
									  side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_TOP:
				width = x1 - x0;
				height = z1 - z0;
				render_block_side_adv(d, bx + x0, by + y1, bz + z0, width, height,
									  tex_x, tex_y, false, tex_rotate, true,
									  side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_FRONT:
				width = x1 - x0;
				height = y1 - y0;
				render_block_side_adv(d, bx + x0, by + y0, bz + z0, width, height,
									  tex_x, tex_y + (16 - height / 16), false,
									  tex_rotate, true, side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			case SIDE_BACK:
				width = x1 - x0;
				height = y1 - y0;
				render_block_side_adv(d, bx + x0, by + y0, bz + z1, width, height,
									  tex_x, tex_y + (16 - height / 16), false,
									  tex_rotate, true, side, vertex_light,
									  blocks[this->block->type]->luminance);
				break;
			default: break;
		}
	}

	return 1;
}

static size_t render_cuboid_side_texrect(struct displaylist* d,
										 struct block_info* this, enum side side,
										 uint8_t* vertex_light, bool count_only,
										 int16_t x0, int16_t y0, int16_t z0,
										 int16_t x1, int16_t y1, int16_t z1,
										 uint8_t tex, int tex_rotate,
										 uint8_t tex_off_x, uint8_t tex_off_y,
										 uint8_t tex_w, uint8_t tex_h) {
	if(x0 == x1 || y0 == y1 || z0 == z1)
		return 0;

	if(!count_only) {
		uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex)) + tex_off_x;
		uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex)) + tex_off_y;
		int16_t bx = W2C_COORD(this->x) * BLK_LEN;
		int16_t by = W2C_COORD(this->y) * BLK_LEN;
		int16_t bz = W2C_COORD(this->z) * BLK_LEN;
		const uint8_t tex_coords[2][4][2] = {
			{
				{tex_x, tex_y},
				{tex_x + tex_w, tex_y},
				{tex_x + tex_w, tex_y + tex_h},
				{tex_x, tex_y + tex_h},
			},
			{
				{tex_x + tex_w, tex_y},
				{tex_x, tex_y},
				{tex_x, tex_y + tex_h},
				{tex_x + tex_w, tex_y + tex_h},
			},
		};

#define PISTON_TEXCOORD(v)                                                     \
	displaylist_texcoord(d,                                                    \
					 tex_coords[0][(tex_rotate + (v)) % 4][0],                \
					 tex_coords[0][(tex_rotate + (v)) % 4][1])

		switch(side) {
			case SIDE_LEFT:
				displaylist_pos(d, bx + x0, by + y0, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[8], level_table_1, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(3);
				displaylist_pos(d, bx + x0, by + y1, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[9], level_table_1, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(0);
				displaylist_pos(d, bx + x0, by + y1, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[10], level_table_1, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(1);
				displaylist_pos(d, bx + x0, by + y0, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[11], level_table_1, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(2);
				break;
			case SIDE_RIGHT:
				displaylist_pos(d, bx + x1, by + y0, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[12], level_table_1, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(2);
				displaylist_pos(d, bx + x1, by + y0, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[13], level_table_1, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(3);
				displaylist_pos(d, bx + x1, by + y1, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[14], level_table_1, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(0);
				displaylist_pos(d, bx + x1, by + y1, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[15], level_table_1, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(1);
				break;
			case SIDE_BOTTOM:
				displaylist_pos(d, bx + x0, by + y0, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[4], level_table_0, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(3);
				displaylist_pos(d, bx + x0, by + y0, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[5], level_table_0, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(0);
				displaylist_pos(d, bx + x1, by + y0, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[6], level_table_0, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(1);
				displaylist_pos(d, bx + x1, by + y0, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[7], level_table_0, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(2);
				break;
			case SIDE_TOP:
				displaylist_pos(d, bx + x0, by + y1, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[0], level_table_0, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(0);
				displaylist_pos(d, bx + x1, by + y1, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[1], level_table_0, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(1);
				displaylist_pos(d, bx + x1, by + y1, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[2], level_table_0, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(2);
				displaylist_pos(d, bx + x0, by + y1, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[3], level_table_0, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(3);
				break;
			case SIDE_FRONT:
				displaylist_pos(d, bx + x0, by + y0, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[20], level_table_2, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(3);
				displaylist_pos(d, bx + x1, by + y0, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[21], level_table_2, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(2);
				displaylist_pos(d, bx + x1, by + y1, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[22], level_table_2, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(1);
				displaylist_pos(d, bx + x0, by + y1, bz + z0);
				displaylist_color(d, DIM_LIGHT(vertex_light[23], level_table_2, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(0);
				break;
			case SIDE_BACK:
				displaylist_pos(d, bx + x0, by + y0, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[16], level_table_2, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(0);
				displaylist_pos(d, bx + x0, by + y1, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[17], level_table_2, true,
											  blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(1);
				displaylist_pos(d, bx + x1, by + y1, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[18], level_table_2, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(2);
				displaylist_pos(d, bx + x1, by + y0, bz + z1);
				displaylist_color(d, DIM_LIGHT(vertex_light[19], level_table_2, true,
											   blocks[this->block->type]->luminance));
				PISTON_TEXCOORD(3);
				break;
			default: break;
		}

#undef PISTON_TEXCOORD
	}

	return 1;
}

static int piston_texture_rotate(enum side facing, enum side side) {
	switch(facing) {
		case SIDE_FRONT:
			if(side == SIDE_FRONT) return 1;
			if(side == SIDE_TOP)   return 0;
			if(side == SIDE_BOTTOM)return 2;
			if(side == SIDE_BACK)  return 1;
			if(side == SIDE_LEFT)  return 1;
			if(side == SIDE_RIGHT) return 3;
			return 0;
		case SIDE_BACK:
			if(side == SIDE_FRONT) return 1;
			if(side == SIDE_TOP)   return 2;
			if(side == SIDE_BOTTOM)return 0;
			if(side == SIDE_BACK)  return 1;
			if(side == SIDE_LEFT)  return 3;
			if(side == SIDE_RIGHT) return 1;
			return 0;
		case SIDE_TOP:
			if(side == SIDE_FRONT) return 0;
			if(side == SIDE_TOP)   return 0;
			if(side == SIDE_BOTTOM)return 0;
			if(side == SIDE_BACK)  return 0;
			if(side == SIDE_LEFT)  return 0;
			if(side == SIDE_RIGHT) return 0;
			return 0;
		case SIDE_BOTTOM:
			if(side == SIDE_FRONT) return 2;
			if(side == SIDE_TOP)   return 1;
			if(side == SIDE_BOTTOM)return 0;
			if(side == SIDE_BACK)  return 2;
			if(side == SIDE_LEFT)  return 2;
			if(side == SIDE_RIGHT) return 2;
			return 0;
		case SIDE_RIGHT:
			if(side == SIDE_FRONT) return 1;
			if(side == SIDE_TOP)   return 3;
			if(side == SIDE_BOTTOM)return 3;
			if(side == SIDE_BACK)  return 3;
			if(side == SIDE_LEFT)  return 3;
			if(side == SIDE_RIGHT) return 3;
			return 0;
		case SIDE_LEFT:
			if(side == SIDE_FRONT) return 3;
			if(side == SIDE_TOP)   return 1;
			if(side == SIDE_BOTTOM)return 1;
			if(side == SIDE_BACK)  return 1;
			if(side == SIDE_LEFT)  return 3;
			if(side == SIDE_RIGHT) return 3;
			return 0;
		default: return 0;
	}
}

static int piston_head_side_rotate(enum side facing, enum side side) {
	int rot = piston_texture_rotate(facing, side);

	if((facing == SIDE_LEFT || facing == SIDE_RIGHT) && side == SIDE_BACK)
		rot = (rot + 1) % 4;

	return rot;
}

size_t render_block_cross(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	if(side != SIDE_TOP)
		return 0;

	if(!count_only) {
		int16_t x = W2C_COORD(this->x) * BLK_LEN;
		int16_t y = W2C_COORD(this->y) * BLK_LEN;
		int16_t z = W2C_COORD(this->z) * BLK_LEN;

		if(blocks[this->block->type]
			   ->render_block_data.cross_random_displacement) {
			uint32_t seed
				= hash_u32(this->x) ^ hash_u32(this->y) ^ hash_u32(this->z);
			x += (seed & 0xFFFF) % 129 - 64;
			z += (seed >> 16) % 129 - 64;
		}

		uint8_t tex
			= blocks[this->block->type]->getTextureIndex(this, SIDE_TOP);
		uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
		uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
		uint8_t light = (MAX_U8(this->block->torch_light,
								blocks[this->block->type]->luminance)
						 << 4)
			| this->block->sky_light;

		displaylist_pos(d, x, y, z);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x, tex_y + 16);
		displaylist_pos(d, x, y + BLK_LEN, z);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x, tex_y);
		displaylist_pos(d, x + BLK_LEN, y + BLK_LEN, z + BLK_LEN);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x + 16, tex_y);
		displaylist_pos(d, x + BLK_LEN, y, z + BLK_LEN);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x + 16, tex_y + 16);

		displaylist_pos(d, x + BLK_LEN, y, z);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x, tex_y + 16);
		displaylist_pos(d, x + BLK_LEN, y + BLK_LEN, z);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x, tex_y);
		displaylist_pos(d, x, y + BLK_LEN, z + BLK_LEN);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x + 16, tex_y);
		displaylist_pos(d, x, y, z + BLK_LEN);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x + 16, tex_y + 16);
	}

	return 2;
}

size_t render_block_tree2d(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	if(side != SIDE_TOP)
		return 0;

	if(!count_only) {
		int16_t x = W2C_COORD(this->x) * BLK_LEN;
		int16_t y = W2C_COORD(this->y) * BLK_LEN;
		int16_t z = W2C_COORD(this->z) * BLK_LEN;

		if(blocks[this->block->type]
			   ->render_block_data.cross_random_displacement) {
			uint32_t seed
				= hash_u32(this->x) ^ hash_u32(this->y) ^ hash_u32(this->z);
			x += (seed & 0xFFFF) % 129 - 64;
			z += (seed >> 16) % 129 - 64;
		}

		uint8_t tex
			= blocks[this->block->type]->getTextureIndex(this, SIDE_TOP);
		uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
		uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
		uint8_t light = (MAX_U8(this->block->torch_light,
								blocks[this->block->type]->luminance)
						 << 4)
			| this->block->sky_light;

		int16_t tree_height = BLK_LEN*(6+2*(this->block->metadata & 1));

		displaylist_pos(d, x, y, z);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x, tex_y + 16);
		displaylist_pos(d, x, y + tree_height, z);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x, tex_y);
		displaylist_pos(d, x + BLK_LEN, y + BLK_LEN, z + BLK_LEN);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x + 16, tex_y);
		displaylist_pos(d, x + BLK_LEN, y, z + BLK_LEN);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x + 16, tex_y + 16);

		displaylist_pos(d, x + BLK_LEN, y, z);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x, tex_y + 16);
		displaylist_pos(d, x + BLK_LEN, y + tree_height, z);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x, tex_y);
		displaylist_pos(d, x, y + tree_height, z + BLK_LEN);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x + 16, tex_y);
		displaylist_pos(d, x, y, z + BLK_LEN);
		displaylist_color(d, light);
		displaylist_texcoord(d, tex_x + 16, tex_y + 16);
	}

	return 2;
}

struct lever_pt {
	float x;
	float y;
	float z;
};

static struct lever_pt lever_rotate_pt(struct lever_pt p, float pivot_x,
									   float pivot_y, float pivot_z, float rot_x,
									   float rot_y, float rot_z) {
	if(rot_x != 0.0f) {
		float ny = p.y * cosf(rot_x) - p.z * sinf(rot_x);
		float nz = p.y * sinf(rot_x) + p.z * cosf(rot_x);
		p.y = ny;
		p.z = nz;
	}
	if(rot_y != 0.0f) {
		float nx = p.x * cosf(rot_y) + p.z * sinf(rot_y);
		float nz = -p.x * sinf(rot_y) + p.z * cosf(rot_y);
		p.x = nx;
		p.z = nz;
	}
	if(rot_z != 0.0f) {
		float nx = p.x * cosf(rot_z) - p.y * sinf(rot_z);
		float ny = p.x * sinf(rot_z) + p.y * cosf(rot_z);
		p.x = nx;
		p.y = ny;
	}
	p.x += pivot_x;
	p.y += pivot_y;
	p.z += pivot_z;
	return p;
}

static void lever_emit_pt(struct displaylist* d, int16_t x, int16_t y, int16_t z,
						  struct lever_pt p, uint8_t vlight, uint8_t u,
						  uint8_t v) {
	displaylist_pos(d, x + (int16_t)lroundf(p.x), y + (int16_t)lroundf(p.y),
					z + (int16_t)lroundf(p.z));
	displaylist_color(d, vlight);
	displaylist_texcoord(d, u, v);
}

size_t render_block_lever(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	static const uint8_t side_uvs[4][2] = {
		{7, 16},
		{7, 6},
		{9, 6},
		{9, 16},
	};
	static const uint8_t top_uvs[4][2] = {
		{7, 6},
		{9, 6},
		{9, 8},
		{7, 8},
	};

	uint8_t meta = this->block->metadata & 0x07;
	bool on = (this->block->metadata & 0x08) != 0;
	uint8_t base_tex = tex_atlas_lookup(TEXAT_COBBLESTONE);
	int16_t bx0 = 5 * 16, by0 = 0, bz0 = 5 * 16;
	int16_t bx1 = 11 * 16, by1 = 2 * 16, bz1 = 11 * 16;
	uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
	uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
	uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
	uint8_t light = (MAX_U8(this->block->torch_light,
							blocks[this->block->type]->luminance)
					 << 4)
		| this->block->sky_light;
	int16_t x = W2C_COORD(this->x) * BLK_LEN;
	int16_t y = W2C_COORD(this->y) * BLK_LEN;
	int16_t z = W2C_COORD(this->z) * BLK_LEN;
	float pivot_x = 128.0f;
	float pivot_y = 32.0f;
	float pivot_z = 128.0f;
	float rot_x = 0.0f;
	float rot_y = 0.0f;
	float rot_z = 0.0f;
	bool  wall_lever = (meta >= 1 && meta <= 4);
	float angle = (on ? -0.70f : 0.70f)
				  - (wall_lever ? (float)GLM_PI_2 : 0.0f);
	float rod_half = 16.0f;
	float rod_len = 144.0f;

	switch(meta) {
		case 1:
			bx0 = 0; by0 = 5 * 16; bz0 = 5 * 16;
			bx1 = 2 * 16; by1 = 11 * 16; bz1 = 11 * 16;
			pivot_x = 32.0f;
			pivot_y = 128.0f;
			pivot_z = 128.0f;
			rot_x = angle;
			rot_y = -(float)GLM_PI_2;
			break;
		case 2:
			bx0 = 14 * 16; by0 = 5 * 16; bz0 = 5 * 16;
			bx1 = 16 * 16; by1 = 11 * 16; bz1 = 11 * 16;
			pivot_x = 224.0f;
			pivot_y = 128.0f;
			pivot_z = 128.0f;
			rot_x = angle;
			rot_y = (float)GLM_PI_2;
			break;
		case 3:
			bx0 = 5 * 16; by0 = 5 * 16; bz0 = 0;
			bx1 = 11 * 16; by1 = 11 * 16; bz1 = 2 * 16;
			pivot_x = 128.0f;
			pivot_y = 128.0f;
			pivot_z = 32.0f;
			rot_x = angle;
			rot_y = (float)M_PI;
			break;
		case 4:
			bx0 = 5 * 16; by0 = 5 * 16; bz0 = 14 * 16;
			bx1 = 11 * 16; by1 = 11 * 16; bz1 = 16 * 16;
			pivot_x = 128.0f;
			pivot_y = 128.0f;
			pivot_z = 224.0f;
			rot_x = angle;
			rot_y = 0.0f;
			break;
		case 6:
			pivot_x = 128.0f;
			pivot_y = 32.0f;
			pivot_z = 128.0f;
			rot_z = -angle;
			break;
		case 7:
			by0 = 14 * 16; by1 = 16 * 16;
			pivot_x = 128.0f;
			pivot_y = 224.0f;
			pivot_z = 128.0f;
			rot_z = angle;
			rod_len = -144.0f;
			break;
		case 5:
		default:
			pivot_x = 128.0f;
			pivot_y = 32.0f;
			pivot_z = 128.0f;
			rot_x = angle;
			break;
	}

	size_t count = render_cuboid_side(d, this, side, vertex_light, count_only,
									  bx0, by0, bz0, bx1, by1, bz1, base_tex, 0);

	if(side == SIDE_BOTTOM)
		return count;

	if(!count_only) {
		struct lever_pt p0, p1, p2, p3;
		uint8_t vlight = light;

		switch(side) {
			case SIDE_LEFT:
				vlight = DIM_LIGHT(light, level_table_1, true, 0);
				p0 = lever_rotate_pt((struct lever_pt){-rod_half, 0.0f, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p1 = lever_rotate_pt((struct lever_pt){-rod_half, rod_len, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p2 = lever_rotate_pt((struct lever_pt){-rod_half, rod_len, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p3 = lever_rotate_pt((struct lever_pt){-rod_half, 0.0f, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				lever_emit_pt(d, x, y, z, p0, vlight, tex_x + side_uvs[0][0], tex_y + side_uvs[0][1]);
				lever_emit_pt(d, x, y, z, p1, vlight, tex_x + side_uvs[1][0], tex_y + side_uvs[1][1]);
				lever_emit_pt(d, x, y, z, p2, vlight, tex_x + side_uvs[2][0], tex_y + side_uvs[2][1]);
				lever_emit_pt(d, x, y, z, p3, vlight, tex_x + side_uvs[3][0], tex_y + side_uvs[3][1]);
				break;
			case SIDE_RIGHT:
				vlight = DIM_LIGHT(light, level_table_1, true, 0);
				p0 = lever_rotate_pt((struct lever_pt){rod_half, 0.0f, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p1 = lever_rotate_pt((struct lever_pt){rod_half, 0.0f, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p2 = lever_rotate_pt((struct lever_pt){rod_half, rod_len, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p3 = lever_rotate_pt((struct lever_pt){rod_half, rod_len, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				lever_emit_pt(d, x, y, z, p0, vlight, tex_x + side_uvs[3][0], tex_y + side_uvs[3][1]);
				lever_emit_pt(d, x, y, z, p1, vlight, tex_x + side_uvs[0][0], tex_y + side_uvs[0][1]);
				lever_emit_pt(d, x, y, z, p2, vlight, tex_x + side_uvs[1][0], tex_y + side_uvs[1][1]);
				lever_emit_pt(d, x, y, z, p3, vlight, tex_x + side_uvs[2][0], tex_y + side_uvs[2][1]);
				break;
			case SIDE_BACK:
				vlight = DIM_LIGHT(light, level_table_2, true, 0);
				p0 = lever_rotate_pt((struct lever_pt){-rod_half, 0.0f, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p1 = lever_rotate_pt((struct lever_pt){-rod_half, rod_len, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p2 = lever_rotate_pt((struct lever_pt){rod_half, rod_len, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p3 = lever_rotate_pt((struct lever_pt){rod_half, 0.0f, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				lever_emit_pt(d, x, y, z, p0, vlight, tex_x + side_uvs[0][0], tex_y + side_uvs[0][1]);
				lever_emit_pt(d, x, y, z, p1, vlight, tex_x + side_uvs[1][0], tex_y + side_uvs[1][1]);
				lever_emit_pt(d, x, y, z, p2, vlight, tex_x + side_uvs[2][0], tex_y + side_uvs[2][1]);
				lever_emit_pt(d, x, y, z, p3, vlight, tex_x + side_uvs[3][0], tex_y + side_uvs[3][1]);
				break;
			case SIDE_FRONT:
				vlight = DIM_LIGHT(light, level_table_2, true, 0);
				p0 = lever_rotate_pt((struct lever_pt){-rod_half, 0.0f, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p1 = lever_rotate_pt((struct lever_pt){rod_half, 0.0f, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p2 = lever_rotate_pt((struct lever_pt){rod_half, rod_len, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p3 = lever_rotate_pt((struct lever_pt){-rod_half, rod_len, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				lever_emit_pt(d, x, y, z, p0, vlight, tex_x + side_uvs[3][0], tex_y + side_uvs[3][1]);
				lever_emit_pt(d, x, y, z, p1, vlight, tex_x + side_uvs[0][0], tex_y + side_uvs[0][1]);
				lever_emit_pt(d, x, y, z, p2, vlight, tex_x + side_uvs[1][0], tex_y + side_uvs[1][1]);
				lever_emit_pt(d, x, y, z, p3, vlight, tex_x + side_uvs[2][0], tex_y + side_uvs[2][1]);
				break;
			case SIDE_TOP:
				p0 = lever_rotate_pt((struct lever_pt){-rod_half, rod_len, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p1 = lever_rotate_pt((struct lever_pt){rod_half, rod_len, -rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p2 = lever_rotate_pt((struct lever_pt){rod_half, rod_len, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				p3 = lever_rotate_pt((struct lever_pt){-rod_half, rod_len, rod_half},
									pivot_x, pivot_y, pivot_z, rot_x, rot_y, rot_z);
				lever_emit_pt(d, x, y, z, p0, light, tex_x + top_uvs[0][0], tex_y + top_uvs[0][1]);
				lever_emit_pt(d, x, y, z, p1, light, tex_x + top_uvs[1][0], tex_y + top_uvs[1][1]);
				lever_emit_pt(d, x, y, z, p2, light, tex_x + top_uvs[2][0], tex_y + top_uvs[2][1]);
				lever_emit_pt(d, x, y, z, p3, light, tex_x + top_uvs[3][0], tex_y + top_uvs[3][1]);
				break;
			default: break;
		}
	}

	return count + 1;
}

static size_t render_button_box(struct displaylist* d, struct block_info* this,
								enum side side, uint8_t* vertex_light,
								bool count_only, int16_t x0, int16_t y0,
								int16_t z0, int16_t x1, int16_t y1,
								int16_t z1) {
	uint8_t tex = tex_atlas_lookup(TEXAT_STONE_BUTTON);
	return render_cuboid_side(d, this, side, vertex_light, count_only, x0, y0,
							  z0, x1, y1, z1, tex, 0);
}

size_t render_block_button(struct displaylist* d, struct block_info* this,
						   enum side side, struct block_info* it,
						   uint8_t* vertex_light, bool count_only) {
	uint8_t meta = this->block->metadata & 0x07;
	bool pressed = (this->block->metadata & 0x08) != 0;
	int16_t depth = pressed ? 8 : 16;

	switch(meta) {
		case 1:
			return render_button_box(d, this, side, vertex_light, count_only,
									 0, 96, 80, depth, 160, 176);
		case 2:
			return render_button_box(d, this, side, vertex_light, count_only,
									 256 - depth, 96, 80, 256, 160, 176);
		case 3:
			return render_button_box(d, this, side, vertex_light, count_only,
									 80, 96, 0, 176, 160, depth);
		case 4:
			return render_button_box(d, this, side, vertex_light, count_only,
									 80, 96, 256 - depth, 176, 160, 256);
		default:
			return render_button_box(d, this, side, vertex_light, count_only,
									 80, 256 - depth, 80, 176, 256, 176);
	}
}

static void repeater_orient_box(uint8_t dir, int16_t* x0, int16_t* z0,
								int16_t* x1, int16_t* z1) {
	int16_t ax0 = *x0, az0 = *z0, ax1 = *x1, az1 = *z1;
	int16_t pts[4][2] = {
		{ax0, az0},
		{ax1, az0},
		{ax1, az1},
		{ax0, az1},
	};
	int16_t min_x = 256, min_z = 256, max_x = 0, max_z = 0;

	for(size_t i = 0; i < 4; i++) {
		int16_t x = pts[i][0];
		int16_t z = pts[i][1];
		int16_t rx = x;
		int16_t rz = z;

		switch(dir & 0x03) {
			case 1: rx = 256 - z; rz = x; break;
			case 2: rx = 256 - x; rz = 256 - z; break;
			case 3: rx = z; rz = 256 - x; break;
			default: break;
		}

		if(rx < min_x)
			min_x = rx;
		if(rx > max_x)
			max_x = rx;
		if(rz < min_z)
			min_z = rz;
		if(rz > max_z)
			max_z = rz;
	}

	*x0 = min_x;
	*z0 = min_z;
	*x1 = max_x;
	*z1 = max_z;
}

static size_t render_repeater_box(struct displaylist* d, struct block_info* this,
								  enum side side, uint8_t* vertex_light,
								  bool count_only, int16_t x0, int16_t y0,
								  int16_t z0, int16_t x1, int16_t y1,
								  int16_t z1, uint8_t tex, int tex_rotate) {
	return render_cuboid_side(d, this, side, vertex_light, count_only, x0, y0,
							  z0, x1, y1, z1, tex, tex_rotate);
}

size_t render_block_repeater(struct displaylist* d, struct block_info* this,
							 enum side side, struct block_info* it,
							 uint8_t* vertex_light, bool count_only) {
	uint8_t dir = this->block->metadata & 0x03;
	uint8_t delay = (this->block->metadata >> 2) & 0x03;
	uint8_t top_tex = blocks[this->block->type]->getTextureIndex(this, side);
	uint8_t lamp_tex = tex_atlas_lookup(this->block->type == BLOCK_REPEATER_ON ?
											TEXAT_REPEATER_TORCH_ON :
											TEXAT_REPEATER_TORCH_OFF);
	size_t count = 0;
	int tex_rotate = dir & 0x03;
	int16_t x0, z0, x1, z1;

	if(side == SIDE_TOP) {
		count += render_repeater_box(d, this, side, vertex_light, count_only, 0,
									 16, 0, 256, 32, 256, top_tex, tex_rotate);
	} else if(side == SIDE_BOTTOM) {
		count += render_repeater_box(d, this, side, vertex_light, count_only, 0,
									 0, 0, 256, 16, 256,
									 tex_atlas_lookup(TEXAT_STONE), 0);
	} else {
		count += render_repeater_box(d, this, side, vertex_light, count_only, 0,
									 0, 0, 256, 32, 256,
									 tex_atlas_lookup(TEXAT_STONE), 0);
	}

	x0 = 64; z0 = 176; x1 = 96; z1 = 208;
	repeater_orient_box(dir, &x0, &z0, &x1, &z1);
	count += render_repeater_box(d, this, side, vertex_light, count_only, x0, 32,
								 z0, x1, 80, z1, lamp_tex, 0);

	x0 = 160; z0 = 176; x1 = 192; z1 = 208;
	repeater_orient_box(dir, &x0, &z0, &x1, &z1);
	count += render_repeater_box(d, this, side, vertex_light, count_only, x0, 32,
								 z0, x1, 80, z1, lamp_tex, 0);

	x0 = 112; z0 = 64 + delay * 24; x1 = 144; z1 = 96 + delay * 24;
	repeater_orient_box(dir, &x0, &z0, &x1, &z1);
	count += render_repeater_box(d, this, side, vertex_light, count_only, x0, 32,
								 z0, x1, 80, z1, lamp_tex, 0);

	return count;
}

size_t render_block_torch(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	if(side == SIDE_BOTTOM)
		return 0;

	if(!count_only) {
		int16_t x = W2C_COORD(this->x);
		int16_t y = W2C_COORD(this->y);
		int16_t z = W2C_COORD(this->z);
		uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
		uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
		uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
		uint8_t light = (MAX_U8(this->block->torch_light,
								blocks[this->block->type]->luminance)
						 << 4)
			| this->block->sky_light;

		int s1x, s1y; // wall offset
		int s2x, s2y; // layer shift
		int s3 = 48;  // y offset

		switch(this->block->metadata) {
			case 1:
				s1x = -128;
				s1y = 0;
				s2x = 104;
				s2y = 0;
				break;
			case 2:
				s1x = 128;
				s1y = 0;
				s2x = -104;
				s2y = 0;
				break;
			case 3:
				s1x = 0;
				s1y = -128;
				s2x = 0;
				s2y = 104;
				break;
			case 4:
				s1x = 0;
				s1y = 128;
				s2x = 0;
				s2y = -104;
				break;
			default: s1x = s1y = s2x = s2y = s3 = 0; break;
		}

		switch(side) {
			case SIDE_LEFT:
				displaylist_pos(d, x * BLK_LEN + 112 + s1x, y * BLK_LEN + s3,
								z * BLK_LEN + s1y);
				displaylist_color(d, DIM_LIGHT(light, level_table_1, true, 0));
				displaylist_texcoord(d, tex_x, tex_y + 16);
				displaylist_pos(d, x * BLK_LEN + 112 + s1x + s2x,
								y * BLK_LEN + BLK_LEN + s3,
								z * BLK_LEN + s1y + s2y);
				displaylist_color(d, DIM_LIGHT(light, level_table_1, true, 0));
				displaylist_texcoord(d, tex_x, tex_y);
				displaylist_pos(d, x * BLK_LEN + 112 + s1x + s2x,
								y * BLK_LEN + BLK_LEN + s3,
								z * BLK_LEN + BLK_LEN + s1y + s2y);
				displaylist_color(d, DIM_LIGHT(light, level_table_1, true, 0));
				displaylist_texcoord(d, tex_x + 16, tex_y);
				displaylist_pos(d, x * BLK_LEN + 112 + s1x, y * BLK_LEN + s3,
								z * BLK_LEN + BLK_LEN + s1y);
				displaylist_color(d, DIM_LIGHT(light, level_table_1, true, 0));
				displaylist_texcoord(d, tex_x + 16, tex_y + 16);
				break;
			case SIDE_RIGHT:
				displaylist_pos(d, x * BLK_LEN + 144 + s1x, y * BLK_LEN + s3,
								z * BLK_LEN + s1y);
				displaylist_color(d, DIM_LIGHT(light, level_table_1, true, 0));
				displaylist_texcoord(d, tex_x + 16, tex_y + 16);
				displaylist_pos(d, x * BLK_LEN + 144 + s1x, y * BLK_LEN + s3,
								z * BLK_LEN + BLK_LEN + s1y);
				displaylist_color(d, DIM_LIGHT(light, level_table_1, true, 0));
				displaylist_texcoord(d, tex_x, tex_y + 16);
				displaylist_pos(d, x * BLK_LEN + 144 + s1x + s2x,
								y * BLK_LEN + BLK_LEN + s3,
								z * BLK_LEN + BLK_LEN + s1y + s2y);
				displaylist_color(d, DIM_LIGHT(light, level_table_1, true, 0));
				displaylist_texcoord(d, tex_x, tex_y);
				displaylist_pos(d, x * BLK_LEN + 144 + s1x + s2x,
								y * BLK_LEN + BLK_LEN + s3,
								z * BLK_LEN + s1y + s2y);
				displaylist_color(d, DIM_LIGHT(light, level_table_1, true, 0));
				displaylist_texcoord(d, tex_x + 16, tex_y);
				break;
			case SIDE_BACK:
				displaylist_pos(d, x * BLK_LEN + s1x, y * BLK_LEN + s3,
								z * BLK_LEN + 144 + s1y);
				displaylist_color(d, DIM_LIGHT(light, level_table_2, true, 0));
				displaylist_texcoord(d, tex_x, tex_y + 16);
				displaylist_pos(d, x * BLK_LEN + s1x + s2x,
								y * BLK_LEN + BLK_LEN + s3,
								z * BLK_LEN + 144 + s1y + s2y);
				displaylist_color(d, DIM_LIGHT(light, level_table_2, true, 0));
				displaylist_texcoord(d, tex_x, tex_y);
				displaylist_pos(d, x * BLK_LEN + BLK_LEN + s1x + s2x,
								y * BLK_LEN + BLK_LEN + s3,
								z * BLK_LEN + 144 + s1y + s2y);
				displaylist_color(d, DIM_LIGHT(light, level_table_2, true, 0));
				displaylist_texcoord(d, tex_x + 16, tex_y);
				displaylist_pos(d, x * BLK_LEN + BLK_LEN + s1x,
								y * BLK_LEN + s3, z * BLK_LEN + 144 + s1y);
				displaylist_color(d, DIM_LIGHT(light, level_table_2, true, 0));
				displaylist_texcoord(d, tex_x + 16, tex_y + 16);
				break;
			case SIDE_FRONT:
				displaylist_pos(d, x * BLK_LEN + s1x, y * BLK_LEN + s3,
								z * BLK_LEN + 112 + s1y);
				displaylist_color(d, DIM_LIGHT(light, level_table_2, true, 0));
				displaylist_texcoord(d, tex_x + 16, tex_y + 16);
				displaylist_pos(d, x * BLK_LEN + BLK_LEN + s1x,
								y * BLK_LEN + s3, z * BLK_LEN + 112 + s1y);
				displaylist_color(d, DIM_LIGHT(light, level_table_2, true, 0));
				displaylist_texcoord(d, tex_x, tex_y + 16);
				displaylist_pos(d, x * BLK_LEN + BLK_LEN + s1x + s2x,
								y * BLK_LEN + BLK_LEN + s3,
								z * BLK_LEN + 112 + s1y + s2y);
				displaylist_color(d, DIM_LIGHT(light, level_table_2, true, 0));
				displaylist_texcoord(d, tex_x, tex_y);
				displaylist_pos(d, x * BLK_LEN + s1x + s2x,
								y * BLK_LEN + BLK_LEN + s3,
								z * BLK_LEN + 112 + s1y + s2y);
				displaylist_color(d, DIM_LIGHT(light, level_table_2, true, 0));
				displaylist_texcoord(d, tex_x + 16, tex_y);
				break;
			case SIDE_TOP:
				displaylist_pos(d, x * BLK_LEN + 112 + s1x + s2x * 10 / 16,
								y * BLK_LEN + 160 + s3,
								z * BLK_LEN + 112 + s1y + s2y * 10 / 16);
				displaylist_color(d, light);
				displaylist_texcoord(d, tex_x + 7, tex_y + 6);
				displaylist_pos(d, x * BLK_LEN + 144 + s1x + s2x * 10 / 16,
								y * BLK_LEN + 160 + s3,
								z * BLK_LEN + 112 + s1y + s2y * 10 / 16);
				displaylist_color(d, light);
				displaylist_texcoord(d, tex_x + 9, tex_y + 6);
				displaylist_pos(d, x * BLK_LEN + 144 + s1x + s2x * 10 / 16,
								y * BLK_LEN + 160 + s3,
								z * BLK_LEN + 144 + s1y + s2y * 10 / 16);
				displaylist_color(d, light);
				displaylist_texcoord(d, tex_x + 9, tex_y + 8);
				displaylist_pos(d, x * BLK_LEN + 112 + s1x + s2x * 10 / 16,
								y * BLK_LEN + 160 + s3,
								z * BLK_LEN + 144 + s1y + s2y * 10 / 16);
				displaylist_color(d, light);
				displaylist_texcoord(d, tex_x + 7, tex_y + 8);
				break;
			default: break;
		}
	}

	return 1;
}

size_t render_block_cactus(struct displaylist* d, struct block_info* this,
						   enum side side, struct block_info* it,
						   uint8_t* vertex_light, bool count_only) {
	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			BLK_LEN, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true,
			(side == SIDE_TOP || side == SIDE_BOTTOM) ? 0 : 16, false, 0, side,
			vertex_light);
	return 1;
}

size_t render_block_piston(struct displaylist* d, struct block_info* this,
						   enum side side, struct block_info* it,
						   uint8_t* vertex_light, bool count_only) {
	enum side facing = (enum side)(this->block->metadata & 0x7);
	int tex_rotate = piston_texture_rotate(facing, side);
	const int16_t tip = BLK_LEN / 4;

	if(this->block->metadata & 0x8) {
		int16_t x0 = 0;
		int16_t x1 = BLK_LEN;
		int16_t y0 = 0;
		int16_t y1 = BLK_LEN;
		int16_t z0 = 0;
		int16_t z1 = BLK_LEN;
		uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);

		switch(facing) {
			case SIDE_RIGHT:
				x1 -= tip;
				break;
			case SIDE_LEFT:
				x0 += tip;
				break;
			case SIDE_TOP:
				y1 -= tip;
				break;
			case SIDE_BOTTOM:
				y0 += tip;
				break;
			case SIDE_BACK:
				z1 -= tip;
				break;
			case SIDE_FRONT:
			default:
				z0 += tip;
				break;
		}

		if(side == facing) {
			return render_cuboid_side(
				d, this, side, vertex_light, count_only, x0, y0, z0, x1, y1, z1,
				tex_atlas_lookup(TEXAT_PISTON_FRONT_EXTENDED), tex_rotate);
		}

		if(side != blocks_side_opposite(facing)) {
			tex = tex_atlas_lookup(TEXAT_PISTON_SIDE);
			return render_cuboid_side_texrect(
				d, this, side, vertex_light, count_only, x0, y0, z0, x1, y1, z1,
				tex, tex_rotate, 0, 4, 16, 12);
		}

		return render_cuboid_side(d, this, side, vertex_light, count_only, x0,
								  y0, z0, x1, y1, z1, tex, tex_rotate);
	}

	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			BLK_LEN, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 0, false, tex_rotate,
			side, vertex_light);
	return 1;
}

size_t render_block_piston_head(struct displaylist* d, struct block_info* this,
								enum side side, struct block_info* it,
								uint8_t* vertex_light, bool count_only) {
	enum side facing = (enum side)(this->block->metadata & 0x7);
	const int16_t tip = BLK_LEN / 4;
	int16_t x0 = 0;
	int16_t x1 = BLK_LEN;
	int16_t y0 = 0;
	int16_t y1 = BLK_LEN;
	int16_t z0 = 0;
	int16_t z1 = BLK_LEN;

	switch(facing) {
		case SIDE_RIGHT:
			x0 = BLK_LEN - tip;
			break;
		case SIDE_LEFT:
			x1 = tip;
			break;
		case SIDE_TOP:
			y0 = BLK_LEN - tip;
			break;
		case SIDE_BOTTOM:
			y1 = tip;
			break;
		case SIDE_BACK:
			z0 = BLK_LEN - tip;
			break;
		case SIDE_FRONT:
		default:
			z1 = tip;
			break;
	}

	if(side == facing || side == blocks_side_opposite(facing)) {
		uint8_t tex = tex_atlas_lookup(TEXAT_PISTON_PLATE);
		return render_cuboid_side(d, this, side, vertex_light, count_only, x0,
								  y0, z0, x1, y1, z1, tex,
								  piston_head_side_rotate(facing, side));
	}

	return render_cuboid_side_texrect(
		d, this, side, vertex_light, count_only, x0, y0, z0, x1, y1, z1,
		tex_atlas_lookup(TEXAT_PISTON_SIDE),
		piston_head_side_rotate(facing, side),
		0, 0, 16, 4);
}

size_t render_block_piston_always(struct displaylist* d, struct block_info* this,
								  enum side side, struct block_info* it,
								  uint8_t* vertex_light, bool count_only) {
	if(!(this->block->metadata & 0x8))
		return 0;

	const int16_t rod_min = BLK_LEN * 6 / 16;
	const int16_t rod_max = BLK_LEN * 10 / 16;
	const int16_t tip = BLK_LEN / 4;
	int16_t x0 = rod_min;
	int16_t x1 = rod_max;
	int16_t y0 = rod_min;
	int16_t y1 = rod_max;
	int16_t z0 = rod_min;
	int16_t z1 = rod_max;

	switch((enum side)(this->block->metadata & 0x7)) {
		case SIDE_RIGHT:
			x0 = BLK_LEN - tip;
			x1 = BLK_LEN * 2 - tip;
			break;
		case SIDE_LEFT:
			x0 = -BLK_LEN + tip;
			x1 = tip;
			break;
		case SIDE_TOP:
			y0 = BLK_LEN - tip;
			y1 = BLK_LEN * 2 - tip;
			break;
		case SIDE_BOTTOM:
			y0 = -BLK_LEN + tip;
			y1 = tip;
			break;
		case SIDE_BACK:
			z0 = BLK_LEN - tip;
			z1 = BLK_LEN * 2 - tip;
			break;
		case SIDE_FRONT:
		default:
			z0 = -BLK_LEN + tip;
			z1 = tip;
			break;
	}

	return render_cuboid_side_texrect(
		d, this, side, vertex_light, count_only, x0, y0, z0, x1, y1, z1,
		tex_atlas_lookup(TEXAT_PISTON_SIDE),
		(piston_head_side_rotate((enum side)(this->block->metadata & 0x7), side)
		 + 1)
			% 4,
		0, 0, 16, 4);
}

size_t render_block_portal(struct displaylist* d, struct block_info* this,
						   enum side side, struct block_info* it,
						   uint8_t* vertex_light, bool count_only) {
	// TODO: handle case with neighbour obsidian blocks correctly (low priority)
	if(it->block->type == BLOCK_OBSIDIAN || it->block->type == BLOCK_PORTAL
	   || side == SIDE_TOP || side == SIDE_BOTTOM)
		return 0;

	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			BLK_LEN, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 96, false, 0, side,
			vertex_light);
	return 1;
}

size_t render_block_fluid(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	if(!count_only) {
		uint16_t height = (this->block->metadata & 0x8) ?
			BLK_LEN :
			(8 - this->block->metadata) * 14 * 2;
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			height, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 0, false, 0, side,
			vertex_light);
	}

	return 1;
}


size_t render_block_rail(struct displaylist* dl,
                         struct block_info* this,
                         enum side side,
                         struct block_info* it,
                         uint8_t* vertex_light,
                         bool count_only) {
    if (side != SIDE_TOP) return 0;
    if (count_only)       return 1;

    int16_t x = W2C_COORD(this->x);
    int16_t y = W2C_COORD(this->y);
    int16_t z = W2C_COORD(this->z);

    uint8_t tex       = blocks[this->block->type]->getTextureIndex(this, side);
    uint8_t luminance = blocks[this->block->type]->luminance;

    uint8_t shape     = this->block->metadata & 0xF;
    bool    is_slope  = (shape >= 2 && shape <= 5);
    int     tex_rotate;

    if (shape == 0) {
        // NS
        tex_rotate = 0;
    } else if (shape == 1) {
        // EW
        tex_rotate = 1;
    } else if (is_slope) {
        // slopes: X‐slopes (2,3) 90°
        tex_rotate = (shape == 2 || shape == 3) ? 1 : 0;
    } else {
        // curves 6–9: map 6->0°, 7->270°, 8->180°, 9->90°
    	// `(shape-6)&3` is in [0..3], subtract from 4 then mod 4
    	tex_rotate = (4 - ((shape - 6) & 3)) & 3;
    }

    bool is_curve = (shape >= 6 && shape <= 9);

    if (is_curve && blocks[this->block->type]->render_block_data.rail_curved_possible) {
        tex = tex_atlas_lookup(TEXAT_RAIL_CURVED);
    }

    uint8_t tx = TEX_OFFSET(TEXTURE_X(tex));
    uint8_t ty = TEX_OFFSET(TEXTURE_Y(tex));
    uint8_t uv[4][2] = {
        { tx,      ty      },
        { tx + 16, ty      },
        { tx + 16, ty + 16 },
        { tx,      ty + 16 },
    };

    // vertex heights: flat or sloped (16 = flat, 272 = 16+256)
    uint16_t h[4] = {16, 16, 16, 16};
    if (is_slope) {
        switch (shape) {
            case 2: h[1] = h[2] = 272; break;
            case 3: h[0] = h[3] = 272; break;
            case 4: h[0] = h[1] = 272; break;
            case 5: h[2] = h[3] = 272; break;
        }
    }

    for (int i = 0; i < 4; i++) {
        int vi = (tex_rotate + i) & 3;
        displaylist_pos(dl,
            x * BLK_LEN + ((i == 1 || i == 2) ? BLK_LEN : 0),
            y * BLK_LEN + h[i],
            z * BLK_LEN + ((i >= 2) ? BLK_LEN : 0)
        );
        displaylist_color(dl,
            DIM_LIGHT(vertex_light[4 + i], NULL, false, luminance)
        );
        displaylist_texcoord(dl,
            uv[vi][0], uv[vi][1]
        );
    }

    return 1;
}

// ----------- render_block_redstone_wire() -----------------
// #define REDSTONE_CASE_DEBUG

static inline int neighbours_ext_index(int dx, int dy, int dz) {
	int idx = 0;
	for (int z = -1; z <= 1; ++z) {
		for (int y = -1; y <= 1; ++y) {
			for (int x = -1; x <= 1; ++x) {
				if (x == 0 && y == 0 && z == 0) continue;
				if (x == dx && y == dy && z == dz) return idx;
				idx++;
			}
		}
	}
	return -1;
}

static inline bool neighbours_ext_get(struct block_info* info,
										int dx, int dy, int dz,
										struct block_data* out) {
	if (!info || !info->neighbours_ext) return false;
	int idx = neighbours_ext_index(dx, dy, dz);
	if (idx < 0) return false;
	*out = info->neighbours_ext[idx];
	return true;
}
static inline bool is_redstone_source(uint8_t type, uint8_t meta) {
	if (type == BLOCK_REDSTONE_WIRE) return true;
	if (type == BLOCK_REDSTONE_TORCH || type == BLOCK_REDSTONE_TORCH_LIT)
		return true;
	if ((type == BLOCK_STONE_PRESSURE_PLATE || type == BLOCK_WOOD_PRESSURE_PLATE)
		&& (meta & 0x0F) == 1) {
		return true;
	}
	return false;
}

static inline enum tex_atlas_entry redstone_variant_base(uint8_t variant) {
	enum tex_atlas_entry base_off;
	switch (variant) {
		case 1:
			base_off = TEXAT_REDSTONE_MIDDLE_OFF;
			break;
		case 2:
			base_off = TEXAT_REDSTONE_WIRE_OFF;
			break;
		case 3:
			base_off = TEXAT_REDSTONE_STOCK_OFF;
			break;
		case 0:
		default:
			base_off = TEXAT_REDSTONE_OFF;
			break;
	}
	return base_off;
}

static inline uint8_t redstone_vertex_light(uint8_t base, uint8_t level) {
	(void)base;
	// buckets: 0 = very dark, 1-5 = bright, 6-10 = medium, 11-15 = dim
	uint8_t v;
	if (level == 0) {
		v = 7;
	} else if (level <= 2) {
		v = 8;
	} else if (level <= 4) {
		v = 9;
	} else if (level <= 6) {
		v = 10;
	} else if (level <= 8) {
		v = 11;
	} else if (level <= 10) {
		v = 12;
	} else if (level <= 12) {
		v = 13;
	} else if (level <= 14) {
		v = 14;
	} else {
		v = 30;
	}
	return (v << 4) | v;
}

static void render_redstone(struct displaylist* dl, int16_t x, int16_t y, int16_t z,
							uint16_t a, uint16_t b, uint16_t c, uint16_t d,
							uint8_t level, const uint8_t (*tex_coords)[2],
							uint8_t* vertex_light, int tex_rotate)
	{
	displaylist_pos(dl, x * BLK_LEN, y * BLK_LEN + a, z * BLK_LEN);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[4], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(tex_rotate + 0) % 4][0],
							tex_coords[(tex_rotate + 0) % 4][1]);
	displaylist_pos(dl, x * BLK_LEN + BLK_LEN, y * BLK_LEN + b,
					z * BLK_LEN);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[5], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(tex_rotate + 1) % 4][0],
							tex_coords[(tex_rotate + 1) % 4][1]);
	displaylist_pos(dl, x * BLK_LEN + BLK_LEN, y * BLK_LEN + c,
					z * BLK_LEN + BLK_LEN);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[6], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(tex_rotate + 2) % 4][0],
							tex_coords[(tex_rotate + 2) % 4][1]);
	displaylist_pos(dl, x * BLK_LEN, y * BLK_LEN + d,
					z * BLK_LEN + BLK_LEN);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[7], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(tex_rotate + 3) % 4][0],
							tex_coords[(tex_rotate + 3) % 4][1]);
	}

static void render_redstone_offset(struct displaylist* dl, int16_t x, int16_t y, int16_t z,
								   uint16_t a, uint16_t b, uint16_t c, uint16_t d,
								   uint8_t level, const uint8_t (*tex_coords)[2],
								   uint8_t* vertex_light, int tex_rotate,
								   int16_t ox, int16_t oz)
	{
	displaylist_pos(dl, x * BLK_LEN + ox, y * BLK_LEN + a, z * BLK_LEN + oz);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[4], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(tex_rotate + 0) % 4][0],
							tex_coords[(tex_rotate + 0) % 4][1]);
	displaylist_pos(dl, x * BLK_LEN + BLK_LEN + ox, y * BLK_LEN + b,
					z * BLK_LEN + oz);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[5], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(tex_rotate + 1) % 4][0],
							tex_coords[(tex_rotate + 1) % 4][1]);
	displaylist_pos(dl, x * BLK_LEN + BLK_LEN + ox, y * BLK_LEN + c,
					z * BLK_LEN + BLK_LEN + oz);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[6], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(tex_rotate + 2) % 4][0],
							tex_coords[(tex_rotate + 2) % 4][1]);
	displaylist_pos(dl, x * BLK_LEN + ox, y * BLK_LEN + d,
					z * BLK_LEN + BLK_LEN + oz);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[7], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(tex_rotate + 3) % 4][0],
							tex_coords[(tex_rotate + 3) % 4][1]);
	}

static void render_redstone_vertical(struct displaylist* dl, int16_t x, int16_t y, int16_t z,
									 uint8_t level, const uint8_t (*tex_coords)[2],
									 uint8_t* vertex_light, int tex_rotate, int dir)
	{
	const int16_t offset = -4;
	const int16_t y_top = y * BLK_LEN + BLK_LEN;
	const int16_t y_bot = y * BLK_LEN + 16;

	int16_t x0 = x * BLK_LEN;
	int16_t x1 = x * BLK_LEN + BLK_LEN;
	int16_t z0 = z * BLK_LEN;
	int16_t z1 = z * BLK_LEN + BLK_LEN;

	switch (dir) {
		case 0: // +X (right)
			x0 = x1 = x * BLK_LEN + BLK_LEN + offset;
			break;
		case 1: // -X (left)
			x0 = x1 = x * BLK_LEN - offset;
			break;
		case 2: // +Z (front)
			z0 = z1 = z * BLK_LEN - offset;
			break;
		case 3: // -Z (back)
			z0 = z1 = z * BLK_LEN + BLK_LEN + offset;
			break;
		default:
			return;
	}

	int rot = tex_rotate;
	if (dir == 0 || dir == 1)
		rot = (tex_rotate + 1) % 4;

	displaylist_pos(dl, x0, y_top, z0);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[4], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(rot + 0) % 4][0],
							tex_coords[(rot + 0) % 4][1]);
	displaylist_pos(dl, x1, y_top, z1);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[5], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(rot + 1) % 4][0],
							tex_coords[(rot + 1) % 4][1]);
	displaylist_pos(dl, x1, y_bot, z1);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[6], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(rot + 2) % 4][0],
							tex_coords[(rot + 2) % 4][1]);
	displaylist_pos(dl, x0, y_bot, z0);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[7], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(rot + 3) % 4][0],
							tex_coords[(rot + 3) % 4][1]);

	// Draw back face so it is visible regardless of culling.
	displaylist_pos(dl, x0, y_bot, z0);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[7], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(rot + 3) % 4][0],
							tex_coords[(rot + 3) % 4][1]);
	displaylist_pos(dl, x1, y_bot, z1);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[6], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(rot + 2) % 4][0],
							tex_coords[(rot + 2) % 4][1]);
	displaylist_pos(dl, x1, y_top, z1);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[5], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(rot + 1) % 4][0],
							tex_coords[(rot + 1) % 4][1]);
	displaylist_pos(dl, x0, y_top, z0);
	displaylist_color(dl, redstone_vertex_light(
		DIM_LIGHT(vertex_light[4], NULL, false, 0), level));
	displaylist_texcoord(dl, tex_coords[(rot + 0) % 4][0],
							tex_coords[(rot + 0) % 4][1]);
	}

// todo: This is currently mostly copied from block-rail. Adapt this to:
// - show corner/intersection texture where needed
size_t render_block_redstone_wire(struct displaylist* dl, struct block_info* this,
						 enum side side, struct block_info* it,
						 uint8_t* vertex_light, bool count_only) {
	if(side != SIDE_TOP) {
		#ifdef REDSTONE_CASE_DEBUG
		printf("return 0;\n");
		#endif
		return 0;
	}

	if(!count_only) {

		bool has_near = false;
		uint8_t near_mask = 0; // bit0=+X, bit1=-X, bit2=+Z, bit3=-Z

		struct block_data above;
		bool above_air = neighbours_ext_get(this, 0, +1, 0, &above)
			&& above.type == BLOCK_AIR;

		#ifdef REDSTONE_CASE_DEBUG
		{
		struct block_data above;
		if (neighbours_ext_get(this, 0, +1, 0, &above)) {
		    printf("above: type=%u meta=%u at %d,%d,%d\n",
		           above.type, above.metadata, this->x, this->y + 1, this->z);
		} else {
		    printf("above: (no data)\n");
		}
		}
		
		printf(above_air ? "above_air = true\n" : "above_air = false\n");
		#endif

		const struct { int dx, dz; enum side side; uint8_t bit; } dirs[4] = {
			{ +1,  0, SIDE_RIGHT, 1 << 0 }, // +X
			{ -1,  0, SIDE_LEFT,  1 << 1 }, // -X
			{  0, -1, SIDE_FRONT, 1 << 3 }, // +Z
			{  0, +1, SIDE_BACK,  1 << 2 }, // -Z
			//{  0, +1, SIDE_FRONT, 1 << 2 }
		};

		for (int i = 0; i < 4; ++i) {
			enum side sdir = dirs[i].side;
			uint8_t ntype = this->neighbours ? this->neighbours[sdir].type : BLOCK_AIR;
			uint8_t nmeta = this->neighbours ? (this->neighbours[sdir].metadata & 0x0F) : 0;

			bool dir_near = false;
			if (is_redstone_source(ntype, nmeta)) {
				dir_near = true;
			}

			// down-diagonal only if side block is air
			if (!dir_near && ntype == BLOCK_AIR) {
				struct block_data btmp;
				if (neighbours_ext_get(this, dirs[i].dx, -1, dirs[i].dz, &btmp)) {
					if (is_redstone_source(btmp.type, btmp.metadata)) {
						dir_near = true;
					}
				}
			}

			// up-diagonal if block above current is air (regardless of side block)
			if (!dir_near && above_air) {
				struct block_data btmp;
				if (neighbours_ext_get(this, dirs[i].dx, +1, dirs[i].dz, &btmp)) {
					if (is_redstone_source(btmp.type, btmp.metadata)) {
						dir_near = true;
					}
				}
			}

			if (dir_near) {
				has_near = true;
				near_mask |= dirs[i].bit;
			}
		}
		
		// Zusatzliste: 0 = nix, 1 = direkt/unter, 2 = oben (bei Luft ueber dem Block)
		uint8_t dir_state[4] = {0, 0, 0, 0};
		#ifdef REDSTONE_CASE_DEBUG
		printf("wo ist der block\n");
		#endif
		for (int i = 0; i < 4; ++i) {
			enum side sdir = dirs[i].side;
			uint8_t ntype = this->neighbours ? this->neighbours[sdir].type : BLOCK_AIR;
			uint8_t nmeta = this->neighbours ? (this->neighbours[sdir].metadata & 0x0F) : 0;

			if (is_redstone_source(ntype, nmeta)) {
				dir_state[i] = 1;
				continue;
				#ifdef REDSTONE_CASE_DEBUG
				printf("1\n");
				#endif
			}

			if (ntype == BLOCK_AIR) {
				struct block_data btmp;
				if (neighbours_ext_get(this, dirs[i].dx, -1, dirs[i].dz, &btmp)) {
					if (is_redstone_source(btmp.type, btmp.metadata)) {
						dir_state[i] = 1;
						continue;
						#ifdef REDSTONE_CASE_DEBUG
						printf("2\n");
						#endif
					}
				}
			}

			if (above_air) {
				struct block_data btmp;
				if (neighbours_ext_get(this, dirs[i].dx, +1, dirs[i].dz, &btmp)) {
					if (is_redstone_source(btmp.type, btmp.metadata)) {
						dir_state[i] = 2;
						#ifdef REDSTONE_CASE_DEBUG
						printf("3\n");
						#endif
					}
				}
			}
			#ifdef REDSTONE_CASE_DEBUG
			printf("nix da\n");
			#endif
		}
		
		// --------------------------------------------------
		int16_t x = W2C_COORD(this->x);
		int16_t y = W2C_COORD(this->y);
		int16_t z = W2C_COORD(this->z);

		bool zero = false;
		for (int i = 0; i < 4; i++) {
			if (dir_state[i] == 0) zero = true;
		}

		#ifdef REDSTONE_CASE_DEBUG
		printf("%d    %d    %d    %d\n", dir_state[0], dir_state[1], dir_state[2], dir_state[3]);
		#endif

		uint8_t tex;
		int cases = -1;
		int tex_rotate = 0;
		
		// straight
		if   (dir_state[0] != 0 && dir_state[1] != 0
		   && dir_state[2] == 0 && dir_state[3] == 0
		   
		   || dir_state[0] != 0 && dir_state[1] == 0
		   && dir_state[2] == 0 && dir_state[3] == 0 
		   
		   || dir_state[0] == 0 && dir_state[1] != 0
		   && dir_state[2] == 0 && dir_state[3] == 0)
	   {
			cases      = 0;
			tex_rotate = 0;
		} else 
		if   (dir_state[0] == 0 && dir_state[1] == 0
		   && dir_state[2] != 0 && dir_state[3] != 0
		   
		   || dir_state[0] == 0 && dir_state[1] == 0
		   && dir_state[2] != 0 && dir_state[3] == 0 
		   
		   || dir_state[0] == 0 && dir_state[1] == 0
		   && dir_state[2] == 0 && dir_state[3] != 0) 
		{
			cases      = 0;
			tex_rotate = 1;
		} else 
		// curve
		{
			if (dir_state[0] == 0 && dir_state[1] != 0
		     && dir_state[2] == 0 && dir_state[3] != 0) 
			{
				cases = 1;
				tex_rotate = 3;
		   	} else
		   	if (dir_state[0] == 0 && dir_state[1] != 0
		     && dir_state[2] != 0 && dir_state[3] == 0) 
		   	{
				cases = 1;
				tex_rotate = 2;
			} else
			if (dir_state[0] != 0 && dir_state[1] == 0
		     && dir_state[2] != 0 && dir_state[3] == 0) 
		   	{
				cases = 1;
				tex_rotate = 1;
			} else
			if (dir_state[0] != 0 && dir_state[1] == 0
		     && dir_state[2] == 0 && dir_state[3] != 0) 
		   	{
				cases = 1;
				tex_rotate = 0;
			} else {
				if (dir_state[0] != 0 && dir_state[1] != 0
			     && dir_state[2] == 0 && dir_state[3] != 0) 
				{
					cases = 2;
					tex_rotate = 3;
			   	} else
			   	if (dir_state[0] == 0 && dir_state[1] != 0
			     && dir_state[2] != 0 && dir_state[3] != 0) 
			   	{
					cases = 2;
					tex_rotate = 2;
				} else
				if (dir_state[0] != 0 && dir_state[1] == 0
			     && dir_state[2] != 0 && dir_state[3] != 0) 
			   	{
					cases = 2;
					tex_rotate = 0;
				} else
				if (dir_state[0] != 0 && dir_state[1] != 0
			     && dir_state[2] != 0 && dir_state[3] == 0) 
		   		{
					cases = 2;
					tex_rotate = 1;
				} else {
					cases = 3;
				}
			}	
		}
		//uint8_t lvl = this->block->metadata & 0x0F;
		//tex = tex_atlas_lookup(redstone_variant_entry(variant, lvl));
		//} else { // all 
			//cases = 3;
		//}

		uint8_t lvl = this->block->metadata & 0x0F;

		uint16_t a = 16, b = 16, c = 16, d = 16;

		/*switch(this->block->metadata & 0x7) {
			case 1: tex_rotate = 1; break;
			case 2:
				b = 272;
				c = 272;
				tex_rotate = 1;
				break;
			case 3:
				a = 272;
				d = 272;
				tex_rotate = 1;
				break;
			case 4:
				a = 272;
				b = 272;
				break;
			case 5:
				c = 272;
				d = 272;
				break;
		}*/

		/*if(blocks[this->block->type]->render_block_data.rail_curved_possible) {
			switch(this->block->metadata) {
				case 6: tex_rotate = 0; break;
				case 7: tex_rotate = 3; break;
				case 8: tex_rotate = 2; break;
			}
		}*/
		
		uint8_t variant;
		//cases = 5;
		#ifdef REDSTONE_CASE_DEBUG
		printf("cases=%d\n", cases);
		#endif

		switch (cases) {
			case 0: { // ----------------------- 0 ------------
				#ifdef REDSTONE_CASE_DEBUG
				printf("case 0\n");
				#endif
				variant = 2; // 0..3: 0=redstone,1=middle,2=wire,3=stock

				tex = tex_atlas_lookup(redstone_variant_base(variant));

				#ifdef REDSTONE_CASE_DEBUG
				printf("pos=%d,%d,%d side=%d above_air=%d  ds=%d %d %d %d\n",
           			this->x,this->y,this->z, side, above_air,
           			dir_state[0],dir_state[1],dir_state[2],dir_state[3]);
				printf("case=%d variant=%d tex=%u tx=%u ty=%u rot=%d\n",
       				cases, variant, tex, TEXTURE_X(tex), TEXTURE_Y(tex), tex_rotate);
				printf("zusatz:\n");
				printf("case=%d variant=%d base=%d tex=%u tx=%u ty=%u\n",
       				cases, variant, redstone_variant_base(variant),
       				tex, TEXTURE_X(tex), TEXTURE_Y(tex));
				printf("OFF  : %u %u\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_OFF)));
				printf("WIRE : %u %u\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF)));
				printf("MIDDLE: %u %u\n\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_MIDDLE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_MIDDLE_OFF)));
				#endif

				uint8_t tex_coords[4][2] = {
					{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex))},
					{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex))},
					{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex)) + 16},
					{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex)) + 16},
				};
				render_redstone(dl, x, y, z, a, b, c, d, lvl, tex_coords,
								vertex_light, tex_rotate);
				break;
			} case 1: { // ----------------------- 1 ------------
				#ifdef REDSTONE_CASE_DEBUG
				printf("case 1\n");
				#endif
				variant = 3; // 0..3: 0=redstone,1=middle,2=wire,3=stock

				tex = tex_atlas_lookup(redstone_variant_base(variant));

				#ifdef REDSTONE_CASE_DEBUG
				printf("pos=%d,%d,%d side=%d above_air=%d  ds=%d %d %d %d\n",
           			this->x,this->y,this->z, side, above_air,
           			dir_state[0],dir_state[1],dir_state[2],dir_state[3]);
				printf("case=%d variant=%d tex=%u tx=%u ty=%u rot=%d\n",
       				cases, variant, tex, TEXTURE_X(tex), TEXTURE_Y(tex), tex_rotate);
				printf("zusatz:\n");
				printf("case=%d variant=%d base=%d tex=%u tx=%u ty=%u\n",
       				cases, variant, redstone_variant_base(variant),
       				tex, TEXTURE_X(tex), TEXTURE_Y(tex));
				printf("OFF  : %u %u\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_OFF)));
				printf("WIRE : %u %u\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF)));
				printf("MIDDLE: %u %u\n\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_MIDDLE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_MIDDLE_OFF)));
				#endif
				

				uint8_t tex_coords[4][2] = {
					{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex))},
					{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex))},
					{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex)) + 16},
					{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex)) + 16},
				};
				render_redstone(dl, x, y, z, a, b, c, d, lvl, tex_coords,
							vertex_light, tex_rotate);
				break;
			} case 2: { // ----------------------- 2 ------------
				#ifdef REDSTONE_CASE_DEBUG
				printf("case 1\n");
				#endif
				variant = 1; // 0..3: 0=redstone,1=middle,2=wire,3=stock

				tex = tex_atlas_lookup(redstone_variant_base(variant));
				
				#ifdef REDSTONE_CASE_DEBUG
				printf("pos=%d,%d,%d side=%d above_air=%d  ds=%d %d %d %d\n",
           			this->x,this->y,this->z, side, above_air,
           			dir_state[0],dir_state[1],dir_state[2],dir_state[3]);
				printf("case=%d variant=%d tex=%u tx=%u ty=%u rot=%d\n",
       				cases, variant, tex, TEXTURE_X(tex), TEXTURE_Y(tex), tex_rotate);
				printf("zusatz:\n");
				printf("case=%d variant=%d base=%d tex=%u tx=%u ty=%u\n",
       				cases, variant, redstone_variant_base(variant),
       				tex, TEXTURE_X(tex), TEXTURE_Y(tex));
				printf("OFF  : %u %u\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_OFF)));
				printf("WIRE : %u %u\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF)));
				printf("MIDDLE: %u %u\n\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_MIDDLE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_MIDDLE_OFF)));
				#endif
				

				uint8_t tex_coords[4][2] = {
					{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex))},
					{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex))},
					{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex)) + 16},
					{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex)) + 16},
				};
				render_redstone(dl, x, y, z, a, b, c, d, lvl, tex_coords,
							vertex_light, tex_rotate);
				break;
			} 
			case 3:		
			default: { // ----------------------- D ------------
				tex_rotate = 0;
				variant = 0; // 0..3: 0=redstone,1=middle,2=wire,3=stock

				#ifdef REDSTONE_CASE_DEBUG
				printf("default\n");
				#endif

				tex = tex_atlas_lookup(redstone_variant_base(variant));

				#ifdef REDSTONE_CASE_DEBUG
				printf("pos=%d,%d,%d side=%d above_air=%d  ds=%d %d %d %d\n",
           			this->x,this->y,this->z, side, above_air,
           			dir_state[0],dir_state[1],dir_state[2],dir_state[3]);
				printf("case=%d variant=%d tex=%u tx=%u ty=%u rot=%d\n",
       				cases, variant, tex, TEXTURE_X(tex), TEXTURE_Y(tex), tex_rotate);
				printf("zusatz:\n");
				printf("case=%d variant=%d base=%d tex=%u tx=%u ty=%u\n",
       				cases, variant, redstone_variant_base(variant),
       				tex, TEXTURE_X(tex), TEXTURE_Y(tex));
				printf("OFF  : %u %u\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_OFF)));
				printf("WIRE : %u %u\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_WIRE_OFF)));
				printf("MIDDLE: %u %u\n\n", TEXTURE_X(tex_atlas_lookup(TEXAT_REDSTONE_MIDDLE_OFF)), 
						TEXTURE_Y(tex_atlas_lookup(TEXAT_REDSTONE_MIDDLE_OFF)));
				#endif
				
				uint8_t tex_coords[4][2] = {
					{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex))},
					{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex))},
					{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex)) + 16},
					{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex)) + 16},
				};
				render_redstone(dl, x, y, z, a, b, c, d, lvl, tex_coords,
								vertex_light, tex_rotate);
			}
		}
			uint8_t tex_coords[4][2] = {
				{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex))},
				{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex))},
				{TEX_OFFSET(TEXTURE_X(tex)) + 16, TEX_OFFSET(TEXTURE_Y(tex)) + 16},
				{TEX_OFFSET(TEXTURE_X(tex)), TEX_OFFSET(TEXTURE_Y(tex)) + 16},
			};
			/*render_redstone(dl, x, y, z, a, b, c, d, lvl, tex_coords,
							vertex_light, tex_rotate);*/
			if (above_air) {
				uint8_t slope_tex = tex_atlas_lookup(redstone_variant_base(2));
				uint8_t slope_tex_coords[4][2] = {
					{TEX_OFFSET(TEXTURE_X(slope_tex)), TEX_OFFSET(TEXTURE_Y(slope_tex))},
					{TEX_OFFSET(TEXTURE_X(slope_tex)) + 16, TEX_OFFSET(TEXTURE_Y(slope_tex))},
					{TEX_OFFSET(TEXTURE_X(slope_tex)) + 16, TEX_OFFSET(TEXTURE_Y(slope_tex)) + 16},
					{TEX_OFFSET(TEXTURE_X(slope_tex)), TEX_OFFSET(TEXTURE_Y(slope_tex)) + 16},
				};
				for (int i = 0; i < 4; ++i) {
					struct block_data btmp;
					#ifdef REDSTONE_CASE_DEBUG
					printf("?\n");
					#endif
					if (neighbours_ext_get(this, dirs[i].dx, +1, dirs[i].dz, &btmp)
						&& is_redstone_source(btmp.type, btmp.metadata)) {
						#ifdef REDSTONE_CASE_DEBUG
						printf("yes\n");
						#endif
						const int dir_rot[4] = {0, 2, 1, 3}; // +X, -X, +Z, -Z
						int slope_rotate = dir_rot[i & 3];
						render_redstone_vertical(dl, x, y, z, lvl, slope_tex_coords,
												  vertex_light, slope_rotate, i);
					}
					#ifdef REDSTONE_CASE_DEBUG
					bool ok = neighbours_ext_get(this, dirs[i].dx, +1, dirs[i].dz, &btmp);
					printf("above_ok=%d above_type=%u above_meta=%u pos=%d,%d,%d\n",
    						ok, btmp.type, btmp.metadata, this->x, this->y, this->z);
					#endif
					
				}
			}
	}
	//#undef REDSTONE_CASE_DEBUG
	#ifdef REDSTONE_CASE_DEBUG
	else {
		printf("no redstone rendering\n");
	}
	#endif
	return 1;
}

size_t render_block_ladder(struct displaylist* d, struct block_info* this,
						   enum side side, struct block_info* it,
						   uint8_t* vertex_light, bool count_only) {
	if((this->block->metadata == 2 && side != SIDE_FRONT)
	   || (this->block->metadata == 3 && side != SIDE_BACK)
	   || (this->block->metadata == 4 && side != SIDE_LEFT)
	   || (this->block->metadata == 5 && side != SIDE_RIGHT))
		return 0;

	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			BLK_LEN, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 240, false, 0, side,
			vertex_light);
	return 1;
}

size_t render_block_crops(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	if(side == SIDE_TOP || side == SIDE_BOTTOM)
		return 0;

	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), -16,
			BLK_LEN, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, false, 64, false, 0, side,
			vertex_light);
	return 1;
}

size_t render_block_cake(struct displaylist* d, struct block_info* this,
						 enum side side, struct block_info* it,
						 uint8_t* vertex_light, bool count_only) {
	if(!count_only) {
		uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
		uint8_t luminance = blocks[this->block->type]->luminance;

		switch(side) {
			case SIDE_FRONT:
				render_block_side_adv(d,
									  W2C_COORD(this->x) * BLK_LEN
										  + this->block->metadata * 32 + 16,
									  W2C_COORD(this->y) * BLK_LEN,
									  W2C_COORD(this->z) * BLK_LEN + 16,
									  (7 - this->block->metadata) * 32, 128,
									  TEX_OFFSET(TEXTURE_X(tex)) + 1,
									  TEX_OFFSET(TEXTURE_Y(tex)) + 8, false, 0,
									  true, side, vertex_light, luminance);
				break;
			case SIDE_BACK:
				render_block_side_adv(d,
									  W2C_COORD(this->x) * BLK_LEN
										  + this->block->metadata * 32 + 16,
									  W2C_COORD(this->y) * BLK_LEN,
									  W2C_COORD(this->z) * BLK_LEN + 240,
									  (7 - this->block->metadata) * 32, 128,
									  TEX_OFFSET(TEXTURE_X(tex)) + 1
										  + this->block->metadata * 2,
									  TEX_OFFSET(TEXTURE_Y(tex)) + 8, false, 0,
									  true, side, vertex_light, luminance);
				break;
			case SIDE_TOP:
				render_block_side_adv(d,
									  W2C_COORD(this->x) * BLK_LEN
										  + this->block->metadata * 32 + 16,
									  W2C_COORD(this->y) * BLK_LEN + 128,
									  W2C_COORD(this->z) * BLK_LEN + 16,
									  (7 - this->block->metadata) * 32, 224,
									  TEX_OFFSET(TEXTURE_X(tex)) + 1
										  + this->block->metadata * 2,
									  TEX_OFFSET(TEXTURE_Y(tex)) + 1, false, 0,
									  true, side, vertex_light, luminance);
				break;
			case SIDE_BOTTOM:
				render_block_side_adv(d,
									  W2C_COORD(this->x) * BLK_LEN
										  + this->block->metadata * 32 + 16,
									  W2C_COORD(this->y) * BLK_LEN,
									  W2C_COORD(this->z) * BLK_LEN + 16,
									  (7 - this->block->metadata) * 32, 224,
									  TEX_OFFSET(TEXTURE_X(tex)) + 1
										  + this->block->metadata * 2,
									  TEX_OFFSET(TEXTURE_Y(tex)) + 1, false, 0,
									  true, side, vertex_light, luminance);
				break;
			default:
				render_block_side(
					d, W2C_COORD(this->x), W2C_COORD(this->y),
					W2C_COORD(this->z), 0, 128, tex, luminance, true,
					(side == SIDE_LEFT) ? (16 + this->block->metadata * 32) :
										  16,
					false, 0, side, vertex_light);
		}
	}

	return 1;
}

size_t render_block_farmland(struct displaylist* d, struct block_info* this,
							 enum side side, struct block_info* it,
							 uint8_t* vertex_light, bool count_only) {
	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			240, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 0, false, 0, side,
			vertex_light);
	return 1;
}

size_t render_block_stairs_always(struct displaylist* d,
								  struct block_info* this, enum side side,
								  struct block_info* it, uint8_t* vertex_light,
								  bool count_only) {
	enum side facing = (enum side[4]) {SIDE_RIGHT, SIDE_LEFT, SIDE_BACK,
									   SIDE_FRONT}[this->block->metadata & 3];

	if(side != SIDE_TOP && side != blocks_side_opposite(facing))
		return 0;

	if(!count_only) {
		uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
		uint8_t luminance = blocks[this->block->type]->luminance;

		if(side == SIDE_TOP) {
			render_block_side(d, W2C_COORD(this->x), W2C_COORD(this->y),
							  W2C_COORD(this->z), 0, BLK_LEN / 2, tex,
							  luminance, true, 0, false, 0, side, vertex_light);
		} else if(side == blocks_side_opposite(facing)) {
			render_block_side(d, W2C_COORD(this->x), W2C_COORD(this->y),
							  W2C_COORD(this->z), 0, BLK_LEN, tex, luminance,
							  true, BLK_LEN / 2, false, 0, side, vertex_light);
		}
	}

	return 1;
}

size_t render_block_stairs(struct displaylist* d, struct block_info* this,
						   enum side side, struct block_info* it,
						   uint8_t* vertex_light, bool count_only) {
	size_t k = 0;
	enum side facing = (enum side[4]) {SIDE_RIGHT, SIDE_LEFT, SIDE_BACK,
									   SIDE_FRONT}[this->block->metadata & 3];
	uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
	uint8_t luminance = blocks[this->block->type]->luminance;

	// render "slab"
	if(side == facing) {
		if(!count_only)
			render_block_side(d, W2C_COORD(this->x), W2C_COORD(this->y),
							  W2C_COORD(this->z), 0, BLK_LEN, tex, luminance,
							  true, 0, false, 0, side, vertex_light);
		k++;
	} else if(side != SIDE_TOP) {
		if(!count_only)
			render_block_side(d, W2C_COORD(this->x), W2C_COORD(this->y),
							  W2C_COORD(this->z), 0, BLK_LEN / 2, tex,
							  luminance, true, 0, false, 0, side, vertex_light);
		k++;
	}

	// render top part
	if(side == SIDE_TOP) {
		int tx = (facing == SIDE_RIGHT) ? BLK_LEN / 2 : 0;
		int ty = (facing == SIDE_BACK) ? BLK_LEN / 2 : 0;
		int sx = (facing == SIDE_LEFT || facing == SIDE_RIGHT) ? BLK_LEN / 2 :
																 BLK_LEN;
		int sy = (facing == SIDE_FRONT || facing == SIDE_BACK) ? BLK_LEN / 2 :
																 BLK_LEN;
		if(!count_only)
			render_block_side_adv(d, W2C_COORD(this->x) * BLK_LEN + tx,
								  W2C_COORD(this->y) * BLK_LEN + BLK_LEN,
								  W2C_COORD(this->z) * BLK_LEN + ty, sx, sy,
								  TEX_OFFSET(TEXTURE_X(tex)) + tx / 16,
								  TEX_OFFSET(TEXTURE_Y(tex)) + ty / 16, false,
								  false, true, SIDE_TOP, vertex_light,
								  luminance);
		k++;
	} else if(side != facing && side != blocks_side_opposite(facing)
			  && side != SIDE_BOTTOM) {
		int off[][2] = {
			[SIDE_LEFT] = {0, 0},
			[SIDE_RIGHT] = {BLK_LEN, 0},
			[SIDE_FRONT] = {0, 0},
			[SIDE_BACK] = {0, BLK_LEN},
		};

		if(facing == SIDE_BACK) {
			off[SIDE_LEFT][1] = BLK_LEN / 2;
			off[SIDE_RIGHT][1] = BLK_LEN / 2;
		} else if(facing == SIDE_RIGHT) {
			off[SIDE_FRONT][0] = BLK_LEN / 2;
			off[SIDE_BACK][0] = BLK_LEN / 2;
		}

		// TODO: fix texture offset

		if(!count_only)
			render_block_side_adv(
				d, W2C_COORD(this->x) * BLK_LEN + off[side][0],
				W2C_COORD(this->y) * BLK_LEN + BLK_LEN / 2,
				W2C_COORD(this->z) * BLK_LEN + off[side][1], BLK_LEN / 2,
				BLK_LEN / 2, TEX_OFFSET(TEXTURE_X(tex)),
				TEX_OFFSET(TEXTURE_Y(tex)), false, false, true, side,
				vertex_light, luminance);
		k++;
	}

	return k;
}

size_t render_block_bed(struct displaylist* d, struct block_info* this,
						enum side side, struct block_info* it,
						uint8_t* vertex_light, bool count_only) {
	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			144, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true,
			(side == SIDE_BOTTOM) ? 48 : 0,
			(side == SIDE_RIGHT && (this->block->metadata & 0x3) == 0)
				|| (side == SIDE_BACK && (this->block->metadata & 0x3) == 1)
				|| (side == SIDE_LEFT && (this->block->metadata & 0x3) == 2)
				|| (side == SIDE_FRONT && (this->block->metadata & 0x3) == 3),
			(side == SIDE_TOP || side == SIDE_BOTTOM) ?
				(3 - (this->block->metadata & 0x3)) :
				0,
			side, vertex_light);
	return 1;
}

size_t render_block_fire(struct displaylist* d, struct block_info* this,
						 enum side side, struct block_info* it,
						 uint8_t* vertex_light, bool count_only) {
	if(side == SIDE_TOP)
		return 0;

	size_t k = 0;
	bool is_floating = !blocks[this->neighbours[SIDE_BOTTOM].type]
		|| (blocks[this->neighbours[SIDE_BOTTOM].type]->can_see_through
			&& !blocks[this->neighbours[SIDE_BOTTOM].type]->flammable);

	int16_t x = W2C_COORD(this->x);
	int16_t y = W2C_COORD(this->y);
	int16_t z = W2C_COORD(this->z);
	uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
	uint8_t luminance = blocks[this->block->type]->luminance;
	uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
	uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
	uint16_t height = 360;

	if(is_floating) {
		switch(side) {
			case SIDE_BOTTOM:
				if(blocks[this->neighbours[SIDE_TOP].type]
				   && blocks[this->neighbours[SIDE_TOP].type]->flammable) {
					if(!count_only) {
						render_block_side_adv_v2(
							d, x * BLK_LEN, y * BLK_LEN + BLK_LEN, z * BLK_LEN,
							BLK_LEN, BLK_LEN, 0, -3 * 16, tex_x, tex_y, false,
							0, false, SIDE_BOTTOM, vertex_light, luminance);
						render_block_side_adv_v2(
							d, x * BLK_LEN, y * BLK_LEN + BLK_LEN, z * BLK_LEN,
							BLK_LEN, BLK_LEN, -3 * 16, 0, tex_x, tex_y, false,
							2, false, SIDE_BOTTOM, vertex_light, luminance);
					}
					k += 2;
				}
				break;
			case SIDE_LEFT:
				if(blocks[this->neighbours[SIDE_RIGHT].type]
				   && blocks[this->neighbours[SIDE_RIGHT].type]->flammable) {
					if(!count_only)
						render_block_side_adv_v2(
							d, x * BLK_LEN + BLK_LEN, y * BLK_LEN + 16,
							z * BLK_LEN, BLK_LEN, height, 0, -48, tex_x, tex_y,
							false, false, false, SIDE_LEFT, vertex_light,
							luminance);
					k++;
				}
				break;
			case SIDE_RIGHT:
				if(blocks[this->neighbours[SIDE_LEFT].type]
				   && blocks[this->neighbours[SIDE_LEFT].type]->flammable) {
					if(!count_only)
						render_block_side_adv_v2(
							d, x * BLK_LEN, y * BLK_LEN + 16, z * BLK_LEN,
							BLK_LEN, height, 0, -48, tex_x, tex_y, false, false,
							false, SIDE_RIGHT, vertex_light, luminance);
					k++;
				}
				break;
			case SIDE_FRONT:
				if(blocks[this->neighbours[SIDE_BACK].type]
				   && blocks[this->neighbours[SIDE_BACK].type]->flammable) {
					if(!count_only)
						render_block_side_adv_v2(
							d, x * BLK_LEN, y * BLK_LEN + 16,
							z * BLK_LEN + BLK_LEN, BLK_LEN, height, 0, -48,
							tex_x, tex_y, false, false, false, SIDE_FRONT,
							vertex_light, luminance);
					k++;
				}
				break;
			case SIDE_BACK:
				if(blocks[this->neighbours[SIDE_FRONT].type]
				   && blocks[this->neighbours[SIDE_FRONT].type]->flammable) {
					if(!count_only)
						render_block_side_adv_v2(
							d, x * BLK_LEN, y * BLK_LEN + 16, z * BLK_LEN,
							BLK_LEN, height, 0, -48, tex_x, tex_y, false, false,
							false, SIDE_BACK, vertex_light, luminance);
					k++;
				}
				break;
			default: break;
		}
	} else {
		switch(side) {
			case SIDE_LEFT:
				if(!count_only) {
					render_block_side_adv_v2(
						d, x * BLK_LEN, y * BLK_LEN, z * BLK_LEN, BLK_LEN,
						height, 0, 24, tex_x, tex_y, false, false, false,
						SIDE_LEFT, vertex_light, luminance);

					render_block_side_adv_v2(
						d, x * BLK_LEN + 5 * 16, y * BLK_LEN, z * BLK_LEN,
						BLK_LEN, height, 0, BLK_LEN / 2, tex_x, tex_y, false,
						false, false, SIDE_LEFT, vertex_light, luminance);
				}
				k += 2;
				break;
			case SIDE_RIGHT:
				if(!count_only) {
					render_block_side_adv_v2(
						d, x * BLK_LEN + BLK_LEN, y * BLK_LEN, z * BLK_LEN,
						BLK_LEN, height, 0, 24, tex_x, tex_y, false, false,
						false, SIDE_RIGHT, vertex_light, luminance);

					render_block_side_adv_v2(
						d, x * BLK_LEN + BLK_LEN - 5 * 16, y * BLK_LEN,
						z * BLK_LEN, BLK_LEN, height, 0, BLK_LEN / 2, tex_x,
						tex_y, false, false, false, SIDE_RIGHT, vertex_light,
						luminance);
				}
				k += 2;
				break;
			case SIDE_FRONT:
				if(!count_only) {
					render_block_side_adv_v2(
						d, x * BLK_LEN, y * BLK_LEN, z * BLK_LEN, BLK_LEN,
						height, 0, 24, tex_x, tex_y, false, false, false,
						SIDE_FRONT, vertex_light, luminance);

					render_block_side_adv_v2(
						d, x * BLK_LEN, y * BLK_LEN, z * BLK_LEN + 5 * 16,
						BLK_LEN, height, 0, BLK_LEN / 2, tex_x, tex_y, false,
						false, false, SIDE_FRONT, vertex_light, luminance);
				}
				k += 2;
				break;
			case SIDE_BACK:
				if(!count_only) {
					render_block_side_adv_v2(
						d, x * BLK_LEN, y * BLK_LEN, z * BLK_LEN + BLK_LEN,
						BLK_LEN, height, 0, 24, tex_x, tex_y, false, false,
						false, SIDE_BACK, vertex_light, luminance);

					render_block_side_adv_v2(
						d, x * BLK_LEN, y * BLK_LEN,
						z * BLK_LEN + BLK_LEN - 5 * 16, BLK_LEN, height, 0,
						BLK_LEN / 2, tex_x, tex_y, false, false, false,
						SIDE_BACK, vertex_light, luminance);
				}
				k += 2;
				break;
			default: break;
		}
	}

	return k;
}

size_t render_block_pressure_plate(struct displaylist* d,
								   struct block_info* this, enum side side,
								   struct block_info* it, uint8_t* vertex_light,
								   bool count_only) {
	if(!count_only) {
		uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
		uint8_t luminance = blocks[this->block->type]->luminance;
		uint8_t height = this->block->metadata ? 8 : 16;

		switch(side) {
			case SIDE_TOP:
				render_block_side_adv(
					d, W2C_COORD(this->x) * BLK_LEN + 16,
					W2C_COORD(this->y) * BLK_LEN + height,
					W2C_COORD(this->z) * BLK_LEN + 16, BLK_LEN - 32,
					BLK_LEN - 32, TEX_OFFSET(TEXTURE_X(tex)) + 1,
					TEX_OFFSET(TEXTURE_Y(tex)) + 1, false, false, true,
					SIDE_TOP, vertex_light, luminance);
				break;
			case SIDE_BOTTOM:
				render_block_side_adv(
					d, W2C_COORD(this->x) * BLK_LEN + 16,
					W2C_COORD(this->y) * BLK_LEN,
					W2C_COORD(this->z) * BLK_LEN + 16, BLK_LEN - 32,
					BLK_LEN - 32, TEX_OFFSET(TEXTURE_X(tex)) + 1,
					TEX_OFFSET(TEXTURE_Y(tex)) + 1, false, false, true,
					SIDE_BOTTOM, vertex_light, luminance);
				break;
			case SIDE_FRONT:
			case SIDE_LEFT:
				render_block_side_adv(
					d, W2C_COORD(this->x) * BLK_LEN + 16,
					W2C_COORD(this->y) * BLK_LEN,
					W2C_COORD(this->z) * BLK_LEN + 16, BLK_LEN - 32, height,
					TEX_OFFSET(TEXTURE_X(tex)) + 1,
					TEX_OFFSET(TEXTURE_Y(tex)) + 1, false, false, true, side,
					vertex_light, luminance);
				break;
			case SIDE_RIGHT:
				render_block_side_adv(
					d, W2C_COORD(this->x) * BLK_LEN + BLK_LEN - 16,
					W2C_COORD(this->y) * BLK_LEN,
					W2C_COORD(this->z) * BLK_LEN + 16, BLK_LEN - 32, height,
					TEX_OFFSET(TEXTURE_X(tex)) + 1,
					TEX_OFFSET(TEXTURE_Y(tex)) + 1, false, false, true,
					SIDE_RIGHT, vertex_light, luminance);
				break;
			case SIDE_BACK:
				render_block_side_adv(
					d, W2C_COORD(this->x) * BLK_LEN + 16,
					W2C_COORD(this->y) * BLK_LEN,
					W2C_COORD(this->z) * BLK_LEN + BLK_LEN - 16, BLK_LEN - 32,
					height, TEX_OFFSET(TEXTURE_X(tex)) + 1,
					TEX_OFFSET(TEXTURE_Y(tex)) + 1, false, false, true,
					SIDE_BACK, vertex_light, luminance);
				break;
			default: break;
		}
	}

	return 1;
}

size_t render_block_fence_always(struct displaylist* d, struct block_info* this,
								 enum side side, struct block_info* it,
								 uint8_t* vertex_light, bool count_only) {
	size_t k = 0;
	int16_t x = W2C_COORD(this->x);
	int16_t y = W2C_COORD(this->y);
	int16_t z = W2C_COORD(this->z);
	uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
	uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
	uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
	uint8_t luminance = blocks[this->block->type]->luminance;
	bool connect_pos_x = this->neighbours[SIDE_RIGHT].type == this->block->type;
	bool connect_pos_z = this->neighbours[SIDE_BACK].type == this->block->type;

	// TODO: textures are not perfect but I'll take it

	switch(side) {
		case SIDE_LEFT:
			if(connect_pos_z && !count_only) {
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 7 / 16,
									  y * BLK_LEN + BLK_LEN * 12 / 16,
									  z * BLK_LEN + BLK_LEN * 10 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN * 3 / 16,
									  tex_x + 2, tex_y + 9, false, 0, true,
									  SIDE_LEFT, vertex_light, luminance);
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 7 / 16,
									  y * BLK_LEN + BLK_LEN * 6 / 16,
									  z * BLK_LEN + BLK_LEN * 10 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN * 3 / 16,
									  tex_x + 2, tex_y + 11, false, 0, true,
									  SIDE_LEFT, vertex_light, luminance);
			}

			k += connect_pos_z ? 2 : 0;
			break;
		case SIDE_RIGHT:
			if(connect_pos_z && !count_only) {
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 9 / 16,
									  y * BLK_LEN + BLK_LEN * 12 / 16,
									  z * BLK_LEN + BLK_LEN * 10 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN * 3 / 16,
									  tex_x + 2, tex_y + 9, false, 0, true,
									  SIDE_RIGHT, vertex_light, luminance);
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 9 / 16,
									  y * BLK_LEN + BLK_LEN * 6 / 16,
									  z * BLK_LEN + BLK_LEN * 10 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN * 3 / 16,
									  tex_x + 2, tex_y + 11, false, 0, true,
									  SIDE_RIGHT, vertex_light, luminance);
			}

			k += connect_pos_z ? 2 : 0;
			break;
		case SIDE_BOTTOM:
			if(connect_pos_x && !count_only) {
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 10 / 16,
									  y * BLK_LEN + BLK_LEN * 12 / 16,
									  z * BLK_LEN + BLK_LEN * 7 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN / 8, tex_x,
									  tex_y, false, 0, true, SIDE_BOTTOM,
									  vertex_light, luminance);
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 10 / 16,
									  y * BLK_LEN + BLK_LEN * 6 / 16,
									  z * BLK_LEN + BLK_LEN * 7 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN / 8, tex_x,
									  tex_y, false, 0, true, SIDE_BOTTOM,
									  vertex_light, luminance);
			}

			if(connect_pos_z && !count_only) {
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 7 / 16,
									  y * BLK_LEN + BLK_LEN * 12 / 16,
									  z * BLK_LEN + BLK_LEN * 10 / 16,
									  BLK_LEN / 8, BLK_LEN * 3 / 4, tex_x,
									  tex_y, false, 0, true, SIDE_BOTTOM,
									  vertex_light, luminance);
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 7 / 16,
									  y * BLK_LEN + BLK_LEN * 6 / 16,
									  z * BLK_LEN + BLK_LEN * 10 / 16,
									  BLK_LEN / 8, BLK_LEN * 3 / 4, tex_x,
									  tex_y, false, 0, true, SIDE_BOTTOM,
									  vertex_light, luminance);
			}

			k += connect_pos_x ? 2 : 0;
			k += connect_pos_z ? 2 : 0;
			break;
		case SIDE_TOP:
			if(connect_pos_x && !count_only) {
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 10 / 16,
									  y * BLK_LEN + BLK_LEN * 15 / 16,
									  z * BLK_LEN + BLK_LEN * 7 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN / 8, tex_x,
									  tex_y, false, 0, true, SIDE_TOP,
									  vertex_light, luminance);
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 10 / 16,
									  y * BLK_LEN + BLK_LEN * 9 / 16,
									  z * BLK_LEN + BLK_LEN * 7 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN / 8, tex_x,
									  tex_y, false, 0, true, SIDE_TOP,
									  vertex_light, luminance);
			}

			if(connect_pos_z && !count_only) {
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 7 / 16,
									  y * BLK_LEN + BLK_LEN * 15 / 16,
									  z * BLK_LEN + BLK_LEN * 10 / 16,
									  BLK_LEN / 8, BLK_LEN * 3 / 4, tex_x,
									  tex_y, false, 0, true, SIDE_TOP,
									  vertex_light, luminance);
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 7 / 16,
									  y * BLK_LEN + BLK_LEN * 9 / 16,
									  z * BLK_LEN + BLK_LEN * 10 / 16,
									  BLK_LEN / 8, BLK_LEN * 3 / 4, tex_x,
									  tex_y, false, 0, true, SIDE_TOP,
									  vertex_light, luminance);
			}

			k += connect_pos_x ? 2 : 0;
			k += connect_pos_z ? 2 : 0;
			break;
		case SIDE_FRONT:
			if(connect_pos_x && !count_only) {
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 10 / 16,
									  y * BLK_LEN + BLK_LEN * 12 / 16,
									  z * BLK_LEN + BLK_LEN * 7 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN * 3 / 16,
									  tex_x + 2, tex_y + 9, false, 0, true,
									  SIDE_FRONT, vertex_light, luminance);
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 10 / 16,
									  y * BLK_LEN + BLK_LEN * 6 / 16,
									  z * BLK_LEN + BLK_LEN * 7 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN * 3 / 16,
									  tex_x + 2, tex_y + 11, false, 0, true,
									  SIDE_FRONT, vertex_light, luminance);
			}

			k += connect_pos_x ? 2 : 0;
			break;
		case SIDE_BACK:
			if(connect_pos_x && !count_only) {
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 10 / 16,
									  y * BLK_LEN + BLK_LEN * 12 / 16,
									  z * BLK_LEN + BLK_LEN * 9 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN * 3 / 16,
									  tex_x + 2, tex_y + 9, false, 0, true,
									  SIDE_BACK, vertex_light, luminance);
				render_block_side_adv(d, x * BLK_LEN + BLK_LEN * 10 / 16,
									  y * BLK_LEN + BLK_LEN * 6 / 16,
									  z * BLK_LEN + BLK_LEN * 9 / 16,
									  BLK_LEN * 3 / 4, BLK_LEN * 3 / 16,
									  tex_x + 2, tex_y + 11, false, 0, true,
									  SIDE_BACK, vertex_light, luminance);
			}

			k += connect_pos_x ? 2 : 0;
			break;
		default: break;
	}

	return k;
}

size_t render_block_fence(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	if(!count_only) {
		int16_t x = W2C_COORD(this->x);
		int16_t y = W2C_COORD(this->y);
		int16_t z = W2C_COORD(this->z);
		uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
		uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
		uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
		uint8_t luminance = blocks[this->block->type]->luminance;

		// TODO: textures are not perfect but I'll take it

		switch(side) {
			case SIDE_LEFT:
				render_block_side_adv(
					d, x * BLK_LEN + BLK_LEN * 3 / 8, y * BLK_LEN,
					z * BLK_LEN + BLK_LEN * 3 / 8, BLK_LEN / 4, BLK_LEN,
					tex_x + 6, tex_y, false, 0, true, SIDE_LEFT, vertex_light,
					luminance);
				break;
			case SIDE_RIGHT:
				render_block_side_adv(
					d, x * BLK_LEN + BLK_LEN - BLK_LEN * 3 / 8, y * BLK_LEN,
					z * BLK_LEN + BLK_LEN * 3 / 8, BLK_LEN / 4, BLK_LEN,
					tex_x + 6, tex_y, false, 0, true, SIDE_RIGHT, vertex_light,
					luminance);
				break;
			case SIDE_BOTTOM:
				render_block_side_adv(
					d, x * BLK_LEN + BLK_LEN * 3 / 8, y * BLK_LEN,
					z * BLK_LEN + BLK_LEN * 3 / 8, BLK_LEN / 4, BLK_LEN / 4,
					tex_x, tex_y, false, 0, true, SIDE_BOTTOM, vertex_light,
					luminance);
				break;
			case SIDE_TOP:
				render_block_side_adv(
					d, x * BLK_LEN + BLK_LEN * 3 / 8, y * BLK_LEN + BLK_LEN,
					z * BLK_LEN + BLK_LEN * 3 / 8, BLK_LEN / 4, BLK_LEN / 4,
					tex_x, tex_y, false, 0, true, SIDE_TOP, vertex_light,
					luminance);
				break;
			case SIDE_FRONT:
				render_block_side_adv(
					d, x * BLK_LEN + BLK_LEN * 3 / 8, y * BLK_LEN,
					z * BLK_LEN + BLK_LEN * 3 / 8, BLK_LEN / 4, BLK_LEN,
					tex_x + 6, tex_y, false, 0, true, SIDE_FRONT, vertex_light,
					luminance);
				break;
			case SIDE_BACK:
				render_block_side_adv(
					d, x * BLK_LEN + BLK_LEN * 3 / 8, y * BLK_LEN,
					z * BLK_LEN + BLK_LEN - BLK_LEN * 3 / 8, BLK_LEN / 4,
					BLK_LEN, tex_x + 6, tex_y, false, 0, true, SIDE_BACK,
					vertex_light, luminance);
				break;
			default: break;
		}
	}

	return 1;
}

static size_t door_side_helper(struct displaylist* d, struct block_info* this,
							   enum side front, enum side side,
							   uint8_t* vertex_light, bool flip_front,
							   bool flip_back, bool count_only) {
	size_t count = 0;
	uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
	uint8_t luminance = blocks[this->block->type]->luminance;
	uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
	uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
	int16_t x = W2C_COORD(this->x);
	int16_t y = W2C_COORD(this->y);
	int16_t z = W2C_COORD(this->z);

	if(side == front) {
		if(!count_only)
			render_block_side(d, x, y, z, 0, 256, tex, luminance, true,
							  BLK_LEN / 16 * 13, flip_front, 0, side,
							  vertex_light);
		count++;
	} else if(side == blocks_side_opposite(front)) {
		if(!count_only)
			render_block_side(d, x, y, z, 0, 256, tex, luminance, true, 0,
							  flip_back, 0, side, vertex_light);
		count++;
	} else if(side == SIDE_TOP || side == SIDE_BOTTOM) {
		if(!count_only)
			render_block_side_adv(
				d, x * BLK_LEN + (front == SIDE_LEFT ? BLK_LEN / 16 * 13 : 0),
				y * BLK_LEN + (side == SIDE_TOP ? BLK_LEN : 0),
				z * BLK_LEN + (front == SIDE_FRONT ? BLK_LEN / 16 * 13 : 0),
				(front == SIDE_LEFT || front == SIDE_RIGHT) ? BLK_LEN / 16 * 3 :
															  BLK_LEN,
				(front == SIDE_FRONT || front == SIDE_BACK) ? BLK_LEN / 16 * 3 :
															  BLK_LEN,
				tex_x + (front == SIDE_LEFT ? 13 : 0), tex_y,
				side == SIDE_BOTTOM, 0, true, side, vertex_light, luminance);
		count++;
	} else {
		if(!count_only)
			render_block_side_adv(
				d,
				x * BLK_LEN + (side == SIDE_RIGHT ? BLK_LEN : 0)
					+ (front == SIDE_LEFT ? BLK_LEN / 16 * 13 : 0),
				y * BLK_LEN,
				z * BLK_LEN + (side == SIDE_BACK ? BLK_LEN : 0)
					+ (front == SIDE_FRONT ? BLK_LEN / 16 * 13 : 0),
				BLK_LEN / 16 * 3, BLK_LEN, tex_x, tex_y, false, 0, true, side,
				vertex_light, luminance);
		count++;
	}

	return count;
}

static size_t sign_side_helper(struct displaylist* d, struct block_info* this,
							   enum side front, enum side side,
							   uint8_t* vertex_light, bool flip_front,
							   bool flip_back, bool count_only) {
	size_t count = 0;
	uint8_t tex = blocks[this->block->type]->getTextureIndex(this, side);
	uint8_t luminance = blocks[this->block->type]->luminance;
	uint8_t tex_x = TEX_OFFSET(TEXTURE_X(tex));
	uint8_t tex_y = TEX_OFFSET(TEXTURE_Y(tex));
	int16_t x = W2C_COORD(this->x);
	int16_t y = W2C_COORD(this->y);
	int16_t z = W2C_COORD(this->z);

	if(side == front) {
		if(!count_only)
			render_block_side(d, x, y, z, 64, 128, tex, luminance, true,
							  BLK_LEN / 16 * 15, flip_front, 0, side,
							  vertex_light);
		count++;
	} else if(side == blocks_side_opposite(front)) {
		if(!count_only)
			render_block_side(d, x, y, z, 64, 128, tex, luminance, true, 0,
							  flip_back, 0, side, vertex_light);
		count++;
	} else if(side == SIDE_TOP || side == SIDE_BOTTOM) {
		if(!count_only)
			render_block_side_adv(
				d, x * BLK_LEN + (front == SIDE_LEFT ? BLK_LEN / 16 * 15 : 0),
				y * BLK_LEN + (side == SIDE_TOP ? (128+64) : 64),
				z * BLK_LEN + (front == SIDE_FRONT ? BLK_LEN / 16 * 15 : 0),
				(front == SIDE_LEFT || front == SIDE_RIGHT) ? BLK_LEN / 16 * 1 :
															  BLK_LEN,
				(front == SIDE_FRONT || front == SIDE_BACK) ? BLK_LEN / 16 * 1 :
															  BLK_LEN,
				tex_x + (front == SIDE_LEFT ? 15 : 0), tex_y,
				side == SIDE_BOTTOM, 0, true, side, vertex_light, luminance);
		count++;
	} else {
		if(!count_only)
			render_block_side_adv(
				d,
				x * BLK_LEN + (side == SIDE_RIGHT ? BLK_LEN : 0)
					+ (front == SIDE_LEFT ? BLK_LEN / 16 * 15 : 0),
				y * BLK_LEN + 64,
				z * BLK_LEN + (side == SIDE_BACK ? BLK_LEN : 0)
					+ (front == SIDE_FRONT ? BLK_LEN / 16 * 15 : 0),
				BLK_LEN / 16 * 1, BLK_LEN / 2, tex_x, tex_y, false, 0, true, side,
				vertex_light, luminance);
		count++;
	}

	return count;
}



size_t render_block_trapdoor(struct displaylist* d,
                             struct block_info* this,
                             enum side side,
                             struct block_info* it,
                             uint8_t* vertex_light,
                             bool count_only)
{
    uint8_t m      = this->block->metadata;
    bool    open   = (m & 0x04) != 0;   // open-flag
    uint8_t orient = m & 0x03;          //  0..3

    if (open) {
        static const enum side map[4] = {
            SIDE_FRONT, SIDE_BACK,
            SIDE_LEFT,  SIDE_RIGHT
        };
        enum side front = map[orient];

        return door_side_helper(
            d, this,
            front, side,
            vertex_light,
	        !open,
	        open,
            count_only
        );
    } else {
        if (!count_only) {
            uint8_t tex = blocks[this->block->type]
                              ->getTextureIndex(this, side);
            uint8_t lumi = blocks[this->block->type]->luminance;
            render_block_side(
                d,
                W2C_COORD(this->x),
                W2C_COORD(this->y),
                W2C_COORD(this->z),
                /*y-offset*/   0,            // height 0..3/16
                /*thickness*/  BLK_LEN / 16 * 3,
                tex, lumi,
                /*double-sided=*/true,
                /*u=*/0, /*flip_u=*/false,
                /*v=*/0, /*side=*/side,
                vertex_light
            );
        }
        return 1;
    }
}

size_t render_block_sign(struct displaylist* d, struct block_info* this,
							 enum side side, struct block_info* it,
							 uint8_t* vertex_light, bool count_only) {
	size_t count = 0;

	if(this->block->metadata & 0x04) {
		count += sign_side_helper(
			d, this,
			(enum side[]) {SIDE_FRONT, SIDE_BACK, SIDE_LEFT,
						   SIDE_RIGHT}[this->block->metadata & 0x03],
			side, vertex_light, false, false, count_only);
	} else {
		if(!count_only)
			render_block_side(
				d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z),
				0, BLK_LEN / 16 * 3,
				blocks[this->block->type]->getTextureIndex(this, side),
				blocks[this->block->type]->luminance, true, 0, false, 0, side,
				vertex_light);
		count++;
	}

	return count;
}

size_t render_block_door(struct displaylist* d,
                         struct block_info* this,
                         enum side side,
                         struct block_info* it,
                         uint8_t* vertex_light,
                         bool count_only)
{
    uint8_t dir  = this->block->metadata & 0x03;
    bool    open = (this->block->metadata & 0x04) != 0;
    uint8_t state;

    if (!open) {
        state = dir;
    } else {
        state = (dir + 3) % 4;
    }

    static const enum side frontForDir[4] = {
        SIDE_RIGHT,  // 0 = east
        SIDE_BACK,   // 1 = south
        SIDE_LEFT,   // 2 = west
        SIDE_FRONT   // 3 = north
    };

    return door_side_helper(
        d,
        this,
        frontForDir[state],
        side,
        vertex_light,
        !open,
        open,
        count_only
    );
}


size_t render_block_layer(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			(this->block->metadata + 1) * 32,
			blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 0, false, 0, side,
			vertex_light);
	return 1;
}

size_t render_block_slab(struct displaylist* d, struct block_info* this,
						 enum side side, struct block_info* it,
						 uint8_t* vertex_light, bool count_only) {
	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			128, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 0, false, 0, side,
			vertex_light);
	return 1;
}

size_t render_block_full(struct displaylist* d, struct block_info* this,
						 enum side side, struct block_info* it,
						 uint8_t* vertex_light, bool count_only) {
	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			BLK_LEN, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 0, false, 0, side,
			vertex_light);
	return 1;
}

size_t render_block_furnace(struct displaylist* d, struct block_info* this,
						 enum side side, struct block_info* it,
						 uint8_t* vertex_light, bool count_only) {
	//TODO: luminance equals to metadata
	if(!count_only)
		render_block_side(
			d, W2C_COORD(this->x), W2C_COORD(this->y), W2C_COORD(this->z), 0,
			BLK_LEN, blocks[this->block->type]->getTextureIndex(this, side),
			blocks[this->block->type]->luminance, true, 0, false, 0, side,
			vertex_light);
	return 1;
}


static struct displaylist block_cracks_dl;
static uint8_t block_cracks_light[24];

void render_block_init() {
	displaylist_init(&block_cracks_dl, 48, 3 * 2 + 2 * 1 + 1);
	memset(block_cracks_light, 0xFF, sizeof(block_cracks_light));
    for (int s = SIDE_TOP; s < SIDE_MAX; ++s) {
    }
}

static uint8_t block_cracks_texture(struct block_info* this, enum side side) {
	struct item_data it;
	inventory_get_hotbar_item(
		windowc_get_latest(gstate_windows()[WINDOWC_INVENTORY]), &it);
	int delay = tool_dig_delay_ms(blocks[this->block->type], item_get(&it));
	int dt = time_diff_ms(gstate.digging.start, time_get()) / (delay / 10);

	if(dt >= 9)
		dt = 9;

	return tex_atlas_lookup(TEXAT_BREAK_0 + dt);
}

void render_block_cracks(struct block_data* blk, mat4 view, w_coord_t x,
						 w_coord_t y, w_coord_t z) {
	assert(blk && view);
	struct block* b = blocks[blk->type];

	if(b && b->digging.hardness > 0) {
		displaylist_reset(&block_cracks_dl);

		struct block_data neighbours[6];
		struct block_data neighbours_ext[26];
		struct block_info neighbours_info[6];

		for(int k = 0; k < SIDE_MAX; k++) {
			int ox, oy, oz;
			blocks_side_offset(k, &ox, &oy, &oz);

			neighbours[k]
				= world_get_block(&gstate.world, x + ox, y + oy, z + oz);

			neighbours_info[k] = (struct block_info) {
				.block = neighbours + k,
				.neighbours = NULL,
				.x = x + ox,
				.y = y + oy,
				.z = z + oz,
			};
		}

		bool want_ext = (blk->type == BLOCK_REDSTONE_WIRE);
		if (want_ext) {
			int ei = 0;
			for (int dz = -1; dz <= 1; ++dz) {
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (dx == 0 && dy == 0 && dz == 0) continue;
						neighbours_ext[ei++]
							= world_get_block(&gstate.world, x + dx, y + dy, z + dz);
					}
				}
			}
		}

		struct block_info local_info = (struct block_info) {
			.block = blk,
			.neighbours = neighbours,
			.neighbours_ext = want_ext ? neighbours_ext : NULL,
			.x = x,
			.y = y,
			.z = z,
		};

		uint8_t (*tmp)(struct block_info*, enum side) = b->getTextureIndex;
		b->getTextureIndex = block_cracks_texture;

		size_t vertices = 0;

		for(int k = 0; k < SIDE_MAX; k++) {
			vertices += b->renderBlock(&block_cracks_dl, &local_info, k,
									   neighbours_info + k, block_cracks_light,
									   false);
			if(b->renderBlockAlways)
				vertices += b->renderBlockAlways(&block_cracks_dl, &local_info,
												 k, neighbours_info + k,
												 block_cracks_light, false);
		}

		blocks[local_info.block->type]->getTextureIndex = tmp;

		mat4 mv;
		glm_translate_to(view,
						 (vec3) {WCOORD_CHUNK_OFFSET(x) * CHUNK_SIZE,
								 y / CHUNK_SIZE * CHUNK_SIZE,
								 WCOORD_CHUNK_OFFSET(z) * CHUNK_SIZE},
						 mv);
		gfx_matrix_modelview(mv);

		gfx_blending(MODE_BLEND3);
		gfx_depth_func(MODE_EQUAL);

		if(b->double_sided)
			gfx_cull_func(MODE_NONE);

		gfx_bind_texture(&texture_terrain);
		displaylist_render_immediate(&block_cracks_dl, vertices * 4);

		if(b->double_sided)
			gfx_cull_func(MODE_BACK);

		gfx_depth_func(MODE_LEQUAL);
		gfx_blending(MODE_OFF);
	}
}
