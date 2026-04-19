#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NUM_CARDS     3 //2 private 1 public
#define MAX_ACTIONS   3 //fold call/check raise/bet
#define MAX_HISTORY   100

#define FOLD_MASK     1
#define CHECK_MASK    2
#define CALL_MASK     2
#define RAISE_MASK    4
#define BET_MASK      4

#define P1            0
#define P2            1

#define GET_RANK(card) (card / 2) //0=J 1=Q 2=K
#define GET_SUIT(card) (card % 2) //0=black 1=red

typedef enum { false, true } bool;

#define TABLE_SIZE    1000003
#define EMPTY_MAGIC   0xBEEFBEEF

typedef struct {
	uint64_t history;
	uint8_t pot;

	uint8_t p1_card;
	uint8_t p2_card;
	int8_t board_card;

	uint8_t street;
	uint8_t active_player;
	uint8_t num_actions_this_street;
	uint8_t num_raises_this_street;
	uint8_t num_actions_total;
	uint8_t last_action;
} GameState;

typedef struct {
	float regret_sum[MAX_ACTIONS];
	float strategy_sum[MAX_ACTIONS];
	uint64_t key;
} InfoSet;

typedef struct {
	uint64_t key;
	InfoSet infoSet;
} HashTable;

HashTable table[TABLE_SIZE];

void init_table() {
	for ( int i = 0; i < TABLE_SIZE; i++)
		table[i].key = EMPTY_MAGIC;
}

//pulled from murmurhash
uint64_t hash(uint64_t key) {
	key ^= key >> 33;
	key *= 0xff51afd7ed558ccdLLU;
	key ^= key >> 33;
	return key % TABLE_SIZE;
}

uint64_t make_hash_key(uint64_t history, int board, int private_card) {
	uint64_t key = 0;

	key |= (uint64_t)private_card;
	key |= (uint64_t)board   << 8;
	key |= (uint64_t)history << 16;
	
	return key;
}

InfoSet* get_or_create_node(uint64_t key) {
	uint64_t hash_key;
	int i;

	hash_key = hash(key) % TABLE_SIZE;
	while (table[hash_key].key != EMPTY_MAGIC) {
		if (table[hash_key].key == key)
			return &table[hash_key].infoSet;

		hash_key = (hash_key + 1) % TABLE_SIZE;
	}

	//empty slot
	table[hash_key].key = key;
	for (i = 0; i < MAX_ACTIONS; i++) {
		table[hash_key].infoSet.regret_sum[i] = 0;
		table[hash_key].infoSet.strategy_sum[i] = 0;
	}
	return &table[hash_key].infoSet;
}

static const char* rank_name(int rank) {
	static const char* names[] = { "J", "Q", "K" };
	return (rank >= 0 && rank < 3) ? names[rank] : "?";
}

static void history_to_string(uint64_t history, char* buf, size_t cap) {
	const char action_char[] = { '?', 'F', 'C', '?', 'R' }; /* 1=Fold, 2=Check/Call, 4=Raise */
	size_t len = 0;
	int shift = 0;
	buf[0] = '\0';
	while (len + 2 < cap && shift < 63) {
		uint64_t a = (history >> shift) & 7U;
		if (a == 0) break;
		char c = (a <= 4) ? action_char[a] : '?';
		buf[len++] = c;
		buf[len] = '\0';
		shift += 3;
	}
}

static void get_average_strategy(const InfoSet* node, float* out_probs) {
	float sum = 0;
	for (int a = 0; a < MAX_ACTIONS; a++) {
		float s = node->strategy_sum[a];
		out_probs[a] = (s > 0) ? s : 0;
		sum += out_probs[a];
	}
	if (sum > 0)
		for (int a = 0; a < MAX_ACTIONS; a++)
			out_probs[a] /= sum;
	else
		for (int a = 0; a < MAX_ACTIONS; a++)
			out_probs[a] = 1.0f / MAX_ACTIONS;
}

typedef struct {
	uint64_t key;
	InfoSet infoSet;
} TableEntry;

static int compare_entries(const void* a, const void* b) {
	uint64_t ka = ((const TableEntry*)a)->key;
	uint64_t kb = ((const TableEntry*)b)->key;
	uint8_t board_a = (ka >> 8) & 0xFF;
	uint8_t board_b = (kb >> 8) & 0xFF;
	if (board_a != board_b) return (board_a == 0xFF) ? -1 : (board_b == 0xFF ? 1 : (int)board_a - (int)board_b);
	if (ka != kb) return (ka < kb) ? -1 : 1;
	return 0;
}

