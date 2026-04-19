#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

/* Hand evaluation: init once, then evaluate(hand_bitmask, board_bitmask). */
#include "ranks.h"

#define MAX_ACTIONS       8
#define BITS_PER_ACTION   3
#define MAX_HISTORY        100
#define TABLE_SIZE         2000003
#define EMPTY_MAGIC        0xBEEFBEEF

#define SB_CENTS 50
#define BB_CENTS 100
#define INITIAL_STACK 10000 // 100 Big Blinds in cents

/* OOP / IP facing check: 0=Check, 1=Bet33, 2=Bet52, 3=Bet100 */
/* IP facing bet: 0=Fold, 1=Call, 2=Raise33, 3=Raise52, 4=Raise100 */
static const int BET_PCT[]   = { 33, 52, 100 };
static const int RAISE_PCT[] = { 33, 52, 100 };
#define MAX_RAISES_PER_STREET  2

#define P1  0
#define P2  1
#define STREET_FLOP  1
#define STREET_TURN  2
#define STREET_RIVER 3

typedef struct {
    // --- 8-Byte Blocks (40 bytes) ---
    uint64_t history;       // Bit-packed actions
    uint64_t p1_hand;       // Bit-mask of cards
    uint64_t p2_hand; 
    uint64_t board;         // Up to 5 cards packed
    uint64_t deck_params;   // Turn/River constants

    // --- 4-Byte Blocks (16 bytes) ---
    uint32_t pot;           // Total in cents
    uint32_t p1_stack;      // Current chips remaining
    uint32_t p2_stack;
    uint32_t initial_pot;   // Useful for relative bet sizing

    // --- 1-Byte Blocks (8 bytes) ---
    uint8_t street;         // 0=Pre, 1=Flop, 2=Turn, 3=River
    uint8_t active_player;  // 0 or 1
    uint8_t actions_st;     // Actions this street
    uint8_t raises_st;      // Raises this street
    uint8_t last_action;    // The action ID that got us here
    uint8_t num_actions_total;

} GameState; // Total: 64 Bytes

typedef struct {
	float regret_sum[MAX_ACTIONS];
	float strategy_sum[MAX_ACTIONS];
	uint64_t key;
} InfoSet;

typedef struct {
	uint64_t key;
	InfoSet infoSet;
	uint64_t count;
} HashTable;

/*
 * Hashing and such
 *
 * Note: RNG is shared with the main GTO solver library (see gto_solver.c),
 * so we just declare the interfaces here and rely on that implementation
 * to avoid duplicate symbol errors at link time.
 */
extern void gto_rng_seed(unsigned int seed);
extern float gto_rng_uniform(void);

HashTable* table = NULL;

