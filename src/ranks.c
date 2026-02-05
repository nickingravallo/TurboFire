#include <stdint.h>
#include <string.h>

#include "ranks.h"

/*
 *   0 = Spades   (bits 0–12)
 *   1 = Hearts   (bits 16–28),
 *   2 = Diamonds (bits 32–44) 
 *   3 = Clubs    (bits 48–60).
 */
static const int SUIT_PERMUTATION[4] = { 1, 0, 3, 2 };

uint16_t flush_map[FLUSH_MAP_SIZE];
uint16_t rank_map[RANK_MAP_SIZE];
uint64_t rank_keys[RANK_MAP_SIZE];

//fast lookup table for combinatorics for cards, 0-12
static int nCk[13][6] = {
    {1,0,0,0,0,0}, {1,1,0,0,0,0}, {1,2,1,0,0,0}, {1,3,3,1,0,0}, {1,4,6,4,1,0}, 
    {1,5,10,10,5,1}, {1,6,15,20,15,6}, {1,7,21,35,35,21}, {1,8,28,56,70,56}, 
    {1,9,36,84,126,126}, {1,10,45,120,210,252}, {1,11,55,165,330,462}, {1,12,66,220,495,792}
};


static uint16_t get_rank_hash(uint64_t hand) {
	uint32_t folded;
	
	folded = (uint32_t)(hand >> 32) ^ (uint32_t)hand;
	return (uint16_t)((folded * (uint64_t)OMPEVAL_MAGIC) >> 16);
}

/* Canonicalize 7-card hand so same rank multiset always gives same 64-bit value.
 * Extract ranks in order 0..12 (like generate_ranks_recursive) and assign suits via SUIT_PERMUTATION. */
static uint64_t canonicalize_hand(uint64_t hand) {
	int ranks[7], n = 0;
	int count[13] = { 0 };
	uint64_t out = 0;
	int r, s, i;

	for (r = 0; r < 13; r++) {
		for (s = 0; s < 4; s++) {
			if (((hand >> (16 * s)) & 0x1FFF) & (1U << r))
				ranks[n++] = r;
		}
	}
	for (i = 0; i < 7; i++) {
		int rank = ranks[i];
		int suit = SUIT_PERMUTATION[count[rank]++];
		out |= 1ULL << (rank + 16 * suit);
	}
	return out;
}

uint16_t get_flush_map_index(uint64_t hand) {
	uint16_t fc, fd, fh, fs;

	//create 13bit hand signature 
	fc = (hand >> 48) & 0x1FFF ;
	fd = (hand >> 32) & 0x1FFF ;
	fh = (hand >> 16) & 0x1FFF ;
	fs =  hand        & 0x1fff ;

	//__builtin_popcount() is a quick gcc func for counting bits 
	if (__builtin_popcount(fc) >= 5) return fc;
	if (__builtin_popcount(fd) >= 5) return fd;
	if (__builtin_popcount(fh) >= 5) return fh;
	if (__builtin_popcount(fs) >= 5) return fs;

	return 0;
}

uint64_t combine_hand_board(uint64_t hand, uint64_t board) {
	return hand | board;
}

int evaluate(uint64_t hand, uint64_t board) {
	uint64_t combined, canonical;
	uint16_t id, flush_id;

	combined = combine_hand_board(hand, board); 
	
	flush_id = get_flush_map_index(combined);
	if (flush_id)
		return flush_map[flush_id];	
	
	canonical = canonicalize_hand(combined);
	id = get_rank_hash(canonical);

	while (rank_keys[id] != 0 && rank_keys[id] != canonical)
		id = (id + 1) & RANK_MAP_MASK;

	return rank_map[id];
}

uint16_t calculate_flush_strength_from_hand(uint16_t generated, int *normal_flush_counter) {
	int i;
	uint16_t rank;

	//wheel straight flush
	if ((generated & 0b1000000001111) == 0b1000000001111)
		return STRAIGHT_FLUSH_FLOOR + 1;
	
	// check all other straight flushes
	// we're going to shift the straight flush all the way up to royal
	for (i = 8; i >= 0; i--)
		if (((generated >> i) & 0b11111) == 0b11111) 
			return STRAIGHT_FLUSH_FLOOR + (i + 2);
	
	//all others, we're now counting up
	rank = FLUSH_FLOOR + *normal_flush_counter;

	(*normal_flush_counter)++;

	return rank;
}

