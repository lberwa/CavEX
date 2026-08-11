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

#include "boot.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef PLATFORM_WII
#include <ogc/system.h>
#include <ogc/machine/processor.h>

#include "panic.h"
#include "loader_reloc.h"

typedef struct {
	void *the_blob;
	size_t blob_size;
	void *old_arena2hi;
} blob_t;

static blob_t blobs[MAX_BLOBS];
static int num_blobs = 0;

// supports only stack-type allocs (free last alloced)
static void *blob_alloc(size_t size)
{
	u32 level;
	u32 addr;
	void *old_arena2hi;

	_CPU_ISR_Disable(level);
	if (num_blobs >= MAX_BLOBS) {
		_CPU_ISR_Restore(level);
		gprintf("too many blobs\n");
		panic();
	}

	old_arena2hi = SYS_GetArena2Hi();
	addr = (((u32)old_arena2hi) - size) & (~0x1f);

	if (addr < (BLOB_MINSLACK + (u32)SYS_GetArena2Lo())) {
		_CPU_ISR_Restore(level);
		return NULL;
	}

	blobs[num_blobs].old_arena2hi = old_arena2hi;
	blobs[num_blobs].the_blob = (void*)addr;
	blobs[num_blobs].blob_size = size;
	num_blobs++;

	SYS_SetArena2Hi((void*)addr);
	_CPU_ISR_Restore(level);
	gprintf("allocated blob size %d at 0x%08x\n", size, addr);
	return (void*)addr;
}

static void blob_free(void *p)
{
	u32 level;
	if (!p)
		return;

	_CPU_ISR_Disable(level);

	if (num_blobs == 0) {
		_CPU_ISR_Restore(level);
		gprintf("blob_free with no blobs\n");
		panic();
	}

	num_blobs--;
	if (p != blobs[num_blobs].the_blob) {
		_CPU_ISR_Restore(level);
		gprintf("mismatched blob_free (%p != %p)\n", p, blobs[num_blobs].the_blob);
		panic();
	}
	if (SYS_GetArena2Hi() != p) {
		_CPU_ISR_Restore(level);
		gprintf("someone else used MEM2 (%p != %p)\n", p, SYS_GetArena2Hi());
		panic();
	}

	SYS_SetArena2Hi(blobs[num_blobs].old_arena2hi);
	_CPU_ISR_Restore(level);
	gprintf("freed blob size %d at %p\n", blobs[num_blobs].blob_size, p);
}

extern int stat (const char *__restrict __file,
		 struct stat *__restrict __buf) __THROW __nonnull ((1, 2));


static bool launch_external_dol(entry_point *ep, void **arena2hi_out,
								const char *path, const char *args)
{
	struct stat st;
	int fd;
	u8 *data;
	u16 arg_len;
	bool ok;

	if (stat(path, &st) < 0 || st.st_size <= 0 || st.st_size > LD_MAX_SIZE) {
		return false;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		return false;
	}

	data = (u8 *) blob_alloc(st.st_size);
	if (!data) {
		close(fd);
		return false;
	}

	if (read(fd, data, st.st_size) != st.st_size) {
		close(fd);
		blob_free(data);
		return false;
	}

	close(fd);

	arg_len = 0;
	if (args)
		arg_len = strlen(args) + 1;

	/* Reihenfolge ist kritisch: die Dienste MUESSEN heruntergefahren werden,
	   solange die libogc-Datenstrukturen (u.a. die Reset-Funktions-Queue) noch
	   gueltig sind. loader_reloc() kopiert das neue Image IN-PLACE an seine
	   finalen Adressen und ueberschreibt dabei das .data-Segment des laufenden
	   Programms -- danach ist libogc unbrauchbar. arena2hi wird deshalb JETZT
	   ausgelesen und an loader_exec() weitergereicht.

	   Der Blob wird bewusst nicht mehr freigegeben: nach erfolgreicher
	   Relokation kehren wir nie zurueck, und blob_free() wuerde ohnehin auf
	   libogc (SYS_GetArena2Hi) zugreifen, dessen Zustand dann bereits zerstoert
	   ist. */
	*arena2hi_out = SYS_GetArena2Hi();

	loader_shutdown_services();

	ok = loader_reloc(ep, data, st.st_size, args, arg_len, true);
	if (!ok) {
		/* Fehlschlag erst nach dem Shutdown -> es gibt keinen Weg zurueck in
		   das (halb tote) Spiel. Deterministisch abbrechen statt undefiniert
		   weiterzulaufen. */
		gprintf("loader_reloc failed after service shutdown\n");
		panic();
	}

	return ok;
}

bool boot_dol(const char *path, const char *args) {
    entry_point ep;
    void *arena2hi;
    bool ok;

    ok = launch_external_dol(&ep, &arena2hi, path, args);
    if (!ok)
        return false;

    loader_exec(ep, arena2hi);

    return true;
}
#endif

#ifdef PLATFORM_PC

/* PC-Gegenstueck zu loader_exec() auf der Wii: statt eine externe DOL zu laden
   und dorthin zu vektorisieren, startet sich der Prozess komplett neu. execv()
   ersetzt das laufende Prozessabbild durch eine frische Instanz derselben
   Executable (/proc/self/exe) -- das entspricht dem Wii-Verhalten "alles
   herunterfahren und von vorne starten". path/args werden ignoriert (auf der
   Wii ist "boot.dol" ohnehin CavEX selbst).

   Kehrt nur zurueck, wenn execv() fehlschlaegt (dann false). */
bool boot_dol(const char *path, const char *args) {
	(void)path;
	(void)args;

	char exe[512];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len <= 0)
		return false;
	exe[len] = '\0';

	/* Wurde das Binary seit dem Start ersetzt (z.B. durch einen Rebuild),
	   haengt der Kernel " (deleted)" an /proc/self/exe -> execv() faende den
	   Pfad nicht (ENOENT). Suffix abschneiden, damit die frisch gebaute
	   Executable am echten Pfad gestartet wird. */
	const char *deleted = " (deleted)";
	size_t dlen = strlen(deleted);
	if ((size_t)len > dlen && strcmp(exe + len - dlen, deleted) == 0)
		exe[len - dlen] = '\0';

	char *const argv[] = {exe, NULL};
	execv(exe, argv);

	/* nur erreichbar, wenn execv() fehlgeschlagen ist */
	perror("boot_dol: execv");
	return false;
}

#endif