static uint64_t hash_id(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

void init_table() {
	size_t i;
	
	if (!table) {
		table = (HashTable*) malloc(TABLE_SIZE * sizeof(HashTable));
		if (!table)
			abort();
	}
	for (i = 0; i < TABLE_SIZE; i++)
		table[i].key = EMPTY_MAGIC;
}

/* * Assumes the 52-card deck is laid out as 4 contiguous 13-bit blocks.
 * Spades: 0-12, Hearts: 13-25, Diamonds: 26-38, Clubs: 39-51.
 */
static unsigned __int128 get_canonical_hand(uint64_t private_hand, uint64_t board) {
    // 1. Pack the board and hand together into a 128-bit integer.
    // Each suit will take up 26 bits (13 for board, 13 for hand).
    unsigned __int128 packed_suits = 0;
    
    // Spades
    packed_suits |= (unsigned __int128)((board >> 0) & 0x1FFF) << 0;
    packed_suits |= (unsigned __int128)((private_hand >> 0) & 0x1FFF) << 13;
    
    // Hearts
    packed_suits |= (unsigned __int128)((board >> 13) & 0x1FFF) << 26;
    packed_suits |= (unsigned __int128)((private_hand >> 13) & 0x1FFF) << 39;
    
    // Diamonds
    packed_suits |= (unsigned __int128)((board >> 26) & 0x1FFF) << 52;
    packed_suits |= (unsigned __int128)((private_hand >> 26) & 0x1FFF) << 65;
    
    // Clubs
    packed_suits |= (unsigned __int128)((board >> 39) & 0x1FFF) << 78;
    packed_suits |= (unsigned __int128)((private_hand >> 39) & 0x1FFF) << 91;

    // 2. Extract the 26-bit chunks for sorting
    uint32_t s[4];
    s[0] = (packed_suits >> 0)  & 0x3FFFFFF;
    s[1] = (packed_suits >> 26) & 0x3FFFFFF;
    s[2] = (packed_suits >> 52) & 0x3FFFFFF;
    s[3] = (packed_suits >> 78) & 0x3FFFFFF;

    // 3. Fast, branchless sorting network for 4 elements (descending)
    #define SWAP(a, b) do { \
        uint32_t t = s[a] ^ s[b]; \
        uint32_t mask = (s[a] < s[b]) ? ~0U : 0U; \
        s[a] ^= t & mask; \
        s[b] ^= t & mask; \
    } while(0)

    SWAP(0, 1); SWAP(2, 3); 
    SWAP(0, 2); SWAP(1, 3); 
    SWAP(1, 2);
    #undef SWAP

    // 4. Repack the sorted suits. The actual suits are stripped away; 
    // only the structural layout of the ranks remains.
    unsigned __int128 canonical = 0;
    canonical |= ((unsigned __int128)s[0]) << 0;
    canonical |= ((unsigned __int128)s[1]) << 26;
    canonical |= ((unsigned __int128)s[2]) << 52;
    canonical |= ((unsigned __int128)s[3]) << 78;

    return canonical;
}

static inline uint64_t make_info_set_key(uint64_t history, uint64_t board, uint64_t private_hand) {
    // 1. Get the combined 128-bit canonical representation
    unsigned __int128 canonical_state = get_canonical_hand(private_hand, board);

    // 2. Fold it down to 64 bits safely
    uint64_t folded_cards = (uint64_t)(canonical_state ^ (canonical_state >> 64));

    // 3. FNV-1a Mix: Just the folded cards and the history
    uint64_t key = 0xcbf29ce484222325ULL;
    
    key ^= folded_cards;
    key *= 0x100000001B3ULL; 
    
    key ^= history;
    key *= 0x100000001B3ULL;
    
    return key;
}

uint64_t get_infoset_key(GameState *state) {
	uint64_t active_hand = (state->active_player == P1) ? state->p1_hand : state->p2_hand;
	return make_info_set_key(state->history, state->board, active_hand);
}

InfoSet* get_or_create_node(uint64_t key) {
	uint64_t id;
	unsigned int probes = 0;
	int i;

	id = hash_id(key);
	while (table[id].key != EMPTY_MAGIC) {
		if (table[id].key == key)
			return &table[id].infoSet;

		id = (id + 1) % TABLE_SIZE;
		if (++probes >= TABLE_SIZE)
			return NULL;
	}

	table[id].key = key;
	for (i = 0; i < MAX_ACTIONS; i++) {
		table[id].infoSet.regret_sum[i] = 0;
		table[id].infoSet.strategy_sum[i] = 0;
	}

	table[id].infoSet.key = key;
	return &table[id].infoSet;
}

bool is_terminal(GameState *state) {
    // Showdown
    if (state->street > STREET_RIVER)
        return true;

    // Fold: Only valid if an action has actually occurred
    if (state->num_actions_total > 0 && state->last_action == 0 && state->p1_stack != state->p2_stack)
        return true;

    return false;
}

