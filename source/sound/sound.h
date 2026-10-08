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

#ifndef SOUND_H
#define SOUND_H

enum mp3_sound {
    mp3_bg1,
    mp3_bg2,
    mp3_bg3,
    mp3_bg4,
    mp3_bg5,
    mp3_bg6,
    mp3_bg7,
    mp3_bg8,
    mp3_bg9,
    mp3_bg10
};

enum pcm_sound {
    pcm_click,
    pcm_chest_close,
    pcm_chest_open,
    pcm_door_close,
    pcm_door_open,
    pcm_drink,
    pcm_eat1,
    pcm_eat2,
    pcm_eat3,
    pcm_fuse,
    pcm_enderman_portal,
    pcm_sheep_say2,
    pcm_villager_idle2,
    pcm_zombie_say3,
    pcm_dig_sand1,
    pcm_dig_stone3,
    pcm_dig_wood2,
    pcm_mob_hit2,
    pcm_cave1,
    // tile
    pcm_piston_out,
    pcm_piston_in,
    // note
    pcm_note_harp,
    pcm_note_pling,
    pcm_note_bass,
    // damage
    pcm_mob_hit1,
    pcm_mob_hit3,
    pcm_fall_big,
    pcm_fall_small,
    // dig
    pcm_dig_grass1,
    pcm_dig_grass2,
    pcm_dig_grass3,
    pcm_dig_grass4,
    pcm_dig_cloth1,
    pcm_dig_cloth2,
    pcm_dig_cloth3,
    pcm_dig_cloth4,
    pcm_dig_gravel1,
    pcm_dig_gravel2,
    pcm_dig_gravel3,
    pcm_dig_gravel4,
    pcm_dig_wood1,
    pcm_dig_wood3,
    pcm_dig_wood4,
    pcm_dig_stone1,
    pcm_dig_stone2,
    pcm_dig_stone4,
    // step
    pcm_step_grass1,
    pcm_step_grass2,
    pcm_step_grass3,
    pcm_step_grass4,
    pcm_step_cloth1,
    pcm_step_cloth2,
    pcm_step_cloth3,
    pcm_step_cloth4,
    pcm_step_gravel1,
    pcm_step_gravel2,
    pcm_step_gravel3,
    pcm_step_gravel4,
    pcm_step_sand1,
    pcm_step_sand2,
    pcm_step_sand3,
    pcm_step_sand4,
    pcm_step_stone1,
    pcm_step_stone2,
    pcm_step_stone3,
    pcm_step_stone4,
    pcm_step_snow1,
    pcm_step_snow2,
    pcm_step_snow3,
    pcm_step_snow4,
    pcm_step_wood1,
    pcm_step_wood2,
    pcm_step_wood3,
    pcm_step_wood4,
    // fire
    pcm_fire,
    // mob
    pcm_sheep_say1,
    pcm_sheep_say3,
    pcm_creeper_death,
    // portal
    pcm_portal,
    pcm_portal_trigger,
    pcm_portal_travel,
    // cave
    pcm_cave2,
    pcm_cave3,
    pcm_cave4,
    pcm_cave5,
    pcm_cave6,
    pcm_cave7,
    pcm_cave8,
    pcm_cave9,
    pcm_cave10,
    pcm_cave11,
    pcm_cave12,
    pcm_cave13,
    // weather
    pcm_rain1,
    pcm_rain2,
    pcm_rain3,
    pcm_rain4,
    pcm_thunder1,
    pcm_thunder2,
    pcm_thunder3,
    // liquid
    pcm_splash,
    pcm_splash2,
    pcm_water,
    // random
    pcm_explode1,
    pcm_explode2,
    pcm_explode3,
    pcm_explode4,
    pcm_bowhit1,
    pcm_bowhit2,
    pcm_bowhit3,
    pcm_bowhit4,
    pcm_pop,
    pcm_levelup,
    pcm_glass1,
    pcm_glass2,
    pcm_glass3,
    pcm_successful_hit,
    pcm_classic_hurt,
    pcm_wood_click,
    pcm_fizz,
    pcm_orb
};

void sound_init(void);
bool sound_play_bg(enum mp3_sound sound[16]);
bool sound_play(enum pcm_sound sound);
bool sound_play_at(enum pcm_sound sound, float wx, float wy, float wz);
bool sound_play_at_vol(enum pcm_sound sound, float wx, float wy, float wz, float vol_scale);
void sound_stop_bg(void);
void sound_resume_bg(void);
void sound_set_volume_bg(float volume);
void sound_update(void);

#endif
