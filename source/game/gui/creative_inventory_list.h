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

#ifndef CREATIVE_INVENTORY_LIST_H
#define CREATIVE_INVENTORY_LIST_H

#include <stddef.h>
#include <stdint.h>

// Pure enumeration of the entries shown in the creative inventory grid.
//
// The creative inventory is a paginated grid of every obtainable item. An item
// id is included iff items[id] is non-NULL (a real, registered item) AND that
// item exposes a renderer (items[id]->renderItem != NULL) so the cell draws a
// real texture rather than nothing, AND it is not on the small exclusion list
// of "placed forms" that are offered through a dedicated item instead (the raw
// bed and door blocks -- you get the Bed/Door items, which place the real
// multi-block structure). Ids are scanned in ascending order 1..ITEMS_MAX-1
// (id 0 is air/empty and is never offered).
//
// This spans the WHOLE item set: block items (ids 1..255) AND the dedicated
// items (ids >= 256: tools, armor, food, doors, bed, boat, minecart, ...), so
// the survival-style creative grid can offer everything, not just blocks.
//
// These functions are deliberately free of any GUI / GL / engine state so the
// enumeration logic can be unit-tested directly against the item registry (the
// GUI screen merely lays the resulting list out on a grid).

// Number of entries offered by the creative inventory, given the current
// `items[]` registry.
size_t creative_inventory_count(void);

// The item id at logical position `index` in the creative list (0-based, in
// ascending item-id order), or 0 if `index` is out of range. The returned id is
// always a valid index into `items[]` with a non-NULL, renderable entry.
uint16_t creative_inventory_item_id(size_t index);

#endif
