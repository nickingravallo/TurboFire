#ifndef GTO_SOLVER_H
#define GTO_SOLVER_H

#include <stdint.h>
#include <stdbool.h>
#include "ranks.h"

#define MAX_ACTIONS    3  // fold, check/call, bet/raise
#define MAX_HISTORY    100
#define TABLE_SIZE     1000003
#define EMPTY_MAGIC    0xBEEFBEEF

#define FOLD_MASK      1
#define CHECK_MASK     2
#define CALL_MASK      2
#define RAISE_MASK     4
#define BET_MASK       4

#define P1             0
#define P2             1

#define STREET_PREFLOP 0
#define STREET_FLOP    1
#define STREET_TURN    2
#define STREET_RIVER   3

typedef struct {
	uint64_t history;           // Action history encoded (3 bits per action)
	float pot;                  // Current pot size
	uint64_t p1_hand;           // Player 1 hole cards (bitmask)
	uint64_t p2_hand;           // Player 2 hole cards (bitmask)
	uint64_t board;             // Board cards (bitmask)
	uint8_t street;             // Current street (preflop/flop/turn/river)
	uint8_t active_player;      // P1 or P2
	uint8_t num_actions_this_street;
	uint8_t num_raises_this_street;
	uint8_t num_actions_total;
	uint8_t last_action;        // Last action taken (mask)
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

// MCCFR algorithm
void gto_get_strategy(float* regret, float* out_strategy, uint8_t legal_actions);
int gto_get_action(float* strategy, uint8_t legal_actions);
float gto_mccfr(GameState state, int traverser);
void gto_get_average_strategy(const InfoSet* node, float* out_probs);

// Utility functions
uint64_t deal_board_card(GameState state);
uint64_t combine_cards(uint64_t hand, uint64_t board);

#endif // GTO_SOLVER_H