float evaluate_payoff(GameState *state, int traverser) {
	uint32_t my_stack = (traverser == P1) ?
		state->p1_stack :
		state->p2_stack;

	if (state->last_action == 0 && state->p1_stack != state->p2_stack) {
		int winner = 1 - state->active_player;

		if (traverser == winner)
			return (float)((my_stack + state->pot) - INITIAL_STACK);
		else
			return (float)(my_stack - INITIAL_STACK);
	}

	//showdown
	int p1_score = evaluate(state->p1_hand, state->board);
	int p2_score = evaluate(state->p2_hand, state->board);

	int my_score  = (traverser == P1) ? p1_score : p2_score;
	int opp_score = (traverser == P1) ? p2_score : p1_score;

	if (my_score > opp_score)
		return (float)((my_stack + state->pot) - INITIAL_STACK); //Win
	else if (my_score < opp_score) 
		return (float)(my_stack - INITIAL_STACK); // Lose
	else {
		uint32_t half_pot = state->pot / 2;
        	return (float)((my_stack + half_pot) - INITIAL_STACK); // Chop
	}
}

uint64_t get_dead_cards(GameState *state) {
	return state->board | state->p1_hand | state->p2_hand;
}

GameState advance_street(GameState state) {
	state.street++;
	state.actions_st = 0;
	state.raises_st = 0;

	state.active_player = P1;

	if (state.street > STREET_RIVER)
		return state;

	while (true) {
		uint64_t dead_cards = get_dead_cards(&state);

		int card_idx = (int)(gto_rng_uniform() * 52.0f);
		if (card_idx == 52)
			card_idx = 51;

		int rank = card_idx % 13;
		int suit = card_idx / 13;

		uint64_t card_mask = 1ULL << (rank + (suit * 16));

		if ((dead_cards & card_mask) == 0) {
			state.board |= card_mask;
			break;
		}
		//draw again
	}

	return state;
}


/*
 * actual solver logic, work on applying actions
 */
GameState apply_action(GameState state, int action_id) {
	state.history |= ((uint64_t)(action_id & 0b111) << (state.num_actions_total * 3));

	state.num_actions_total++;
	state.actions_st++;
	state.last_action = (uint8_t)action_id;

	uint32_t p1_invested = INITIAL_STACK - state.p1_stack;
	uint32_t p2_invested = INITIAL_STACK - state.p2_stack;

	uint32_t to_call = (state.active_player == 0) ?
		(p2_invested > p1_invested ? p2_invested - p1_invested : 0) :
		(p1_invested > p2_invested ? p1_invested - p2_invested : 0);

	uint32_t *actor_stack = (state.active_player == 0) ? &state.p1_stack : &state.p2_stack;

	//facing a bet, (fold / check / raise)
	if (to_call > 0) {
		if (action_id == 0)
			return state; //fold

		if (action_id == 1) {
			uint32_t commit = (*actor_stack < to_call) ? *actor_stack : to_call;
			*actor_stack -= commit;
			state.pot += commit;
			return advance_street(state);
		}

		//raise logic
		uint32_t raise_val = (uint32_t)(state.pot * (RAISE_PCT[action_id - 2] / 100.f));
		uint32_t total_commit = to_call + raise_val;
		if (total_commit > *actor_stack)
			total_commit = *actor_stack;

		*actor_stack -= total_commit;
		state.pot += total_commit;
		state.raises_st++;

		state.active_player = 1-state.active_player;
		return state;
	}

	//Not facing a bet, CHECK OR BET
	if (action_id == 0) {
		if (state.actions_st >= 2)
			return advance_street(state);

		state.active_player = 1 - state.active_player;
		return state;
	}

	//bet logic
	uint32_t bet_amt = (uint32_t)(state.pot * (BET_PCT[action_id - 1] / 100.f));
	if (bet_amt > *actor_stack)
		bet_amt = *actor_stack;

	*actor_stack -= bet_amt;
	state.pot += bet_amt;
	state.raises_st++;
	state.active_player = 1 - state.active_player;

	return state;
}