void print_gto_strategy(void) {
	TableEntry* entries = (TableEntry*)malloc((size_t)TABLE_SIZE * sizeof(TableEntry));
	int n = 0;
	for (int i = 0; i < TABLE_SIZE; i++) {
		if (table[i].key != EMPTY_MAGIC) {
			entries[n].key = table[i].key;
			entries[n].infoSet = table[i].infoSet;
			n++;
		}
	}
	qsort(entries, (size_t)n, sizeof(TableEntry), compare_entries);

	printf("\n");
	printf("  ═══════════════════════════════════════════════════════════════════════════════════════════════\n");
	printf("  GTO Strategy (average strategy from MCCFR)\n");
	printf("  ═══════════════════════════════════════════════════════════════════════════════════════════════\n\n");

	const char* street_label[] = { "Preflop", "Flop" };
	char hist_buf[64];

	printf("  %-8s %-5s %-5s %-12s %8s %8s %8s\n",
		"Street", "Card", "Board", "History", "Fold %", "Call %", "Raise %");
	printf("  ─────────────────────────────────────────────────────────────────────────────────────────────\n");

	for (int i = 0; i < n; i++) {
		uint64_t key = entries[i].key;
		uint8_t private_card = key & 0xFF;
		uint8_t board_byte = (key >> 8) & 0xFF;
		uint64_t hist = key >> 16;

		int street = (board_byte == 0xFF) ? 0 : 1;
		int board_card = (street == 0) ? -1 : (int)board_byte;
		int rank = GET_RANK(private_card);

		if (street == 0)
			(void)strcpy(hist_buf, "-");
		else {
			history_to_string(hist, hist_buf, sizeof(hist_buf));
			if (hist_buf[0] == '\0') (void)strcpy(hist_buf, "(start)");
		}

		float probs[MAX_ACTIONS];
		get_average_strategy(&entries[i].infoSet, probs);

		printf("  %-8s %-5s %-5s %-12s %7.1f%% %7.1f%% %7.1f%%\n",
			street_label[street],
			rank_name(rank),
			street == 0 ? "-" : rank_name(GET_RANK(board_card)),
			hist_buf,
			(double)(probs[0] * 100.0f),
			(double)(probs[1] * 100.0f),
			(double)(probs[2] * 100.0f));
	}

	printf("  ═══════════════════════════════════════════════════════════════════════════════════════════════\n");
	printf("  Legend: F=Fold  C=Check/Call  R=Raise/Bet\n");
	printf("  ═══════════════════════════════════════════════════════════════════════════════════════════════\n\n");

	free(entries);
}

bool is_terminal(GameState* state) {
	if (state->last_action == FOLD_MASK)
		return true;

	if (state->street == 1) {
		if (state->last_action == CALL_MASK)
			return true;
		if (state->last_action == CHECK_MASK && state->num_actions_this_street >= 2)
			return true;
	}

	return false;
}

float get_payout(GameState* state, int traverser) {
	int opponent;

	int traverser_card, opponent_card;
	bool traverser_paired, opponent_paired;

	if (state->last_action == FOLD_MASK)
		return (state->active_player == traverser) ? -(state->pot / 2.0f) : (state->pot / 2.0f);

	opponent = 1 - traverser;
	traverser_card = (traverser == P1) ? state->p1_card : state->p2_card;
	opponent_card  = (opponent == P1)  ? state->p1_card : state->p2_card;

	traverser_paired = (GET_RANK(traverser_card) == GET_RANK(state->board_card));
	opponent_paired  = (GET_RANK(opponent_card) == GET_RANK(state->board_card));

	if (traverser_paired && !opponent_paired)
		return (state->pot / 2.0f);
	if (!traverser_paired && opponent_paired)
		return -(state->pot / 2.0f);

	if (GET_RANK(traverser_card) > GET_RANK(opponent_card))
		return (state->pot / 2.0f);
	if (GET_RANK(traverser_card) < GET_RANK(opponent_card)) 
		return -(state->pot / 2.0f);

	return 0.0f; //chop
}

void get_strategy(float* regret, float* out_strategy, uint8_t legal_actions) {
	int i;
	uint8_t num_legal_actions;
	float normalized_sum;

	num_legal_actions = __builtin_popcount(legal_actions);

	normalized_sum = 0;
	for ( i = 0 ; i < MAX_ACTIONS; i++) {
		if (legal_actions & (1 << i)) {
			out_strategy[i]   = (regret[i] <= 0) ? 0 : regret[i];
			normalized_sum += out_strategy[i];
		}
		else
			out_strategy[i] = 0.0;
	}

	for ( i = 0; i < MAX_ACTIONS; i++) {
		if (legal_actions & (1 << i)) {
			if (normalized_sum > 0)
				out_strategy[i] = out_strategy[i] / normalized_sum;
			else
				out_strategy[i] = 1.0 / (float)num_legal_actions;
		}
	}
}

int get_action(float* strategy, uint8_t legal_actions) {
	float r;
	float cumulative_strat;
	int a, last_legal_action;

	last_legal_action = 0;
	cumulative_strat = 0;
	r = (float)rand() / (float)RAND_MAX;

	for ( a = 0; a < MAX_ACTIONS; a++) {
		//check mask
		if (legal_actions & (1 << a)) {
			last_legal_action = a;
			cumulative_strat += strategy[a];

			if ( r < cumulative_strat )
				return a;
		}
	}

	return last_legal_action;
}

