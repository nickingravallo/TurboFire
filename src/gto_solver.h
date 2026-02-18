#ifndef GTO_SOLVER_H
#define GTO_SOLVER_H

#include <stdint.h>
#include <stdbool.h>
#include "ranks.h"

#define MAX_ACTIONS    8   // OOP: Check + 5 bet sizes; IP facing bet: Fold, Call, 3 raise sizes
#define BITS_PER_ACTION 3  // history stores action_id 0..7 per action
#define MAX_HISTORY    100
#define TABLE_SIZE     8000003   /* ~700MB/table; reduce if OOM. Increase for faster merge (more RAM). */
#define EMPTY_MAGIC    0xBEEFBEEF
#define STARTING_FLOP_POT_BB 6.0f
#define STARTING_STACK_BB 97.0f

/* Action IDs: OOP / IP facing check use 0=Check, 1=Bet33, 2=Bet52, 3=Bet75, 4=Bet100, 5=Bet123.
 * IP facing bet uses 0=Fold, 1=Call, 2=Raise33, 3=Raise75, 4=Raise123. */
#define ACTION_CHECK   0
#define ACTION_BET_33  1
#define ACTION_BET_52  2
#define ACTION_BET_75  3
#define ACTION_BET_100 4
#define ACTION_BET_123 5
#define ACTION_FOLD    0   // when facing bet
#define ACTION_CALL    1
#define ACTION_RAISE_33  2
#define ACTION_RAISE_75  3
#define ACTION_RAISE_123 4

/* Legacy masks for terminal checks: treat action_id as "last action taken" (0..7). */
#define FOLD_MASK      0   // Fold is action_id 0 when facing bet
#define CHECK_MASK     0   // Check is action_id 0 when not facing bet
#define CALL_MASK      1
#define RAISE_MASK     4   // any raise
#define BET_MASK       1   // any bet (1..5)

#define P1             0
#define P2             1

#define STREET_PREFLOP 0
#define STREET_FLOP    1
#define STREET_TURN    2
#define STREET_RIVER   3

typedef struct {
	uint64_t history;           // Action history (BITS_PER_ACTION bits per action_id)
	float pot;                  // Current pot size
	float to_call;              // Amount current player must call (0 if no bet)
	float p1_stack;             // Remaining stack for OOP (P1)
	float p2_stack;             // Remaining stack for IP (P2)
	float p1_contribution;      // Total amount P1 has committed to the pot
	float p2_contribution;      // Total amount P2 has committed to the pot
	uint64_t p1_hand;           // Player 1 hole cards (bitmask)
	uint64_t p2_hand;           // Player 2 hole cards (bitmask)
	uint64_t board;             // Board cards (bitmask)
	uint64_t preset_turn_card;  // Optional fixed turn card revealed when street advances to turn
	uint64_t preset_river_card; // Optional fixed river card revealed when street advances to river
	uint8_t street;             // Current street (preflop/flop/turn/river)
	uint8_t active_player;      // P1 or P2
	uint8_t num_actions_this_street;
	uint8_t num_raises_this_street;
	uint8_t num_actions_total;
	uint8_t last_action;        // Last action_id taken (0..7)
	bool facing_bet;            // True if current player faces a bet (so action_id 0 = Fold)
	bool is_terminal;           // Terminal state flag
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

// Hash table for storing information sets (allocated in init_gto_table)
extern HashTable *gto_table;

// Hash table management
void init_gto_table(void);
void gto_init_table(HashTable *table);  /* init a table (e.g. per-thread) */
void gto_set_thread_table(HashTable *table);   /* use this table for get/create in this thread */
void gto_clear_thread_table(void);             /* revert to global table */
uint64_t hash_key(uint64_t key);
uint64_t make_info_set_key(
	uint64_t history, uint64_t board, uint64_t private_hand,
	float pot, float p1_stack, float p2_stack
);
InfoSet* gto_get_or_create_node(uint64_t key);
InfoSet* gto_get_node(uint64_t key);  /* lookup only; NULL if not found */
/* Merge src into dst. If progress is non-NULL, call progress(user, current, total) periodically during the pass. */
void gto_merge_table_into(HashTable *dst, const HashTable *src,
	void (*progress)(void *user, int current, int total), void *progress_user);

// Game state management
void init_game_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand);
void gto_init_flop_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand, uint64_t board);
void gto_init_postflop_state(
	GameState* state, uint64_t p1_hand, uint64_t p2_hand,
	uint64_t flop_board, uint64_t preset_turn_card, uint64_t preset_river_card
);
bool is_terminal_state(GameState* state);
float gto_get_payout(GameState* state, int traverser);
uint8_t gto_get_legal_actions(GameState* state);
GameState gto_apply_action(GameState state, int action_id);
// Replay flop history from start; compatibility wrapper around postflop replay with no preset cards.
void gto_replay_flop_history(uint64_t history, uint64_t board, int num_actions, GameState* out_state);
void gto_replay_postflop_history(
	uint64_t history, uint64_t flop_board, uint64_t preset_turn_card, uint64_t preset_river_card,
	int num_actions, GameState* out_state
);

// MCCFR algorithm
void gto_get_strategy(float* regret, float* out_strategy, uint8_t legal_actions);
int gto_get_action(float* strategy, uint8_t legal_actions);
float gto_mccfr(GameState state, int traverser);
void gto_get_average_strategy(const InfoSet* node, float* out_probs);

// Utility functions
uint64_t deal_board_card(GameState state);
uint64_t combine_cards(uint64_t hand, uint64_t board);

// Thread-local RNG (seed once per thread before solve work)
void gto_rng_seed(unsigned int seed);
float gto_rng_uniform(void);

#endif // GTO_SOLVER_H
