/*
	Copyright (c) 2026 ByteBit/xtreme8000

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

#include "creative_inventory_list.h"

#include "../../block/blocks.h"
#include "../../item/items.h"

// "Placed form" block ids that are offered through a dedicated item instead, so
// the grid never shows a duplicate that places a broken single block. The Bed
// and Door items (ids >= 256) place the real multi-block structure; their raw
// blocks (this list) are hidden from the grid.
static bool creative_inventory_excluded(uint16_t id) {
	return id == BLOCK_BED || id == BLOCK_DOOR_WOOD || id == BLOCK_DOOR_IRON;
}

// Curated "featured" ids surfaced on page 1, in build-priority order, so the
// items reached for to BUILD (bed, doors, carpet, minecart, ...) are not
// scattered through a raw-id-ordered grid where bed/doors/minecart land on the
// last page. These are the dedicated item ids (the raw bed/door blocks are
// hidden by creative_inventory_excluded, so no duplicate). Any id here that is
// not currently includable is simply skipped, so the list degrades gracefully.
//   171 carpet, 69 lever, 77 button, 324 wooden door, 330 iron door,
//   355 bed, 328 minecart, 333 boat
static const uint16_t FEATURED[] = {171, 69, 77, 324, 330, 355, 328, 333};
static const size_t FEATURED_COUNT = sizeof(FEATURED) / sizeof(FEATURED[0]);

// True if `id` is one of the curated featured ids (regardless of whether it is
// currently includable).
static bool creative_inventory_is_featured(uint16_t id) {
	for(size_t k = 0; k < FEATURED_COUNT; k++) {
		if(FEATURED[k] == id)
			return true;
	}
	return false;
}

// An item is offered in the creative inventory when it is registered AND it can
// be drawn as an item (so no cell renders as nothing/magenta) AND it is not a
// hidden placed-form. Keeping this in one predicate means count() and item_id()
// can never disagree.
static bool creative_inventory_includes(uint16_t id) {
	return id > 0 && id < ITEMS_MAX && items[id] && items[id]->renderItem
		&& !creative_inventory_excluded(id);
}

size_t creative_inventory_count(void) {
	size_t n = 0;
	for(int id = 1; id < ITEMS_MAX; id++) {
		if(creative_inventory_includes((uint16_t)id))
			n++;
	}
	return n;
}

uint16_t creative_inventory_item_id(size_t index) {
	// Page 1: the includable featured ids, in FEATURED order.
	size_t seen = 0;
	for(size_t k = 0; k < FEATURED_COUNT; k++) {
		if(creative_inventory_includes(FEATURED[k])) {
			if(seen == index)
				return FEATURED[k];
			seen++;
		}
	}

	// Tail: the remaining includable ids in ascending order, skipping any id
	// already emitted as featured (no duplicates). `seen` continues counting
	// from the featured block so logical positions stay contiguous.
	for(int id = 1; id < ITEMS_MAX; id++) {
		if(creative_inventory_includes((uint16_t)id)
		   && !creative_inventory_is_featured((uint16_t)id)) {
			if(seen == index)
				return (uint16_t)id;
			seen++;
		}
	}
	return 0;
}
