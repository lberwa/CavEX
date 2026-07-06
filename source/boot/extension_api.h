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

/*
	Geteilte ABI zwischen dem Host (CavEX) und einem nachladbaren
	Overlay-Modul. DIESE Datei muss in BEIDEN Projekten identisch sein.

	Ein Modul initialisiert NICHTS selbst (kein Video/GX/Input/FAT) -- es
	bekommt beim Aufruf einen Zeiger auf diese Struct und benutzt
	ausschliesslich die Dienste des Hosts. Alle dynamischen Daten holt es
	sich ueber arena_alloc() aus dem freien RAM oberhalb des Modulcodes.
	Beim Entladen wird dieser gesamte Bereich in einem Rutsch freigegeben
	-- als haette es das Modul nie gegeben.
*/

#ifndef EXTENSION_API_H
#define EXTENSION_API_H

#include <stddef.h>
#include <stdint.h>

#define EXT_ABI_VERSION 2u

typedef struct ext_api {
	uint32_t abi_version; /* == EXT_ABI_VERSION, vom Modul pruefen lassen  */

	/* Speicher aus dem freien RAM oberhalb des Modulcodes. Bump-Allocator:
	   es gibt KEIN Einzel-free(). Alles verschwindet erst bei ext_unload().
	   Gibt NULL zurueck, wenn der freie RAM erschoepft ist.               */
	void *(*arena_alloc)(size_t size);

	/* noch verfuegbare Bytes in der Arena                                 */
	size_t (*arena_avail)(void);

	/* einfaches Logging ueber den Host                                    */
	void (*log)(const char *msg);

	/* vom Host durchgereicht, z.B. ein struct game_state* -- so sieht das
	   Modul den laufenden Spielzustand, ohne etwas zu initialisieren.     */
	void *user;

	/* Vom Host bereitgestellter, bereits initialisierter Grafik-Kontext (Wii),
	   damit ein Overlay ohne Neu-Init zeichnen kann. NULL auf dem PC.       */
	void *screenmode;  /* GXRModeObj* des Hosts (-> init.py: mode_ptr)      */
	void *back_buffer; /* aktueller Back-Buffer  (-> init.py: fb_ptr)       */
} ext_api_t;

/*
	Der Einstiegspunkt, den das Modul exportieren MUSS. Im Linkerscript des
	Moduls per ENTRY(ext_main) als Entry-Point setzen. Wird vom Host wie eine
	ganz normale Funktion aufgerufen (kein crt0, kein Reset) und muss normal
	zurueckkehren:

	    int ext_main(const ext_api_t *api);
*/

#endif
