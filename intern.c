#include "minic.h"

typedef struct slot {
	u32 length;
	identifierId id;
	u8 *ptr;
	u64 hash;
} slot;

typedef struct ctx {
	slot *map;
	usize slot_count;
	u8 **identifier_contents;
	identifierId ident_id;
} ctx;

static void processToken(tokenBuffer *buf, u8 *contents, usize token, ctx *c,
			 memory *m)
{
	assert(buf->identifier_ids[token].raw == (u32)-1);

	if (buf->kinds[token] != TOK_IDENTIFIER)
		return;

	span span = buf->spans[token];
	u32 length = span.end - span.start;
	u8 *ptr = contents + span.start;
	u64 hash = fxhash(ptr, length);

	usize slot_index = hash % c->slot_count;
	slot *slot = &c->map[slot_index];
	while (slot->ptr != NULL) {
		if (slot->hash != hash)
			goto no_match;

		if (slot->length != length)
			goto no_match;

		if (memcmp(ptr, slot->ptr, length) != 0)
			goto no_match;

		// at this point we have compared
		// every possible source of equality we could,
		// and everything has come up as a match
		//
		// thus, we have a slot which contains
		// the current identifier
		//
		// we link the current token to
		// the identifier ID of the current slot,
		// and then we’re done with this token!
		buf->identifier_ids[token] = slot->id;
		return;

	no_match:
		slot_index++;
		slot = &c->map[slot_index];
	}

	// at this point we’ve exited the loop
	// because we found an empty slot
	// and along the way didn’t stumble upon a slot
	// which matches the current token

	slot->length = length;
	slot->id = c->ident_id;
	slot->ptr = ptr;
	slot->hash = hash;

	buf->identifier_ids[token] = c->ident_id;

	u8 *s = allocateInBump(&m->general, length + 1);
	memcpy(s, ptr, length);
	s[length] = '\0';
	c->identifier_contents[c->ident_id.raw] = s;

	c->ident_id.raw++;
}

interner intern(tokenBuffer *bufs, u8 **contents, usize buf_count,
		usize identifier_count, memory *m)
{
	// allocate space for load factor of 0.75
	usize slot_count = identifier_count * 4 / 3;
	usize map_size = slot_count * sizeof(slot);
	clearBump(&m->temp);
	slot *map = allocateInBump(&m->temp, map_size);
	memset(map, 0, map_size); // each ptr in map starts out as null

	ctx c = {
		.map = map,
		.slot_count = slot_count,
		.identifier_contents = allocateInBump(
			&m->general, identifier_count * sizeof(u8 *)),
		.ident_id = { .raw = 0 },
	};

	for (usize buf_index = 0; buf_index < buf_count; buf_index++) {
		tokenBuffer *buf = &bufs[buf_index];
		for (usize token = 0; token < buf->count; token++)
			processToken(buf, contents[buf_index], token, &c, m);
	}

	// we have no use for the map any longer
	// now that identifier IDs have been allocated
	// and identifier contents have been copied
	clearBump(&m->temp);

	interner i = {
		.contents = c.identifier_contents,
	};
	return i;
}

u8 *lookup(interner i, identifierId id)
{
	return i.contents[id.raw];
}
