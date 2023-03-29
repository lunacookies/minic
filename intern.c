#include "minic.h"

typedef struct slot {
	u32 length;
	identifierId id;
	char *ptr;
	u64 hash;
} slot;

typedef struct ctx {
	slot *map;
	usize slot_count;
	char **identifier_contents;
	identifierId ident_id;
	u8 pad[4];
} ctx;

static void processToken(tokenBuffer *buf, char *contents, usize token, ctx *c,
			 memory *m)
{
	assert(buf->identifier_ids[token].raw == (u32)-1);

	if (buf->kinds[token] != TOK_IDENTIFIER)
		return;

	span span = buf->spans[token];
	u32 length = span.end - span.start;
	char *ptr = contents + span.start;
	u64 hash = fxhash((u8 *)ptr, length);

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
		slot_index %= c->slot_count;
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

	char *s = bumpCopyArray(char, &m->general, ptr, length + 1);
	s[length] = 0;
	c->identifier_contents[c->ident_id.raw] = s;

	c->ident_id.raw++;
}

interner intern(tokenBuffer *bufs, char **contents, usize buf_count, memory *m)
{
	usize identifier_count = 0;
	for (u16 i = 0; i < buf_count; i++)
		for (usize j = 0; j < bufs[i].count; j++)
			if (bufs[i].kinds[j] == TOK_IDENTIFIER)
				identifier_count++;

	// We allocate enough space to have a load factor of 0.75
	// once the map has been populated.
	usize slot_count = identifier_count * 4 / 3;

	bumpMark mark = bumpCreateMark(&m->temp);
	slot *map = bumpAllocateArray(slot, &m->temp, slot_count);

	// Zero everything out so that each ptr starts out as null.
	memset(map, 0, slot_count * sizeof(slot));

	ctx c = {
		.map = map,
		.slot_count = slot_count,
		.identifier_contents = bumpAllocateArray(char *, &m->general,
							 identifier_count),
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
	bumpClearToMark(&m->temp, mark);

	return (interner){
		.contents = c.identifier_contents,
	};
}

char *internerLookup(interner i, identifierId id)
{
	return i.contents[id.raw];
}
