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

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#include "parson/parson.h"

struct config {
	JSON_Value* root;
};

bool config_create(struct config* c, const char* filename);
const char* config_read_string(struct config* c, const char* key,
							   const char* fallback);
bool config_read_int_array(struct config* c, const char* key, int* dest,
						   size_t* length);
void config_destroy(struct config* c);

#endif
