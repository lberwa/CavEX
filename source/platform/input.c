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

// Temporary local 2-player test mode. With TEST_MULTIPLAYER_INPUT enabled:
// - Key mappings are read from `config.json` (and optional per-player overrides
//   like `input.p1.*`) like in normal mode.
// - Only exception: on PC there is only one mouse. For players > 0, mouse
//   buttons are emulated via keyboard keys Z/U/H.
#ifdef PLATFORM_PC
//#define TEST_MULTIPLAYER_INPUT
#endif

#ifndef SPLITSCREEN
#define SPLITSCREEN 2
#endif

#include <assert.h>

#include "../cglm/cglm.h"
#include "gfx.h"
#include "input.h"

#define MAX_WIIMOTES 4

#ifdef PLATFORM_PC

#include <GLFW/glfw3.h>

extern GLFWwindow* window;

static bool input_pointer_enabled;
static double input_old_pointer_x, input_old_pointer_y;
static bool input_key_held[1024];

void input_init() {
	for(int k = 0; k < 1024; k++)
		input_key_held[k] = false;

	input_pointer_enabled = false;
	input_old_pointer_x = 0;
	input_old_pointer_y = 0;
}

void input_poll() { }

	void input_native_key_status(int key, int player, bool* pressed, bool* released,
								 bool* held) {
		int orig_key = key;
		// On PC we have one shared keyboard/mouse. The higher layers pass a
		// `player` index; for local splitscreen testing we can interpret that here.
	#ifdef TEST_MULTIPLAYER_INPUT
		// PC has only one mouse. For players > 0, emulate mouse buttons using
		// keyboard keys:
		// - mouse0 (1000) -> Z
		// - mouse1 (1001) -> U
		// - mouse2 (1002) -> H
		if(player > 0 && key >= 1000 && key <= 1002) {
			switch(key) {
				case 1000: key = 90; break; // Z
				case 1001: key = 85; break; // U
				case 1002: key = 72; break; // H
				default: break;
			}
		}
	#endif

		if(key < 0 || key >= 1024) {
			*pressed = false;
			*released = false;
			*held = false;
			return;
		}

		int state = 0;
		if(orig_key >= 1000) {
	#ifdef TEST_MULTIPLAYER_INPUT
			if(player > 0) {
				// Emulated mouse buttons (Z/U/H) are queried as keys.
				state = glfwGetKey(window, key);
			} else
	#endif
			{
				state = glfwGetMouseButton(window, orig_key - 1000);
			}
		} else {
			state = glfwGetKey(window, key);
		}

	*pressed = (state == GLFW_PRESS) && !input_key_held[key];
	*released = (state == GLFW_RELEASE) && input_key_held[key];
	*held = !(*released) && input_key_held[key];

#ifdef TEST_MULTIPLAYER_INPUT
	// Debug: show whether the per-player mapping actually happens at runtime.
	// Only print on press/release events to avoid spamming.
	static bool printed_p1_wasd_held = false;
	if((*pressed || *released)
	   && (orig_key == 87 || orig_key == 83 || orig_key == 65 || orig_key == 68
		   || orig_key == 73 || orig_key == 75 || orig_key == 74 || orig_key == 76
		   || orig_key == 1000 || orig_key == 1001)) {
		printf("[input_native_key_status] player=%d orig=%d mapped=%d state=%d pressed=%d released=%d held=%d\n",
			   player, orig_key, key, state, (int)*pressed, (int)*released,
			   (int)*held);
	}
	if(!printed_p1_wasd_held && player == 1 && *held
	   && (orig_key == 87 || orig_key == 83 || orig_key == 65 || orig_key == 68)) {
		printed_p1_wasd_held = true;
		printf("[input_native_key_status] WARNING player=1 sees WASD held (orig=%d mapped=%d)\n",
			   orig_key, key);
	}
#endif

	if(state == GLFW_PRESS)
		input_key_held[key] = true;

	if(state == GLFW_RELEASE)
		input_key_held[key] = false;
}

bool input_native_key_symbol(int key, int* symbol, int* symbol_help,
							 enum input_category* category, int* priority) {
	*category = INPUT_CAT_NONE;
	*symbol = 7;
	*symbol_help = 7;
	*priority = 1;
	return true;
}