int get_legal_actions(GameState* state, int *legal_actions_out) {
	int count = 0;

	int32_t stack_diff = (state->active_player == 0) ?
			(int32_t)state->p1_stack - (int32_t)state->p2_stack :
			(int32_t)state->p2_stack - (int32_t)state->p1_stack;

	uint32_t to_call = (stack_diff > 0) ? (uint32_t)stack_diff : 0;

	uint32_t actor_stack = (state->active_player == 0) ? state->p1_stack : state->p2_stack;

	if (to_call > 0) {
		legal_actions_out[count++] = 0; //fold always legal
		legal_actions_out[count++] = 1; //call is always legal
		
		//only allow raises if we have more than required to call
		//also need to not hit raise cap
		if (actor_stack > to_call && state->raises_st < MAX_RAISES_PER_STREET) {
			legal_actions_out[count++] = 2; //raise size 1
			legal_actions_out[count++] = 3; //raise size 2
			legal_actions_out[count++] = 4; //raise size 3
		}
	}
	else {
		//not facing a bet
		legal_actions_out[count++] = 0; 
		
		//we can only bet if we have chips and raises arent capped
		if (actor_stack > 0 && state->raises_st < MAX_RAISES_PER_STREET) {
			legal_actions_out[count++] = 1; //bet size 1
			legal_actions_out[count++] = 2; //bet size 2
			legal_actions_out[count++] = 3; //bet size 3
		}
	}

	return count;
}

/*
 * cfr+ alg
 */
static float cfrp(GameState state, int traverser, int iter) {
	if (is_terminal(&state))
		return evaluate_payoff(&state, traverser);

	uint64_t key = get_infoset_key(&state);
	InfoSet *node = get_or_create_node(key);

	int legal_actions[MAX_ACTIONS];
	int num_legal_actions = get_legal_actions(&state, legal_actions);

	float strategy[MAX_ACTIONS] = {0};
	float sum_positive_regrets = 0.0f;

	/*
	 * Section is for external sampling, get the strategy values
	 */
	for (int i = 0; i < num_legal_actions; i++) {
		int action = legal_actions[i];
		float positive_regret = node->regret_sum[action] > 0.0f ?
			node->regret_sum[action] :
			0.0f;
		strategy[action] = positive_regret;
		sum_positive_regrets += positive_regret;
	}

	for (int i = 0; i < num_legal_actions; i++) {
		int action = legal_actions[i];
		if (sum_positive_regrets > 0.0f)
			strategy[action] /= sum_positive_regrets;
		else
			strategy[action] = 1.0f / num_legal_actions;
	}

	//traverse the tree and compute action utils
	float action_utils[MAX_ACTIONS] = {0};
	float node_util = 0.0f;

	for (int i = 0; i < num_legal_actions; i++) {
		int action = legal_actions[i];
		GameState next_state = apply_action(state, action);

		//recursive
		action_utils[action] = cfrp(next_state, traverser, iter);
		//calc ev for node
		node_util += strategy[action] * action_utils[action];
	}

	//update regrets and avg strat for cfr+
	if (state.active_player == traverser) {
		//we are traverser, upgrade our own regrets
		for (int i = 0; i < num_legal_actions; i++) {
			int action = legal_actions[i];
			float regret = action_utils[action] - node_util;

			node->regret_sum[action] += regret;
			if (node->regret_sum[action] < 0.0f)
				node->regret_sum[action] = 0.0f;
		}
	}
	else {
		//we are opponent: update avg strat
		for (int i = 0; i < num_legal_actions; i++) {
			int action = legal_actions[i];
			node->strategy_sum[action] += strategy[action] * (float)iter;
		}
	}

	return node_util;
}

// Helper to draw a single card
static uint64_t draw_random_card(uint64_t dead_cards) {
    while (true) {
        int card_idx = (int)(gto_rng_uniform() * 52.0f);
        if (card_idx == 52) card_idx = 51;
        
        int rank = card_idx % 13;
        int suit = card_idx / 13;
        uint64_t mask = 1ULL << (rank + (suit * 16));
        
        if ((dead_cards & mask) == 0) return mask;
    }
}

