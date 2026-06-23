/*
	Copyright (c) 2026
*/

#include "../entity/entity.h"
#include "../network/server_local.h"
#include "blocks.h"

static enum block_material getMaterial(struct block_info* this) {
	(void)this;
	return MATERIAL_ORGANIC;
}

static bool tripwire_attached(const struct block_data* blk) {
	return (blk->metadata & 0x04) != 0;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x) {
		if(tripwire_attached(this->block)) {
			aabb_setsize(x, 1.0F, 0.09375F, 1.0F);
			aabb_translate(x, 0.0F, 0.0625F, 0.0F);
		} else {
			aabb_setsize(x, 1.0F, 0.5F, 1.0F);
		}
	}
	return entity ? 0 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	(void)this;
	(void)side;
	(void)it;
	return face_occlusion_empty();
}

static uint16_t getTextureIndex(struct block_info* this, enum side side) {
	(void)this;
	(void)side;
	return tex_atlas_lookup(TEXAT_TRIPWIRE_CROSS);
}

static size_t renderBlock(struct displaylist* d, struct block_info* this,
						  enum side side, struct block_info* it,
						  uint8_t* vertex_light, bool count_only) {
	(void)d;
	(void)this;
	(void)side;
	(void)it;
	(void)vertex_light;
	(void)count_only;
	return 0;
}

static bool tripwire_hitbox(struct block_info* this, struct AABB* aabb) {
	if(!aabb)
		return true;

	if(tripwire_attached(this->block)) {
		aabb_setsize(aabb, 1.0F, 0.09375F, 1.0F);
		aabb_translate(aabb, this->x, this->y + 0.0625F, this->z);
	} else {
		aabb_setsize(aabb, 1.0F, 0.5F, 1.0F);
		aabb_translate(aabb, this->x, this->y, this->z);
	}
	return true;
}

static bool tripwire_triggered_by_players(struct server_local* s,
										  struct AABB* hitbox) {
	const float eye_height = 1.62F;

	for(int i = 0; i < MAX_SERVER_PLAYERS; i++) {
		struct server_player* p = &s->players[i];
		struct AABB player_box;
		if(!p->has_pos)
			continue;
		aabb_setsize_centered_offset(&player_box, 0.6F, 1.8F, 0.6F,
									 (float)p->x, (float)p->y - eye_height + 0.9F,
									 (float)p->z);
		if(aabb_intersection(hitbox, &player_box))
			return true;
	}
	return false;
}

static bool tripwire_triggered_by_entities(struct server_local* s,
										   struct AABB* hitbox) {
	dict_entity_it_t it;
	dict_entity_it(it, s->entities);

	while(!dict_entity_end_p(it)) {
		struct entity* e = dict_entity_ref(it)->value;
		struct AABB entity_box;
		dict_entity_next(it);

		if(!e || e->type == ENTITY_LOCAL_PLAYER || !e->getBoundingBox)
			continue;
		if(e->getBoundingBox(e, &entity_box) == 0)
			continue;
		if(aabb_intersection(hitbox, &entity_box))
			return true;
	}

	return false;
}

static void onWorldTick(struct server_local* s, struct block_info* info) {
	struct AABB hitbox;
	struct block_data cur = *info->block;
	bool powered;

	tripwire_hitbox(info, &hitbox);
	powered = tripwire_triggered_by_players(s, &hitbox)
			  || tripwire_triggered_by_entities(s, &hitbox);

	if((cur.metadata & 0x01) == (powered ? 0x01 : 0x00))
		return;

	cur.metadata = (cur.metadata & ~0x01) | (powered ? 0x01 : 0x00);
	server_world_set_block(s, info->x, info->y, info->z, cur);
	notifyNeighbours(s, info->x, info->y, info->z);
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g, struct server_local* s) {
	(void)this;
	(void)g;
	(void)s;
	if(it) {
		it->id = ITEM_STRING;
		it->durability = 0;
		it->count = 1;
	}
	return 1;
}

struct block block_tripwire = {
	.name = "Tripwire",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.onWorldTick = onWorldTick,
	.transparent = false,
	.renderBlock = renderBlock,
	.renderBlockAlways = render_block_tripwire,
	.luminance = 0,
	.double_sided = true,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 0,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = block_place_default,
		.fuel = 0,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