bool input_native_key_any(int* key) {
	return false;
}

	void input_pointer_enable(bool enable) {
		glfwSetInputMode(window, GLFW_CURSOR,
						 enable ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

	if(!input_pointer_enabled && enable)
		glfwSetCursorPos(window, gfx_width() / 2, gfx_height() / 2);

	if(input_pointer_enabled && !enable)
		glfwGetCursorPos(window, &input_old_pointer_x, &input_old_pointer_y);

	input_pointer_enabled = enable;
}

bool input_pointer(float* x, float* y, float* angle, int player) {
	double x2, y2;
	glfwGetCursorPos(window, &x2, &y2);
	*x = x2;
	*y = y2;
	*angle = 0.0F;
	return input_pointer_enabled && x2 >= 0 && y2 >= 0 && x2 < gfx_width()
		&& y2 < gfx_height();
}

void input_native_joystick(float dt, float* dx, float* dy, int player) {
	if(!input_pointer_enabled) {
		double x2, y2;
		glfwGetCursorPos(window, &x2, &y2);
		*dx = (x2 - input_old_pointer_x) * 0.001F;
		*dy = -(y2 - input_old_pointer_y) * 0.001F;
		input_old_pointer_x = x2;
		input_old_pointer_y = y2;
	} else {
		*dx = 0.0F;
		*dy = 0.0F;
	}
}

int which_controller(int chan) {
	return 1; // todo: find out which controller type 
}

#endif

#include "../game/game_state.h"

#ifdef PLATFORM_WII

#include <wiiuse/wpad.h>
#include <ogc/pad.h>

#define MAX_CONTROLLERS 3

static struct {
	float dx, dy;
	float magnitude;
	bool available;
} joystick_input[MAX_CONTROLLERS * MAX_WIIMOTES]; // 1: 0-2,   2: 3-5,   3: 6-8,   4: 9-11

static bool js_emulated_btns_prev[8][3][4];
static bool js_emulated_btns_held[8][3][4];

void input_init() {
	WPAD_Init();
	PAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_ALL, gfx_width(), gfx_height());

	for (int p = 0; p < 8; p++) {
		for(int k = 0; k < 4; k++) {
			for(int j = 0; j < 3; j++) {
				js_emulated_btns_prev[p][j][k] = js_emulated_btns_held[p][j][k] = false;
			}
		}
	}
}

void input_poll() {
	WPAD_ScanPads();
	PAD_ScanPads();

	for (int i = 0; i < MAX_WIIMOTES; i++) {
		expansion_t e;
		WPAD_Expansion(i, &e);

		if(e.type == WPAD_EXP_NUNCHUK) {
			joystick_input[0 + MAX_CONTROLLERS*i].dx = sin(glm_rad(e.nunchuk.js.ang));
			joystick_input[0 + MAX_CONTROLLERS*i].dy = cos(glm_rad(e.nunchuk.js.ang));
			joystick_input[0 + MAX_CONTROLLERS*i].magnitude = e.nunchuk.js.mag;
			joystick_input[0 + MAX_CONTROLLERS*i].available = true;
		} else {
			joystick_input[0 + MAX_CONTROLLERS*i].available = false;
		}

		if(e.type == WPAD_EXP_CLASSIC) {
			joystick_input[1 + MAX_CONTROLLERS*i].dx = sin(glm_rad(e.classic.ljs.ang));
			joystick_input[1 + MAX_CONTROLLERS*i].dy = cos(glm_rad(e.classic.ljs.ang));
			joystick_input[1 + MAX_CONTROLLERS*i].magnitude = e.classic.ljs.mag;
			joystick_input[1 + MAX_CONTROLLERS*i].available = true;

			joystick_input[2 + MAX_CONTROLLERS*i].dx = sin(glm_rad(e.classic.rjs.ang));
			joystick_input[2 + MAX_CONTROLLERS*i].dy = cos(glm_rad(e.classic.rjs.ang));
			joystick_input[2 + MAX_CONTROLLERS*i].magnitude = e.classic.rjs.mag;
			joystick_input[2 + MAX_CONTROLLERS*i].available = true;
		} else {
			joystick_input[1 + MAX_CONTROLLERS*i].available = 
						joystick_input[2 + MAX_CONTROLLERS*i].available = false;
		}

		for(int j = 0; j < 3; j++) {
			for(int k = 0; k < 4; k++) {
				js_emulated_btns_prev[i][j][k] = js_emulated_btns_held[i][j][k];
				js_emulated_btns_held[i][j][k] = false;
			}

			if(joystick_input[j + MAX_CONTROLLERS*i].available) 
			{
				float x = joystick_input[j + MAX_CONTROLLERS*i].dx *
								 joystick_input[j + MAX_CONTROLLERS*i].magnitude;
				float y = joystick_input[j + MAX_CONTROLLERS*i].dy * 
								 joystick_input[j + MAX_CONTROLLERS*i].magnitude;

				if(x > 0.2F) {
					js_emulated_btns_held[i][j][3] = true;
				} else if(x < -0.2F) {
					js_emulated_btns_held[i][j][2] = true;
				}

				if(y > 0.2F) {
					js_emulated_btns_held[i][j][0] = true;
				} else if(y < -0.2F) {
					js_emulated_btns_held[i][j][1] = true;
				}
			}
		}
	}
}

