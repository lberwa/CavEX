/*
	Copyright (c) 2022-2026 ByteBit/xtreme8000, lberwa

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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <ogc/system.h>
#include <ogc/cache.h>
#include <ogc/machine/processor.h>

#include "extension.h"
#include "extension_api.h"
#include "../platform/gfx.h"
#include "../network/server_comunication.h" /* debug_send (No-op ohne NET_DEBUG) */

/*
	Feste Adresse im MEM2, an die der Modulcode gelinkt wird. MUSS mit der
	ORIGIN im extension.ld des Moduls uebereinstimmen.

	Merke: der eigentliche Code (~8,6 MB) liegt fest hier; ALLE dynamischen
	Variablen des Moduls kommen darueber aus der Arena und koennen den ganzen
	restlichen freien RAM nutzen. Falls das Modul beim Laden ueber "outside
	free MEM2" meckert, den geloggten Bereich [Arena2Lo,Arena2Hi) ansehen und
	EXT_CODE_BASE (hier UND in extension.ld) passend setzen.
*/
#ifndef EXT_CODE_BASE
#define EXT_CODE_BASE 0x91000000u
#endif

#define EXT_ALIGN 32u

/* DOL-Header (identisch zu loader_reloc.c) */
typedef struct {
	u32 text_pos[7];
	u32 data_pos[11];
	u32 text_start[7];
	u32 data_start[11];
	u32 text_size[7];
	u32 data_size[11];
	u32 bss_start;
	u32 bss_size;
	u32 entry_point;
} dolheader;

static struct {
	bool loaded;
	void *old_arena2hi; /* zum Wiederherstellen bei ext_unload() */
	u32 arena_ptr;		/* Bump-Cursor                          */
	u32 arena_end;		/* obere Grenze der Arena               */
	ext_entry_fn entry;
} ext;

static u32 align_up(u32 v, u32 a) {
	return (v + (a - 1)) & ~(a - 1);
}

/* --- Arena-Allocator, den das Modul ueber die API bekommt --------------- */

static void *ext_arena_alloc(size_t size) {
	u32 p = align_up(ext.arena_ptr, EXT_ALIGN);
	if(!size || p + size > ext.arena_end || p + size < p)
		return NULL;
	ext.arena_ptr = p + size;
	return (void *)p;
}

static size_t ext_arena_avail(void) {
	u32 p = align_up(ext.arena_ptr, EXT_ALIGN);
	return (p >= ext.arena_end) ? 0 : (ext.arena_end - p);
}

/* letzte Log-Nachricht des Moduls -- ueberlebt bewusst ext_unload(), damit
   das Menue sie danach noch anzeigen kann. */
static char ext_last_msg[96] = "";

/* Host-seitiges Debug: printf + (falls NET_DEBUG) ueber das Netz an den PC.
   Laeuft immer im HOST-Kontext -- auch wenn es aus dem Overlay via api->log
   aufgerufen wird -> so kommt der Overlay-Log bis zum Freeze am PC an. */
static void ext_netdbg(const char *msg) {
	char line[192];
	snprintf(line, sizeof(line), "%s\n", msg ? msg : "(null)");
	printf("%s", line);
	debug_send(line);
}

static void ext_log_cb(const char *msg) {
	if(msg) {
		strncpy(ext_last_msg, msg, sizeof(ext_last_msg) - 1);
		ext_last_msg[sizeof(ext_last_msg) - 1] = '\0';
	}
	ext_netdbg(msg);
}

const char *ext_last_message(void) {
	return ext_last_msg;
}

/* --- Laden ------------------------------------------------------------- */

/* Liest sz Bytes ab Dateioffset pos direkt an die absolute Adresse dst. */
static bool read_section(int fd, u32 pos, void *dst, u32 sz) {
	if(lseek(fd, pos, SEEK_SET) != (off_t)pos)
		return false;
	return read(fd, dst, sz) == (int)sz;
}

