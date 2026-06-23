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

#include <GL/glew.h>
#include <assert.h>
#include <malloc.h>
#include <string.h>

#include "../displaylist.h"

#define MEM_U8(b, i) (*((uint8_t*)(b) + (i)))
#define MEM_U16(b, i) (*(uint16_t*)((uint8_t*)(b) + (i)))
#define MEM_I16(b, i) (*(int16_t*)((uint8_t*)(b) + (i)))
#define MEM_FLT(b, i) (*(float*)((uint8_t*)(b) + (i)))

void displaylist_init(struct displaylist* l, size_t vertices,
					  size_t vertex_size) {
	assert(l && vertices > 0 && vertex_size > 0);

	l->length = 4096;
	l->data = NULL;
	l->index = 0;
	l->finished = false;
	l->failed = false;
}

void displaylist_destroy(struct displaylist* l) {
	assert(l);

	if(l->data)
		free(l->data);

	if(l->finished)
		glDeleteBuffers(1, &l->vbo);

	l->data = NULL;
	l->failed = false;
}

void displaylist_reset(struct displaylist* l) {
	assert(l && !l->finished);
	l->index = 0;
	l->failed = false;
}

void displaylist_finalize(struct displaylist* l, uint16_t vtxcnt) {
	assert(l && !l->finished && l->data);

	l->index = vtxcnt;
}

void displaylist_pos(struct displaylist* l, int16_t x, int16_t y, int16_t z) {
	assert(l && !l->finished);

	if(l->failed)
		return;

	if(!l->data) {
		l->data = malloc(l->length);
		if(!l->data) {
			l->failed = true;
			return;
		}
	}

	if(l->index + 18 > l->length) {
		l->length *= 2;
		void* tmp = realloc(l->data, l->length);
		if(!tmp) {
			free(l->data);
			l->data = NULL;
			l->failed = true;
			return;
		}
		l->data = tmp;
	}

	MEM_FLT(l->data, l->index) = (float)x / 256.0F;
	l->index += 4;
	MEM_FLT(l->data, l->index) = (float)y / 256.0F;
	l->index += 4;
	MEM_FLT(l->data, l->index) = (float)z / 256.0F;
	l->index += 4;
}

void displaylist_color(struct displaylist* l, uint8_t index) {
	assert(l && !l->finished);
	if(l->failed || !l->data)
		return;

	MEM_U8(l->data, l->index++) = index % 16;
	MEM_U8(l->data, l->index++) = index / 16;
}

void displaylist_texcoord(struct displaylist* l, uint16_t s, uint16_t t) {
	assert(l && !l->finished);
	if(l->failed || !l->data)
		return;
	MEM_U16(l->data, l->index) = s;
	l->index += 2;
	MEM_U16(l->data, l->index) = t;
	l->index += 2;
}

void displaylist_render(struct displaylist* l) {
	assert(l);
	if(l->failed || !l->data)
		return;

	if(!l->finished) {
		l->finished = true;

		glGenBuffers(1, &l->vbo);
		glBindBuffer(GL_ARRAY_BUFFER, l->vbo);
		glBufferData(GL_ARRAY_BUFFER, l->index * 18, l->data, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(3);
	glEnableVertexAttribArray(2);

	glBindBuffer(GL_ARRAY_BUFFER, l->vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 18, NULL);
	glVertexAttribPointer(3, 2, GL_UNSIGNED_BYTE, GL_FALSE, 18,
						  (uint8_t*)(3 * sizeof(float)));
	glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_FALSE, 18,
						  (uint8_t*)(2 + 3 * sizeof(float)));
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glDrawArrays(GL_QUADS, 0, l->index);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(2);
}

void displaylist_render_immediate(struct displaylist* l, uint16_t vtxcnt) {
	assert(l && !l->finished);
	if(l->failed || !l->data)
		return;
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(3);
	glEnableVertexAttribArray(2);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 18, l->data);
	glVertexAttribPointer(3, 2, GL_UNSIGNED_BYTE, GL_FALSE, 18,
						  (uint8_t*)l->data + 3 * sizeof(float));
	glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_FALSE, 18,
						  (uint8_t*)l->data + 2 + 3 * sizeof(float));

	glDrawArrays(GL_QUADS, 0, vtxcnt);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(2);
}