static uint32_t input_wpad_translate(int key, int player) {
	switch(key) {
		case 0: return WPAD_BUTTON_UP;
		case 1: return WPAD_BUTTON_DOWN;
		case 2: return WPAD_BUTTON_LEFT;
		case 3: return WPAD_BUTTON_RIGHT;
		case 4: return WPAD_BUTTON_A;
		case 5: return WPAD_BUTTON_B;
		case 6: return WPAD_BUTTON_1;
		case 7: return WPAD_BUTTON_2;
		case 8: return WPAD_BUTTON_PLUS;
		case 9: return WPAD_BUTTON_MINUS;
		case 10:return WPAD_BUTTON_HOME;
		default: break;
	}

	expansion_t e;
	WPAD_Expansion(player, &e);

	if(e.type == WPAD_EXP_NUNCHUK) {
		switch(key) {
			case 100: return WPAD_NUNCHUK_BUTTON_Z;
			case 101: return WPAD_NUNCHUK_BUTTON_C;
			default: break;
		}
	} else if(e.type == WPAD_EXP_CLASSIC) {
		switch(key) {
			case 200: return WPAD_CLASSIC_BUTTON_UP;
			case 201: return WPAD_CLASSIC_BUTTON_DOWN;
			case 202: return WPAD_CLASSIC_BUTTON_LEFT;
			case 203: return WPAD_CLASSIC_BUTTON_RIGHT;
			case 204: return WPAD_CLASSIC_BUTTON_A;
			case 205: return WPAD_CLASSIC_BUTTON_B;
			case 206: return WPAD_CLASSIC_BUTTON_X;
			case 207: return WPAD_CLASSIC_BUTTON_Y;
			case 208: return WPAD_CLASSIC_BUTTON_ZL;
			case 209: return WPAD_CLASSIC_BUTTON_ZR;
			case 210: return WPAD_CLASSIC_BUTTON_FULL_L;
			case 211: return WPAD_CLASSIC_BUTTON_FULL_R;
			case 212: return WPAD_CLASSIC_BUTTON_PLUS;
			case 213: return WPAD_CLASSIC_BUTTON_MINUS;
			case 214: return WPAD_CLASSIC_BUTTON_HOME;
			default: break;
		}
	} else if(e.type == WPAD_EXP_GUITARHERO3) {
		switch(key) {
			case 300: return WPAD_GUITAR_HERO_3_BUTTON_YELLOW;
			case 301: return WPAD_GUITAR_HERO_3_BUTTON_GREEN;
			case 302: return WPAD_GUITAR_HERO_3_BUTTON_BLUE;
			case 303: return WPAD_GUITAR_HERO_3_BUTTON_RED;
			case 304: return WPAD_GUITAR_HERO_3_BUTTON_ORANGE;
			case 305: return WPAD_GUITAR_HERO_3_BUTTON_PLUS;
			case 306: return WPAD_GUITAR_HERO_3_BUTTON_MINUS;
			case 307: return WPAD_GUITAR_HERO_3_BUTTON_STRUM_UP;
			case 308: return WPAD_GUITAR_HERO_3_BUTTON_STRUM_DOWN;
			default: break;
		}
	}

	return 0;
}