void print_node_strategy(GameState state) {
    uint64_t key = get_infoset_key(&state);
    uint64_t id = hash_id(key);
    unsigned int probes = 0;
    InfoSet *node = NULL;

    // Search the hash table
    while (table[id].key != EMPTY_MAGIC) {
        if (table[id].key == key) {
            node = &table[id].infoSet;
            break;
        }
        id = (id + 1) % TABLE_SIZE;
        if (++probes >= TABLE_SIZE) break;
    }

    if (!node) {
        printf("State not found in memory. It was never explored.\n");
        return;
    }

    int legal_actions[MAX_ACTIONS];
    int num_legal_actions = get_legal_actions(&state, legal_actions);

    // Calculate the total sum of all strategy weights
    float sum = 0.0f;
    for (int i = 0; i < num_legal_actions; i++) {
        sum += node->strategy_sum[legal_actions[i]];
    }

    int32_t stack_diff = (state.active_player == P1) ? 
        (int32_t)state.p1_stack - (int32_t)state.p2_stack : 
        (int32_t)state.p2_stack - (int32_t)state.p1_stack;
    uint32_t to_call = (stack_diff > 0) ? stack_diff : 0;

    printf("\n--- Strategy for Player %d ---\n", state.active_player + 1);
    for (int i = 0; i < num_legal_actions; i++) {
        int a = legal_actions[i];
        
        // Normalize the probability (or default to uniform if sum is 0)
        float prob = (sum > 0.0f) ? (node->strategy_sum[a] / sum) : (1.0f / (float)num_legal_actions);
        
        // Format the output based on the action type
        if (to_call > 0) {
            if (a == 0) printf("  Fold: ");
            else if (a == 1) printf("  Call: ");
            else printf("  Raise %d%%: ", RAISE_PCT[a - 2]); 
        } else {
            if (a == 0) printf("  Check: ");
            else printf("  Bet %d%%: ", BET_PCT[a - 1]);
        }
        
        printf("%.2f%%\n", prob * 100.0f);
    }
    printf("----------------------------\n");
}

int main() {
    // 1. Initialize Everything
    printf("Initializing tables...\n");
    init_rank_map();   // From your ranks file
    init_flush_map();  // From your ranks file
    init_table();      // Hash table
    gto_rng_seed((unsigned int)time(NULL));

    int num_iterations = 100000;

    printf("Starting CFR+ traversal...\n");
    for (int iter = 1; iter <= num_iterations; iter++) {
        
        // 2. Create a clean game state
        GameState root = {0};
        root.p1_stack = INITIAL_STACK - SB_CENTS;
        root.p2_stack = INITIAL_STACK - BB_CENTS;
        root.pot = SB_CENTS + BB_CENTS;
        root.active_player = P1; // P1 is SB preflop
        root.street = 0; // Preflop
        
        // 3. Deal Hole Cards
        root.p1_hand |= draw_random_card(root.board | root.p1_hand | root.p2_hand);
        root.p1_hand |= draw_random_card(root.board | root.p1_hand | root.p2_hand);
        
        root.p2_hand |= draw_random_card(root.board | root.p1_hand | root.p2_hand);
        root.p2_hand |= draw_random_card(root.board | root.p1_hand | root.p2_hand);

        // 4. CFR+ Alternating Updates
        cfrp(root, P1, iter);
        cfrp(root, P2, iter);

        if (iter % 10000 == 0) {
            printf("Completed iteration %d\n", iter);
        }
    }

    printf("Solving complete!\n");

    // 1. Create a clean root state matching your starting parameters
    GameState query_state = {0};
    query_state.p1_stack = INITIAL_STACK - SB_CENTS;
    query_state.p2_stack = INITIAL_STACK - BB_CENTS;
    query_state.pot = SB_CENTS + BB_CENTS;
    query_state.active_player = P1;
    query_state.street = 0;

    // 2. Hardcode Pocket Aces (Ace of Spades, Ace of Hearts)
    // Spades = bit 12 (offset 0), Hearts = bit 12 (offset 16)
    uint64_t ace_spades = 1ULL << 12;
    uint64_t ace_hearts = 1ULL << (12 + 16);
    query_state.p1_hand = ace_spades | ace_hearts;

    // 3. Print the strategy!
    print_node_strategy(query_state);

    return 0;
}
