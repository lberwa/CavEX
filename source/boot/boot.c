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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ogc/system.h>
#include <ogc/machine/processor.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "boot.h"

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


static bool launch_external_dol(entry_point *ep, const char *path, const char *args) 
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

	ok = loader_reloc(ep, data, st.st_size, args, arg_len, true);
	blob_free(data);

	if (!ok)

	return ok;
}

bool boot_dol(const char *path, const char *args) {
    entry_point ep;
    bool ok;

    ok = launch_external_dol(&ep, path, args);
    if (!ok)
        return false;

    loader_exec(ep);

    return true;
}