void input_native_key_status(int key, int player, bool* pressed, bool* released,
							 bool* held) {
	if(key >= 900 && key < 924) {
		int js = (key - 900) / 10;
		int offset = (key - 900) % 10;
		if(offset < 4) {
			*held =      js_emulated_btns_held[player][js][offset]
				      && js_emulated_btns_prev[player][js][offset];
		    *pressed =   js_emulated_btns_held[player][js][offset]
			         && !js_emulated_btns_prev[player][js][offset];
			*released = !js_emulated_btns_held[player][js][offset]
			    	  && js_emulated_btns_prev[player][js][offset];
			return;
		}
	}

	*pressed = WPAD_ButtonsDown(player) & input_wpad_translate(key, player);
	*released = WPAD_ButtonsUp(player) & input_wpad_translate(key, player);
	*held = !(*pressed) && !(*released)
		&& WPAD_ButtonsHeld(player) & input_wpad_translate(key, player);
}

bool input_native_key_symbol(int key, int* symbol, int* symbol_help,
							 enum input_category* category, int* priority) {
	if(key >= 900 && key < 904) {
		*symbol = *symbol_help = 17;
		*category = INPUT_CAT_NUNCHUK;
		*priority = 1;
		return true;
	}

	if(key >= 910 && key < 914) {
		*symbol = *symbol_help = 18;
		*category = INPUT_CAT_CLASSIC_CONTROLLER;
		*priority = 1;
		return true;
	}

	if(key >= 920 && key < 924) {
		*symbol = *symbol_help = 19;
		*category = INPUT_CAT_CLASSIC_CONTROLLER;
		*priority = 1;
		return true;
	}

	if(key < 0 || key > 308)
		return false;

	int symbols[] = {
		[0] = 25,	[1] = 26,	[2] = 27,	[3] = 28,	[4] = 0,	[5] = 1,
		[6] = 2,	[7] = 3,	[8] = 5,	[9] = 6,	[10] = 4,	[100] = 8,
		[101] = 9,	[200] = 25, [201] = 26, [202] = 27, [203] = 28, [204] = 10,
		[205] = 11, [206] = 12, [207] = 13, [208] = 14, [209] = 15, [210] = 22,
		[211] = 23, [212] = 5,	[213] = 6,	[214] = 4,	[300] = 7,	[301] = 7,
		[302] = 7,	[303] = 7,	[304] = 7,	[305] = 5,	[306] = 6,	[307] = 7,
		[308] = 7,
	};

	*category = INPUT_CAT_NONE;

	if(key >= 0 && key <= 10)
		*category = INPUT_CAT_WIIMOTE;

	if(key >= 100 && key <= 101)
		*category = INPUT_CAT_NUNCHUK;

	if(key >= 200 && key <= 214)
		*category = INPUT_CAT_CLASSIC_CONTROLLER;

	*symbol = symbols[key];
	*symbol_help = symbols[key];

	if(*symbol_help >= 25 && *symbol_help <= 28)
		*symbol_help = 24;

	expansion_t e;
	WPAD_Expansion(WPAD_CHAN_0, &e);

	if((*category == INPUT_CAT_NUNCHUK && e.type == WPAD_EXP_NUNCHUK)
	   || (*category == INPUT_CAT_CLASSIC_CONTROLLER
		   && e.type == WPAD_EXP_CLASSIC)) {
		*priority = 2;
	} else {
		*priority = 1;
	}

	return true;
}

bool input_native_key_any(int* key) {
	return false;
}

void input_pointer_enable(bool enable) { }

bool input_pointer(float* x, float* y, float* angle, int player) {
	int vplayer = gstate.player_sequence[player];
	if (vplayer == -1)
	 return false;

	struct ir_t ir;
	WPAD_IR(vplayer, &ir);
	*x = ir.x;
	*y = ir.y;
	*angle = ir.angle;
	return ir.valid;
}

