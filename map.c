#include "minic.h"

typedef struct mapLookupResult {
	u64 hash;
	void *value;
	usize slot_index;
} mapLookupResult;

static mapLookupResult mapLookupInternal(map *m, void *key, usize key_size,
					 usize value_size)
{
	u64 hash = fxhash(key, key_size);
	usize slot_index = hash % m->slot_count;

	while (m->is_resident[slot_index]) {
		u64 slot_hash = m->hashes[slot_index];
		u8 *slot_key = m->keys + key_size * slot_index;

		if (slot_hash == hash && memcmp(slot_key, key, key_size) == 0) {
			u8 *value = m->values + value_size * slot_index;
			return (mapLookupResult){
				.hash = hash,
				.value = value,
				.slot_index = slot_index,
			};
		}

		slot_index++;
		slot_index %= m->slot_count;
	}

	return (mapLookupResult){
		.hash = hash,
		.value = NULL,
		.slot_index = slot_index,
	};
}

map mapCreate_(usize slot_count, bump *b, usize key_size, usize value_size)
{
	map m = {
		.hashes = bumpAllocateArray(u64, b, slot_count),
		.is_resident = bumpAllocateArray(bool, b, slot_count),
		.keys = bumpAllocateArray(u8, b, key_size * slot_count),
		.values = bumpAllocateArray(u8, b, value_size * slot_count),
		.slot_count = slot_count,
	};
	mapClear_(&m);
	return m;
}

void *mapLookup_(map *m, void *key, usize key_size, usize value_size)
{
	return mapLookupInternal(m, key, key_size, value_size).value;
}

void mapInsert_(map *m, void *key, void *value, usize key_size,
		usize value_size)
{
	mapLookupResult result =
		mapLookupInternal(m, key, key_size, value_size);
	if (result.value != NULL)
		internalError("tried to insert already-present value into map");

	m->hashes[result.slot_index] = result.hash;
	m->is_resident[result.slot_index] = true;

	u8 *key_p = m->keys + key_size * result.slot_index;
	memcpy(key_p, key, key_size);

	u8 *value_p = m->values + value_size * result.slot_index;
	memcpy(value_p, value, value_size);
}

void mapClear_(map *m)
{
	for (usize i = 0; i < m->slot_count; i++)
		m->is_resident[i] = false;
}