uint16_t calculate_rank_strength(int *ranks) {
	int i;
	int rank;
	int rank_counts[13] = { 0 }; //histogram for card counts
	uint16_t rank_mask;

	int quads, trips, high_pair, low_pair;

	int kicker;

	//pop histogram
	rank_mask = 0;
	for ( i = 0 ; i < 7; i++ ) {
		rank = ranks[i];
		rank_counts[rank]++;
		rank_mask |= (1 << rank);
	}
	
	//find groups
	quads     = -1; 
	trips     = -1;
	high_pair = -1; 
	low_pair  = -1;

	for ( i = 12; i >= 0; i--) {
		if (rank_counts[i] == 4)
			quads = i;
		else if (rank_counts[i] == 3) {
			if (trips == -1)
				trips = i;
			//old trips are now a pair
			else if (high_pair == -1)
				high_pair = i;
		}
		else if(rank_counts[i] == 2) {
			if (high_pair == -1)
				high_pair = i;
			else if (low_pair == -1) 
				low_pair = i;
		}
	}

	//check quads
	kicker = -1;

	if (quads != -1) {
		//best kicker
		for ( i = 12; i >= 0; i--) {
			if (rank_counts[i] > 0 && i != quads) {
				kicker = i;
				break;
			}
		}

		// fix for actual card value
		if (kicker > quads)
			kicker--;

		return QUADS_FLOOR + (quads * 12) + kicker + 1;
	}

	//check full house
	if (trips != -1 && high_pair != -1) {
		//normalize top pair
		if (high_pair > trips)
			high_pair--;

		return FULL_HOUSE_FLOOR + (trips * 12) + high_pair + 1;
	}

	//straights - broadways
	for (i = 8; i >= 0; i--) {
		if (((rank_mask >> i) & 0b11111) == 0b11111) {
			return STRAIGHT_FLOOR + (i + 2);
		}
	}

	//straights - wheel
	if ((rank_mask & 0b1000000001111) == 0b1000000001111) 
		return STRAIGHT_FLOOR + 1;

	//trips
	if (trips != -1) {
		int kicker_high, kicker_low;
		kicker_high = -1;
		kicker_low  = -1;

		for (i = 12; i >= 0; i--) {
			if (rank_counts[i] > 0 && i != trips) {
				//find first non-trips kicker, then get the lower
				if (kicker_high == -1) {
					kicker_high = i;
				}
				else {
					kicker_low = i;
					break;
				}
			}
		}

		//normalize
		if (kicker_high > trips)
			kicker_high--;
		if (kicker_low  > trips)
			kicker_low --;

		return TRIPS_FLOOR + (trips * 66) + nCk[kicker_high][2] + nCk[kicker_low][1] + 1; 
	}

	int kicker_score;
	//two pair
	if (high_pair != -1 && low_pair != -1) {
		kicker = -1;

		//we only need one kicker...
		for( i = 12; i >= 0; i--) {
			if (rank_counts[i] > 0 && i != high_pair && i != low_pair) {
				kicker = i;
				break;
			}
		}

		if (kicker > high_pair)
			kicker--;
		if (kicker > low_pair)
			kicker--;

		kicker_score = nCk[high_pair][2] + nCk[low_pair][1]; 
		return TWO_PAIR_FLOOR + (kicker_score* 11) + kicker + 1;
	}

	//one pair
	if (high_pair != -1) {
		int kickers[3];
		int id;

		id = 0;
		for ( i = 12; i >= 0; i--) {
			if (rank_counts[i] > 0 && i != high_pair) {
				kickers[id] = i;
				//normalize the kicker
				if (kickers[id] > high_pair)
					kickers[id]--;
				id++;
				if (id == 3)
					break;
			}
		}

		kicker_score = nCk[kickers[0]][3] + nCk[kickers[1]][2] + nCk[kickers[2]][1];
		return ONE_PAIR_FLOOR + (high_pair * 220) + kicker_score + 1;
	}

	//high card
	int kickers[5];
	int id;
	int high_card_score;

	id = 0;
	for ( i = 12; i >= 0; i--) {
		if (rank_counts[i] > 0) {
			kickers[id++] = i;
			if (id == 5)
				break;
		}
	}
	
	high_card_score = nCk[kickers[0]][5] + nCk[kickers[1]][4] + nCk[kickers[2]][3] + nCk[kickers[3]][2] + nCk[kickers[4]][1];
	return HIGH_CARD_FLOOR + high_card_score + 1;
}

void generate_ranks_recursive(int depth, int start_rank, uint64_t current_hand, int *current_ranks) {
	uint16_t id;
	int count;
	uint64_t new_card;
	int rank, k;

	if (depth == 7) {
		id = get_rank_hash(current_hand);

		while (rank_keys[id] != 0 && rank_keys[id] != current_hand)
			id = (id + 1) & RANK_MAP_MASK;

		if (rank_keys[id] == 0) {
			rank_map[id] = calculate_rank_strength(current_ranks);
			rank_keys[id] = current_hand;
		}
		return;
	}

	for (rank = start_rank; rank <= 12; rank++) {
		count = 0;
		for (k = 0; k < depth; k++)
			if (current_ranks[k] == rank)
				count++;

		if (count >= 4)
			continue;

		current_ranks[depth] = rank;
		new_card = (1ULL << (rank + (16 * SUIT_PERMUTATION[count])));
		generate_ranks_recursive(depth + 1, rank, current_hand | new_card, current_ranks);
	}
}

void init_rank_map() {
	int rank_storage[7];

	//no zero sets in recurse, safety
	memset(rank_map, 0, sizeof(rank_map));
	memset(rank_keys, 0, sizeof(rank_keys));

	generate_ranks_recursive(0, 0, 0, rank_storage);
}

void init_flush_map() {
	int count, normal_flush_counter, i;

	normal_flush_counter = 0;
	for (i = 0; i < 0x2000; i++) {
		count = __builtin_popcount(i);
		if (count == 5) 
			flush_map[i] = calculate_flush_strength_from_hand(i, &normal_flush_counter);
		else if (count > 5)
			// a fun hack to set 6/7 card hand to its 5 card hand value
			// ex: B:AdQdJdTd3s H:Kd3d will be hashed to diff indeces with but same result as AdKdQdJdTd 
			flush_map[i] = flush_map[i & (i - 1)]; 
		else
			flush_map[i] = 0; // non flush
	}
}



 
