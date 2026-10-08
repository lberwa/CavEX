/*
	Copyright (c) 2022-2026 ByteBit/xtreme8000, lberwa

	This file is part of CavEX.

	Portions derived from the Homebrew Channel (fail0verflow/hbc),
	Copyright (C) 2008-2009 Team Twiizers / Hector Martin "marcan" et al.,
	originally licensed under the GNU General Public License version 2
	or (at your option) any later version. Relicensed here under GPLv3
	as permitted by the "or later" clause. See the NOTICE file.

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

#ifndef BOOT_H
#define BOOT_H

#include <stdbool.h>

bool boot_dol(const char *path, const char *args);

#endif