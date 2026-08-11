/*
	Copyright (c) 2022-2026 ByteBit/xtreme8000, lberwa

	This file is part of CavEX. (based on the original HBC code)

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

#ifndef _LOADER_RELOC_H_
#define _LOADER_RELOC_H_

#include <gctypes.h>

#define ARGS_MAX_LEN 1024
#define BASE_ADDR 0x81330000
#define LD_MIN_ADDR 0x80003400
#define LD_MAX_ADDR (BASE_ADDR - 1 - ARGS_MAX_LEN)
#define LD_MAX_SIZE (LD_MAX_ADDR - LD_MIN_ADDR)
#define LD_ARGS_ADDR (LD_MAX_ADDR + 1)


#define MAX_BLOBS 8
#define BLOB_MINSLACK (512*1024)

#define gprintf(...)

typedef void (*entry_point) (void);

bool loader_reloc (entry_point *ep, const u8 *addr, u32 size, const char *args,
				   u16 arg_len, bool check_overlap);

/* Faehrt alle libogc-Dienste herunter (SYS_ResetSystem + __exception_closeall).
   MUSS aufgerufen werden, SOLANGE die libogc-Datenstrukturen noch gueltig sind,
   d.h. VOR loader_reloc() (die In-Place-Relokation ueberschreibt das
   .data-Segment des laufenden Programms). */
void loader_shutdown_services (void);

/* arena2hi: der VOR loader_reloc() ausgelesene SYS_GetArena2Hi()-Wert. Nach der
   Relokation ist libogcs .data unbrauchbar, daher darf SYS_GetArena2Hi() hier
   nicht mehr aufgerufen werden. */
void loader_exec (entry_point ep, void *arena2hi);

#endif