void input_native_joystick(float dt, float* dx, float* dy, int player) {
	if(joystick_input[0 + MAX_CONTROLLERS * player].available 
	&& joystick_input[0 + MAX_CONTROLLERS * player].magnitude > 0.1F) 
	{
		*dx = joystick_input[0 + MAX_CONTROLLERS * player].dx * 
			  joystick_input[0 + MAX_CONTROLLERS * player].magnitude * dt;
		*dy = joystick_input[0 + MAX_CONTROLLERS * player].dy * 
			  joystick_input[0 + MAX_CONTROLLERS * player].magnitude * dt;

	} else if(joystick_input[2 + MAX_CONTROLLERS * player].available
		   && joystick_input[2 + MAX_CONTROLLERS * player].magnitude > 0.1F) 
	{
		*dx = joystick_input[2 + MAX_CONTROLLERS * player].dx * 
			  joystick_input[2 + MAX_CONTROLLERS * player].magnitude * dt;
		*dy = joystick_input[2 + MAX_CONTROLLERS * player].dy * 
			  joystick_input[2 + MAX_CONTROLLERS * player].magnitude * dt;
	} else {
		*dx = 0.0F;
		*dy = 0.0F;
	}
}

int which_controller(int chan) {
    if (joystick_input[0 + chan * MAX_CONTROLLERS].available) {
        return 0; // Nunchuk

    } else if (joystick_input[1 + chan * MAX_CONTROLLERS].available ||
               joystick_input[2 + chan * MAX_CONTROLLERS].available) {
        return 1; // Classic Controller

    } else {
        return -1;
	}
}

#endif

static const char* input_config_translate(enum input_button key) {
	switch(key) {
		case IB_ACTION1: return "input.item_action_left";
		case IB_ACTION2: return "input.item_action_right";
		case IB_FORWARD: return "input.player_forward";
		case IB_BACKWARD: return "input.player_backward";
		case IB_LEFT: return "input.player_left";
		case IB_RIGHT: return "input.player_right";
		case IB_JUMP: return "input.player_jump";
		case IB_SNEAK: return "input.player_sneak";
		case IB_INVENTORY: return "input.inventory";
		case IB_HOME: return "input.open_menu";
		case IB_SCROLL_LEFT: return "input.scroll_left";
		case IB_SCROLL_RIGHT: return "input.scroll_right";
		case IB_GUI_UP: return "input.gui_up";
		case IB_GUI_DOWN: return "input.gui_down";
		case IB_GUI_LEFT: return "input.gui_left";
		case IB_GUI_RIGHT: return "input.gui_right";
		case IB_GUI_CLICK: return "input.gui_click";
		case IB_GUI_CLICK_ALT: return "input.gui_click_alt";
		case IB_SCREENSHOT: return "input.screenshot";
		case IB_BACK: return "input.gui_back";
		case IB_ANY: return "input.any_button";
		default: return NULL;
	}
}

static inline const char* input_config_suffix(const char* key) {
	if(!key)
		return NULL;
	// keys come from input_config_translate() and use "input.<name>".
	return (strncmp(key, "input.", 6) == 0) ? (key + 6) : key;
}

static bool input_read_mapping(enum input_button b, int player, int* mapping,
							   size_t* length) {
	const char* base = input_config_translate(b);
	if(!base)
		return false;

#ifdef PLATFORM_PC
#ifdef SPLITSCREEN
	// Optional per-player overrides: input.p1.player_forward, input.p2..., etc.
	if(player > 0) {
		char keybuf[96];
		const char* suffix = input_config_suffix(base);
		if(suffix) {
			snprintf(keybuf, sizeof(keybuf), "input.p%d.%s", player, suffix);
			if(config_read_int_array(&gstate.config_user, keybuf, mapping,
									 length))
				return true;
		}
	}
#endif
#endif

	// On Wii this intentionally stays at the original single-player mapping
	// behavior from the old input.c implementation.
	return config_read_int_array(&gstate.config_user, base, mapping, length);
}

