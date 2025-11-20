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

#include <assert.h>

#include "../../platform/gfx.h"
#include "../game_state.h"
#include "screen.h"

struct screen* screen_stack[MAX_SCREEN_STACK];
int screen_stack_top = -1;

void screen_set(struct screen* s) {
	assert(s);
	gstate.current_screen = s;
	if(s->reset)
		s->reset(s, gfx_width(), gfx_height());
}

void screen_back() {
    if(screen_stack_top > 0) {
        screen_stack_top--;
        struct screen* s = screen_stack[screen_stack_top];
        gstate.current_screen = s;

        if(s->reset)
            s->reset(s, gfx_width(), gfx_height());
    }
}



void menu_screen_set(struct screen* s) {
    assert(s);
    
    for(int i=0; i<=screen_stack_top; i++) {
        if(screen_stack[i] == s) return; // Schon im Stack
    }

    if(screen_stack_top < MAX_SCREEN_STACK - 1) {
        screen_stack[++screen_stack_top] = s;
    }

    gstate.current_screen = s;

    if(s->reset)
        s->reset(s, gfx_width(), gfx_height());
}

