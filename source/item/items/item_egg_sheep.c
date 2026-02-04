/*
    Sheep spawn egg item
*/

#include "../../network/server_local.h"
#include "../../network/client_interface.h"
#include "../../network/server_interface.h"
#include "../../entity/entity_monster.h"


static bool onItemPlace(struct server_local* s, struct item_data* it,
                        struct block_info* where, struct block_info* on,
                        enum side on_side) {
    /* spawn sheep at the clicked location */
    server_local_spawn_monster((vec3) {where->x, where->y, where->z}, ANIMAIL_SHEEP, s);
    return false; /* spawn eggs have infinite uses */
}

struct item item_egg_sheep = {
    .name = "Sheep Egg",
    .has_damage = false,
    .max_stack = 64,
    .fuel = 0,
    .renderItem = render_item_flat,
    .onItemPlace = onItemPlace,
    .armor.is_armor = false,
    .tool.type = TOOL_TYPE_ANY,
    .render_data = {
        .item = {
            .texture_x = 10,
            .texture_y = 9,
        },
    },
};