#include "minic.h"

typedef struct ctx {
	u64 *slot_hashes;
	u32 *slot_lengths;
	char **slot_ptrs;
	identifierId *slot_ids;
	usize slot_count;
	char **identifier_contents;
	identifierId ident_id;
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
	while (c->slot_ptrs[slot_index] != NULL) {
		if (c->slot_hashes[slot_index] != hash)
			goto no_match;

		if (c->slot_lengths[slot_index] != length)
			goto no_match;

		if (memcmp(ptr, c->slot_ptrs[slot_index], length) != 0)
			goto no_match;

		// We’ve compared every possible source of equality we could,
		// and everything has come up as a match.
		// Thus, we have a slot which contains the current identifier.
		// We link the current token to
		// the identifier ID of the current slot,
		// and then we’re done with this token!
		buf->identifier_ids[token] = c->slot_ids[slot_index];
		return;

	no_match:
		slot_index++;
		slot_index %= c->slot_count;
	}

	// At this point we’ve exited the loop because we found an empty slot
	// and along the way didn’t stumble upon a slot
	// which matches the current token.

	c->slot_hashes[slot_index] = hash;
	c->slot_lengths[slot_index] = length;
	c->slot_ptrs[slot_index] = ptr;
	c->slot_ids[slot_index] = c->ident_id;

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

	ctx c = {
		.slot_hashes = bumpAllocateArray(u64, &m->temp, slot_count),
		.slot_lengths = bumpAllocateArray(u32, &m->temp, slot_count),
		.slot_ptrs = bumpAllocateArray(char *, &m->temp, slot_count),
		.slot_ids =
			bumpAllocateArray(identifierId, &m->temp, slot_count),
		.slot_count = slot_count,
		.identifier_contents = bumpAllocateArray(char *, &m->general,
							 identifier_count),
		.ident_id = { .raw = 0 },
	};

	// Zero everything out so that each ptr starts out as null.
	memset(c.slot_ptrs, 0, slot_count * sizeof(char *));

	for (usize buf_index = 0; buf_index < buf_count; buf_index++) {
		tokenBuffer *buf = &bufs[buf_index];
		for (usize token = 0; token < buf->count; token++)
			processToken(buf, contents[buf_index], token, &c, m);
	}

	// We have no use for the map any longer now that
	// IDs have been allocated and contents have been copied.
	bumpClearToMark(&m->temp, mark);

	return (interner){
		.contents = c.identifier_contents,
	};
}

char *internerLookup(interner i, identifierId id)
{
	return i.contents[id.raw];
}
