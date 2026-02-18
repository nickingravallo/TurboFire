#ifndef GTO_SOLVER_H
#define GTO_SOLVER_H

#include <stdint.h>
#include <stdbool.h>
#include "ranks.h"

#define MAX_ACTIONS    8   // OOP: Check + 5 bet sizes; IP facing bet: Fold, Call, 3 raise sizes
#define BITS_PER_ACTION 3  // history stores action_id 0..7 per action
#define MAX_HISTORY    100
#define TABLE_SIZE     1000003
#define EMPTY_MAGIC    0xBEEFBEEF

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
	uint64_t p1_hand;           // Player 1 hole cards (bitmask)
	uint64_t p2_hand;           // Player 2 hole cards (bitmask)
	uint64_t board;             // Board cards (bitmask)
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

// Hash table for storing information sets
extern HashTable gto_table[TABLE_SIZE];

// Hash table management
void init_gto_table(void);
uint64_t hash_key(uint64_t key);
uint64_t make_info_set_key(uint64_t history, uint64_t board, uint64_t private_hand);
InfoSet* gto_get_or_create_node(uint64_t key);
InfoSet* gto_get_node(uint64_t key);  /* lookup only; NULL if not found */

// Game state management
void init_game_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand);
void gto_init_flop_state(GameState* state, uint64_t p1_hand, uint64_t p2_hand, uint64_t board);
bool is_terminal_state(GameState* state);
float gto_get_payout(GameState* state, int traverser);
uint8_t gto_get_legal_actions(GameState* state);
GameState gto_apply_action(GameState state, int action_id);
// Replay flop history from start; inits state with p1=0,p2=0,board, then applies num_actions from history.
void gto_replay_flop_history(uint64_t history, uint64_t board, int num_actions, GameState* out_state);

// MCCFR algorithm
void gto_get_strategy(float* regret, float* out_strategy, uint8_t legal_actions);
int gto_get_action(float* strategy, uint8_t legal_actions);
float gto_mccfr(GameState state, int traverser);
void gto_get_average_strategy(const InfoSet* node, float* out_probs);

// Utility functions
uint64_t deal_board_card(GameState state);
uint64_t combine_cards(uint64_t hand, uint64_t board);

#endif // GTO_SOLVER_H
