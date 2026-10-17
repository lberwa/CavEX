#ifndef SCREEN_INVENTORY_CREATIVE_H
#define SCREEN_INVENTORY_CREATIVE_H

#include <stddef.h>
#include "../../item/items.h"
#include "screen.h"

typedef enum {
	CREATIVE_TAB_CHEST  = 0,
	CREATIVE_TAB_ITEMS  = 1,
	CREATIVE_TAB_SEARCH = 2,
	CREATIVE_TAB_SAVED  = 3,
} creative_inv_tab;

creative_inv_tab creative_inventory_get_tab(void);
void creative_inventory_set_tab(creative_inv_tab tab);
void creative_inventory_set_saved_item(size_t slot, struct item_data item);

extern struct screen screen_inventory_creative;

#endif