GameState apply_action(GameState state, int action_id) {
	int previous_action;

	previous_action = state.last_action;
	state.last_action = (1 << action_id);
	state.history |= (state.last_action << (state.num_actions_total * 3));
	state.num_actions_this_street++;
	state.num_actions_total++;

	if (action_id == 0) //fold
		return state;
	else if (action_id == 1) { //call or check
		bool is_facing_bet = (previous_action == RAISE_MASK);

		if (is_facing_bet) { //call the bet
			int call_amount;

			call_amount = (state.street == 0) ? 2 : 4;
			state.pot += call_amount;

			// Only advance street when calling on preflop (street 0). Calling on flop (street 1) ends the hand.
			if (state.street == 0) {
				state.street++;
				state.num_actions_this_street = 0;
				state.num_raises_this_street  = 0;
				state.active_player = P1;
				state.last_action = 0;
			}
			// else: street stays 1, last_action remains CALL_MASK -> is_terminal will end the hand
		}
		else { //check
			if (state.num_actions_this_street == 2) {
				// Only advance street when both checked on preflop. Check-check on flop ends the hand.
				if (state.street == 0) {
					state.street++;
					state.num_actions_this_street = 0;
					state.num_raises_this_street  = 0;
					state.active_player = P1;
					state.last_action = 0;
				}
				// else: street stays 1, last_action remains CHECK_MASK -> is_terminal will end the hand
			}
			else { //check->check
				state.active_player = 1 - state.active_player;
			}
		}
	}
	else if (action_id == 2) {
		int bet_limit;
		bool is_facing_bet;

		bet_limit = (state.street == 0) ? 2 : 4;
		is_facing_bet = (previous_action == RAISE_MASK);
		
		if (is_facing_bet)
			state.pot += (bet_limit * 2);
		else
			state.pot += bet_limit;

		state.num_raises_this_street++;
		state.active_player = 1 - state.active_player;
	}

	return state;
}

uint8_t get_legal_actions(GameState state) {
	uint8_t legal_mask;
	bool is_facing_bet;

	legal_mask = 0;
	legal_mask |= CHECK_MASK; //we can always call or check

	is_facing_bet = (state.last_action == RAISE_MASK);
	if (is_facing_bet)
		legal_mask |= FOLD_MASK;

	if (state.num_raises_this_street < 2)
		legal_mask |= RAISE_MASK;

	return legal_mask;
}

int deal_random_board_card(GameState state) {
	int rcard;

	do
		rcard = rand() % 6;	
	while (state.p1_card == rcard || state.p2_card == rcard);

	return rcard;
}

float mccfr(GameState state, int traverser) {
	float strategy[MAX_ACTIONS];
	float action_values[MAX_ACTIONS];
	float node_value;

	InfoSet* node;

	uint8_t legal_actions;
	int active_card;

	if (is_terminal(&state))
		return get_payout(&state, traverser);

	//chance node, going to flop
	if (state.street == 1 && state.board_card == -1) {
		GameState next_state = state;
		next_state.board_card = deal_random_board_card(state);
		next_state.num_actions_this_street = 0;
		next_state.num_raises_this_street = 0;
		return mccfr(next_state, traverser);
	}

	active_card = (state.active_player == P1) ? state.p1_card : state.p2_card;

	node = get_or_create_node(make_hash_key(state.history, state.board_card, active_card));
	legal_actions = get_legal_actions(state);
	get_strategy(node->regret_sum, strategy, legal_actions);
	node_value = 0;

	if (state.active_player == traverser) {
		//walk legal actions
		for ( int a = 0 ; a < MAX_ACTIONS; a++ ) {
			if (legal_actions & (1 << a)) {
				GameState next_state = apply_action(state, a);
				action_values[a] = mccfr(next_state, traverser);
				node_value += strategy[a] * action_values[a];
			}
		}

		//calcualte and update node regret
		for (int a = 0 ; a < MAX_ACTIONS; a++) {
			if (legal_actions & (1 << a)) {
				float regret = action_values[a] - node_value;
				node->regret_sum[a] += regret;
			}
		}
	}
	else {
		int sampled_action = get_action(strategy, legal_actions);
		GameState next_state = apply_action(state, sampled_action);

		node_value = mccfr(next_state, traverser);

		//update strategy
		for (int a = 0; a < MAX_ACTIONS; a++)
			if (legal_actions & (1 << a)) 
				node->strategy_sum[a] += strategy[a];
	}

	return node_value;
}

int main() {
    init_table();
    for (int i = 0; i < 1000000; i++) {
        GameState state = {0};
        // Initial Deal
        state.p1_card = rand() % 6;
        do { state.p2_card = rand() % 6; } while (state.p2_card == state.p1_card);
        
        state.board_card = -1;
        state.pot = 2; // Antes
        state.active_player = P1;

        // Alternate traversers so both players learn
        mccfr(state, P1);
        mccfr(state, P2);

        if (i % 100000 == 0) printf("Iteration %d...\n", i);
    }
    print_gto_strategy();
    return 0;
}