bool input_symbol(enum input_button b, int* symbol, int* symbol_help,
				  enum input_category* category, int player) {
	const char* key = input_config_translate(b);

	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];

	if(!input_read_mapping(b, player, mapping, &length))
		return false;

	int priority = 0;
	bool has_any = false;

	for(size_t k = 0; k < length; k++) {
		int symbol_tmp, symbol_help_tmp, priority_tmp;
		enum input_category category_tmp;
		if(input_native_key_symbol(mapping[k], &symbol_tmp, &symbol_help_tmp,
								   &category_tmp, &priority_tmp)
		   && priority_tmp > priority) {
			priority = priority_tmp;
			*symbol = symbol_tmp;
			*symbol_help = symbol_help_tmp;
			*category = category_tmp;
			has_any = true;
		}
	}

	return has_any;
}

bool input_pressed(enum input_button b, int player) {
	int vplayer = gstate.player_sequence[player];
	if(vplayer == -1) {
	#ifdef PLATFORM_PC
		// On PC the keyboard is shared; treat all players as available.
		// Still pass `player` through so the callsite can differentiate later.
		vplayer = player;
#else
		return false;
#endif
	}

	const char* key = input_config_translate(b);

	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];

	if(!input_read_mapping(b, player, mapping, &length))
		return false;

	bool any_pressed = false;
	bool any_held = false;
	bool any_released = false;

	for(size_t k = 0; k < length; k++) {
		bool pressed, released, held;
		input_native_key_status(mapping[k], vplayer, &pressed, &released, &held);
		if(pressed)
			any_pressed = true;
		if(released)
			any_released = true;
		if(held)
			any_held = true;
	}

	return any_pressed && !any_held && !any_released;
}

bool input_released(enum input_button b,int player) {
	int vplayer = gstate.player_sequence[player];
	if(vplayer == -1) {
	#ifdef PLATFORM_PC
		vplayer = player;
#else
		return false;
#endif
	}

	const char* key = input_config_translate(b);

	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];

	if(!input_read_mapping(b, player, mapping, &length))
		return false;

	bool any_pressed = false;
	bool any_held = false;
	bool any_released = false;

	for(size_t k = 0; k < length; k++) {
		bool pressed, released, held;
		input_native_key_status(mapping[k], vplayer, &pressed, &released, &held);
		if(pressed)
			any_pressed = true;
		if(released)
			any_released = true;
		if(held)
			any_held = true;
	}

	return !any_pressed && !any_held && any_released;
	}

bool input_held(enum input_button b, int player) {
	int vplayer = gstate.player_sequence[player];
	if(vplayer == -1) {
	#ifdef PLATFORM_PC
		vplayer = player;
#else
		return false;
#endif
	}
	
	const char* key = input_config_translate(b);

	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];

	if(!input_read_mapping(b, player, mapping, &length))
		return false;

	bool any_pressed = false;
	bool any_held = false;

	for(size_t k = 0; k < length; k++) {
		bool pressed, released, held;
		input_native_key_status(mapping[k], vplayer, &pressed, &released, &held);
		if(pressed)
			any_pressed = true;
		if(held)
			any_held = true;
	}

#ifdef PLATFORM_PC
	if(player == 1 && (b == IB_FORWARD || b == IB_BACKWARD || b == IB_LEFT
					   || b == IB_RIGHT || b == IB_JUMP)) {
		if(any_pressed || any_held) {
			printf("[input_held] player=%d vplayer=%d button=%d key=%s pressed=%d held=%d\n",
				   player, vplayer, (int)b, key ? key : "(null)", (int)any_pressed,
				   (int)any_held);
		}
	}
#endif

	return any_pressed || any_held;
}

