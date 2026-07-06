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
	Nachladbares Overlay-Modul ("Erweiterung").

	Anders als boot_dol()/loader_exec() (die alles herunterfahren und NICHT
	zurueckkehren) laedt dieses System ein selbst gebautes Modul an eine feste
	kleine Code-Basis im MEM2, ruft es wie eine Funktion auf und gibt danach
	den GESAMTEN von ihm belegten Speicher wieder frei.

	Ablauf:
	    ext_load("sd:/apps/cavex/ext.dol");  // laedt Code, reserviert freien RAM
	    int r = ext_run(game_state_ptr);      // ruft ext_main(api) auf
	    ext_unload();                          // gibt ALLES frei -> RAM zurueck

	Der bestehende Chainloader (boot.h) bleibt davon voellig unberuehrt und
	weiterhin nutzbar.
*/

#ifndef EXTENSION_H
#define EXTENSION_H

#include <stdbool.h>

#include "extension_api.h"

typedef int (*ext_entry_fn)(const ext_api_t *api);

/* Laedt das Modul (DOL-Format) an die feste Code-Basis, nullt dessen BSS und
   reserviert den freien MEM2 oberhalb des Codes als Arena. Kein crt0, kein
   Reset. Gibt false zurueck bei Fehler (Datei/Adresse/Sektionen).           */
bool ext_load(const char *path);

/* Ruft ext_main(api) im geladenen Modul auf. 'user' wird als api->user
   durchgereicht (z.B. ein game_state*). Rueckgabe = Rueckgabe des Moduls,
   oder -1 wenn nichts geladen ist.                                          */
int ext_run(void *user);

/* Gibt Code + alle Modul-Variablen in einem Rutsch frei (Arena2Hi zurueck).
   Danach ist der RAM wieder komplett verfuegbar.                            */
void ext_unload(void);

/* true, solange ein Modul geladen ist. */
bool ext_is_loaded(void);

/* letzte via api->log() gesendete Nachricht des Moduls (leerer String, wenn
   noch keine kam). Bleibt auch nach ext_unload() erhalten -- fuer Anzeige. */
const char *ext_last_message(void);

#endif
