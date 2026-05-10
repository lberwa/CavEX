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
    pcm_cave1
};

void sound_init(void);
bool sound_play_bg(enum mp3_sound sound[16]);
bool sound_play(enum pcm_sound sound);
void sound_stop_bg(void);
void sound_set_volume_bg(float volume);
void sound_update(void);

#endif
