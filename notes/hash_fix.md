# Hash fix: rank map collision resolution

## List of changes

### 1. **`include/ranks.h`**
- Added `RANK_MAP_SIZE` (kept at `0x10000`; linear probing resolves collisions).
- Declared `get_rank_map_index(uint64_t hand)` returning `uint32_t`.

```c
#define RANK_MAP_SIZE        0x10000   /* 64K slots; linear probing resolves collisions */

uint32_t get_rank_map_index(uint64_t hand);
int      evaluate(uint64_t hand, uint64_t board);
void     init_flush_map();
void     init_rank_map();
```

### 2. **`src/ranks.c`**

- **Key array for collision resolution**
  - Added `rank_key[RANK_MAP_SIZE]` (`uint64_t`) to store the full 7-card hand in each used slot so lookups can distinguish different hands that hash to the same index.

```c
uint16_t flush_map[FLUSH_MAP_SIZE];
uint16_t rank_map[RANK_MAP_SIZE];
uint64_t rank_key[RANK_MAP_SIZE];  /* full hand for collision check (folded is not unique) */
```

- **Helper**
  - Added `get_rank_folded(uint64_t hand)` (static inline) returning `(hand >> 32) ^ (uint32_t)hand` for use in hashing and probing.

```c
static inline uint32_t get_rank_folded(uint64_t hand) {
	return (uint32_t)(hand >> 32) ^ (uint32_t)hand;
}
```

- **`get_rank_map_index`**
  - Return type changed from `uint16_t` to `uint32_t`.
  - Uses `get_rank_folded(hand)` and the same 16-bit multiplicative hash: `(folded * OMPEVAL_MAGIC) >> 16` with mask `RANK_MAP_SIZE - 1`.

```c
uint32_t get_rank_map_index(uint64_t hand) {
	uint32_t folded = get_rank_folded(hand);
	return (uint32_t)((folded * (uint64_t)OMPEVAL_MAGIC) >> 16) & (RANK_MAP_SIZE - 1);
}
```

- **`evaluate`**
  - Rank lookup now uses linear probing: start at the hashed index, then probe until `rank_map[id] == 0` or `rank_key[id] == combined`; return `rank_map[id]`.

```c
	id = get_flush_map_index(combined);
	if (id)
		return flush_map[id];	
	
	uint32_t folded = get_rank_folded(combined);
	id = (uint32_t)((folded * (uint64_t)OMPEVAL_MAGIC) >> 16) & (RANK_MAP_SIZE - 1);
	while (rank_map[id] != 0 && rank_key[id] != combined)
		id = (id + 1) & (RANK_MAP_SIZE - 1);
	return rank_map[id];
```

- **`generate_ranks_recursive`**
  - When storing a 7-card hand: compute start index from folded hand, then probe until an empty slot or the same hand. Store full hand in `rank_key` and strength in `rank_map`.

```c
	//base case 7 cards
	if (depth == 7) {
		uint32_t folded = get_rank_folded(current_hand);
		rank_id = (uint32_t)((folded * (uint64_t)OMPEVAL_MAGIC) >> 16) & (RANK_MAP_SIZE - 1);

		while (rank_map[rank_id] != 0 && rank_key[rank_id] != current_hand)
			rank_id = (rank_id + 1) & (RANK_MAP_SIZE - 1);
		if (rank_map[rank_id] != 0)
			return;  /* already stored this hand */

		rank_key[rank_id] = current_hand;
		rank_map[rank_id] = calculate_rank_strength(current_ranks);
		return;
	}
```

- **`init_rank_map`**
  - Still `memset(rank_map, 0, ...)`. No init of `rank_key`; empty slots are identified only by `rank_map[id] == 0`.

```c
void init_rank_map() {
	int rank_storage[7];

	memset(rank_map, 0, sizeof(rank_map));
	/* rank_key not initialized; empty is detected by rank_map[id]==0 only */

	generate_ranks_recursive(0, 0, 0, rank_storage);
}
```

### 3. **`test/rank_collision_test.c`**
- Removed local `#define RANK_MAP_SIZE 0x10000`; the test now uses `RANK_MAP_SIZE` from `ranks.h`.
- Rank-count loop uses `(int)RANK_MAP_SIZE` for the iteration bound.

```c
#define FLUSH_MAP_SIZE    0x2000
/* RANK_MAP_SIZE from ranks.h */

	//ranks test
	for ( i = 0; i < (int)RANK_MAP_SIZE; i++)
		if (rank_map[i]) //it's populated!
			rank_count++;
```

## Why this was needed

- There are **49,205** distinct non-flush 7-card hands but only **65,536** hash buckets.
- The original code did `if (rank_map[rank_id]) return;`, so only the first hand per bucket was stored; the rest were dropped (~16k hands).
- Enlarging the table (128K–1M) still left many collisions with the same hash.
- **Linear probing** with a **full 64-bit hand key** ensures every hand gets a slot and lookups resolve collisions correctly; the test now sees 49,205 rank entries as expected.