bool ext_load(const char *path) {
	int fd;
	dolheader hdr;
	u32 lo, hi, max_end;
	int i;

	if(ext.loaded) {
		printf("[ext] already loaded\n");
		return false;
	}

	lo = (u32)SYS_GetArena2Lo();
	hi = (u32)SYS_GetArena2Hi();

	{
		char m[160];
		snprintf(m, sizeof(m),
				 "[ext] ext_load '%s' | freies MEM2 [0x%08x,0x%08x)=%u KB | "
				 "MEM1=%u KB MEM2=%u KB",
				 path ? path : "(null)", lo, hi, (hi - lo) / 1024,
				 (unsigned)SYS_GetArena1Size() / 1024,
				 (unsigned)SYS_GetArena2Size() / 1024);
		ext_netdbg(m);
	}

	if(EXT_CODE_BASE < lo || EXT_CODE_BASE >= hi) {
		printf("[ext] EXT_CODE_BASE 0x%08x outside free MEM2 -- adjust it\n",
			   EXT_CODE_BASE);
		return false;
	}

	fd = open(path, O_RDONLY);
	if(fd < 0) {
		printf("[ext] open failed: %s\n", path);
		return false;
	}

	if(read(fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
		printf("[ext] header read failed\n");
		close(fd);
		return false;
	}

	/* Alles oberhalb der Code-Basis reservieren, damit weder libogc noch der
	   Host-Heap den Modulcode/-speicher ueberschreiben. */
	ext.old_arena2hi = (void *)hi;
	SYS_SetArena2Hi((void *)EXT_CODE_BASE);

	max_end = EXT_CODE_BASE;

	/* Text-Sektionen direkt an ihre Ziel-Adressen laden. */
	for(i = 0; i < 7; i++) {
		u32 s = hdr.text_start[i], sz = hdr.text_size[i];
		if(!sz)
			continue;
		if(s < EXT_CODE_BASE || s + sz > hi || s + sz < s) {
			printf("[ext] text %d @0x%08x sz 0x%x outside region\n", i, s, sz);
			goto fail;
		}
		if(!read_section(fd, hdr.text_pos[i], (void *)s, sz)) {
			printf("[ext] text %d read failed\n", i);
			goto fail;
		}
		DCFlushRange((void *)s, sz);
		ICInvalidateRange((void *)s, sz);
		if(s + sz > max_end)
			max_end = s + sz;
	}

	/* Data-Sektionen. */
	for(i = 0; i < 11; i++) {
		u32 s = hdr.data_start[i], sz = hdr.data_size[i];
		if(!sz)
			continue;
		if(s < EXT_CODE_BASE || s + sz > hi || s + sz < s) {
			printf("[ext] data %d @0x%08x sz 0x%x outside region\n", i, s, sz);
			goto fail;
		}
		if(!read_section(fd, hdr.data_pos[i], (void *)s, sz)) {
			printf("[ext] data %d read failed\n", i);
			goto fail;
		}
		DCFlushRange((void *)s, sz);
		if(s + sz > max_end)
			max_end = s + sz;
	}

	close(fd);
	fd = -1;

	/* BSS nullen -- kein crt0 laeuft fuer das Modul, sonst haetten dessen
	   globale Variablen Datenmuell. */
	if(hdr.bss_size) {
		u32 s = hdr.bss_start, sz = hdr.bss_size;
		if(s < EXT_CODE_BASE || s + sz > hi || s + sz < s) {
			printf("[ext] bss @0x%08x sz 0x%x outside region\n", s, sz);
			goto fail;
		}
		memset((void *)s, 0, sz);
		DCFlushRange((void *)s, sz);
		if(s + sz > max_end)
			max_end = s + sz;
	}

	ext.entry = (ext_entry_fn)hdr.entry_point;
	ext.arena_ptr = align_up(max_end, EXT_ALIGN);
	ext.arena_end = hi; /* Arena spannt bis zum alten MEM2-Top */
	ext.loaded = true;

	printf("[ext] loaded '%s'\n", path);
	printf("[ext]   code  0x%08x .. 0x%08x (%u KB)\n", EXT_CODE_BASE, max_end,
		   (max_end - EXT_CODE_BASE) / 1024);
	printf("[ext]   arena 0x%08x .. 0x%08x (%u KB free)\n", ext.arena_ptr,
		   ext.arena_end, (ext.arena_end - ext.arena_ptr) / 1024);
	printf("[ext]   entry 0x%08x\n", (u32)ext.entry);
	return true;

fail:
	if(fd >= 0)
		close(fd);
	SYS_SetArena2Hi(ext.old_arena2hi); /* RAM sofort zurueckgeben */
	memset(&ext, 0, sizeof(ext));
	return false;
}

/* --- Ausfuehren -------------------------------------------------------- */

int ext_run(void *user) {
	ext_api_t api;

	if(!ext.loaded || !ext.entry) {
		printf("[ext] run: nothing loaded\n");
		return -1;
	}

	api.abi_version = EXT_ABI_VERSION;
	api.arena_alloc = ext_arena_alloc;
	api.arena_avail = ext_arena_avail;
	api.log = ext_log_cb;
	api.user = user;
	api.screenmode = gfx_wii_screenmode();
	api.back_buffer = gfx_wii_backbuffer();

	/* ganz normaler Funktionsaufruf ins geladene Modul -- kein Reset */
	ext_netdbg("[ext] ext_run: VOR entry() (Sprung ins Overlay)");
	int r = ext.entry(&api);
	ext_netdbg("[ext] ext_run: entry() zurueckgekehrt");
	return r;
}

/* --- Entladen ---------------------------------------------------------- */

void ext_unload(void) {
	if(!ext.loaded)
		return;

	/* Ein einziger Handgriff gibt Code + saemtliche Modul-Variablen frei. */
	SYS_SetArena2Hi(ext.old_arena2hi);
	printf("[ext] unloaded, MEM2 top restored to 0x%08x\n",
		   (u32)ext.old_arena2hi);
	memset(&ext, 0, sizeof(ext));
}

bool ext_is_loaded(void) {
	return ext.loaded;
}