bool input_joystick(float dt, float* x, float* y, int player) {
#ifdef TEST_MULTIPLAYER_INPUT
#ifdef PLATFORM_PC
	static bool printed_joy_p0 = false;
	static bool printed_joy_p1 = false;
	// Player 0: mouse look (existing behavior).
	// Player 1: arrow keys emulate joystick look.
	if(player == 0) {
		if(!printed_joy_p0) {
			printed_joy_p0 = true;
			printf("[input_joystick] player=0 uses mouse\n");
		}
		input_native_joystick(dt, x, y, player);
		return true;
	}
	if(player == 1) {
		if(!printed_joy_p1) {
			printed_joy_p1 = true;
			printf("[input_joystick] player=1 uses arrow keys\n");
		}
		bool pressed, released, held;

		// 262..265 are the GLFW arrow key codes, matching config.json.
		input_native_key_status(263, player, &pressed, &released, &held); // Left
		int left = held ? 1 : 0;
		input_native_key_status(262, player, &pressed, &released, &held); // Right
		int right = held ? 1 : 0;
		input_native_key_status(265, player, &pressed, &released, &held); // Up
		int up = held ? 1 : 0;
		input_native_key_status(264, player, &pressed, &released, &held); // Down
		int down = held ? 1 : 0;

		int xdir = right - left;
		int ydir = down - up;

		const float speed = 1.5f; // tuned to feel similar to mouse deltas
		*x = (float)xdir * speed * dt;
		*y = (float)ydir * speed * dt;
		return true;
	}
	return false;
#endif
#endif

	int vplayer = gstate.player_sequence[player];
	if(vplayer == -1) {
#ifdef PLATFORM_PC
		vplayer = player;
#else
		return false;
#endif
	}

#ifdef PLATFORM_PC
#ifdef SPLITSCREEN
	// Only one mouse exists; give additional players keyboard-look so they can
	// be controlled independently without extra devices.
	if(player > 0 && gstate.num_players > 1) {
		bool pressed, released, held;
		input_native_key_status(263, player, &pressed, &released, &held); // Left
		int left = held ? 1 : 0;
		input_native_key_status(262, player, &pressed, &released, &held); // Right
		int right = held ? 1 : 0;
		input_native_key_status(265, player, &pressed, &released, &held); // Up
		int up = held ? 1 : 0;
		input_native_key_status(264, player, &pressed, &released, &held); // Down
		int down = held ? 1 : 0;

		int xdir = right - left;
		int ydir = down - up;
		const float speed = 1.5f;
		*x = (float)xdir * speed * dt;
		*y = (float)ydir * speed * dt;
		return true;
	}
#endif
#endif

	input_native_joystick(dt, x, y, vplayer);
	return true;
}



// real channels
bool rinput_pressed(enum input_button b, int player) {
#ifdef PLATFORM_PC
	// `rinput_*` are intended to operate on the "real channel" (`player` is not
	// translated through `gstate.player_sequence`).
	//
	// On PC this is also used for controller assignment screens: IB_ANY should
	// mean "any mapped key/button for that channel", not a single configured key.

	// Special-case: "any key/button" should behave as a union across all mapped
	// inputs for this channel (so it works even when config has no sensible
	// `input.any_button` mapping).
	if(b == IB_ANY) {
		bool any_pressed = false;
		bool any_held = false;
		bool any_released = false;

		for(int bb = 0; bb <= (int)IB_ANY; bb++) {
			enum input_button b2 = (enum input_button)bb;
			if(b2 == IB_ANY)
				continue;

			size_t length = 8;
			int mapping[length];
			if(!input_read_mapping(b2, player, mapping, &length))
				continue;

			for(size_t k = 0; k < length; k++) {
				bool pressed, released, held;
				input_native_key_status(mapping[k], player, &pressed, &released,
										&held);
				if(pressed)
					any_pressed = true;
				if(released)
					any_released = true;
				if(held)
					any_held = true;
			}
		}

		return any_pressed && !any_held && !any_released;
	}
#endif

	size_t length = 8;
	int mapping[length];
	if(!input_read_mapping(b, player, mapping, &length))
		return false;

	bool any_pressed = false;
	bool any_held = false;
	bool any_released = false;

	for(size_t k = 0; k < length; k++) {
		bool pressed, released, held;
		input_native_key_status(mapping[k], player, &pressed, &released, &held);
		if(pressed)
			any_pressed = true;
		if(released)
			any_released = true;
		if(held)
			any_held = true;
	}

	return any_pressed && !any_held && !any_released;
}
