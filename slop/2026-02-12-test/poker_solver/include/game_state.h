/**
 * game_state.h - Poker Game State Management
 * 
 * This module manages the complete state of a poker hand:
 * - Player cards and positions
 * - Pot sizes and bets
 * - Betting rounds (streets)
 * - Action history
 * - Terminal state detection
 * 
 * The game follows Texas Hold'em rules with:
 * - 2 players (IP and OOP)
 * - 4 betting rounds: Preflop, Flop, Turn, River
 * - Showdown if neither player folds
 */

#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "cards.h"
#include "actions.h"

/* Maximum cards on board */
#define MAX_BOARD_CARDS 5
#define HOLE_CARDS 2

/* 
 * Streets (betting rounds)
 */
typedef enum {
    STREET_PREFLOP = 0,
    STREET_FLOP,
    STREET_TURN,
    STREET_RIVER,
    STREET_SHOWDOWN,
    NUM_STREETS
} Street;

/*
 * Players
 */
typedef enum {
    PLAYER_OOP = 0,   /* Out of position (Big Blind) */
    PLAYER_IP,        /* In position (Button/Small Blind) */
    NUM_PLAYERS
} Player;

/*
 * Game state structure
 * This is immutable - applying an action creates a new state
 */
typedef struct GameState {
    /* Cards */
    Card ip_cards[HOLE_CARDS];      /* IP hole cards */
    Card oop_cards[HOLE_CARDS];     /* OOP hole cards */
    Card board[MAX_BOARD_CARDS];    /* Community cards */
    int num_board_cards;            /* Current number of board cards (0, 3, 4, or 5) */
    
    /* Stacks (in bb) */
    double stacks[NUM_PLAYERS];
    
    /* Current bets (in bb) */
    double current_bets[NUM_PLAYERS];
    double to_call;                  /* Amount current player needs to call */
    
    /* Pot */
    double pot;
    
    /* Game progression */
    Street street;                   /* Current street */
    Player current_player;           /* Whose turn is it */
    Player last_aggressor;           /* Who made the last bet/raise */
    int num_actions_this_street;     /* Actions taken on current street */
    bool street_bet_made;            /* Has someone bet this street? */
    
    /* Terminal state */
    bool is_terminal;                /* Is hand over? */
    Player winner;                   /* Winner if terminal (or -1 for tie) */
    double ip_winnings;              /* Amount IP wins (negative if losing) */
    
    /* Action history for info set construction */
    uint64_t action_history;         /* Encoded action history */
    int history_depth;               /* Number of actions in history */
} GameState;

/*
 * Game initialization
 */

/* Initialize a new hand with given cards */
void game_init(GameState* state, 
               const Card* ip_cards, 
               const Card* oop_cards,
               const Card* board,
               int num_board);

/* Initialize from a deck (deals cards randomly) */
void game_init_random(GameState* state, Deck* deck);

/*
 * State transitions
 */

/* Apply action to state, returning true if successful */
bool game_apply_action(GameState* state, Action action);

/* Deal cards for next street (call after betting round ends) */
bool game_deal_street(GameState* state, Deck* deck);

/* 
 * Legal actions
 */

/* Get legal actions for current player */
int game_get_legal_actions(const GameState* state, Action* actions);

/* Check if action is legal */
bool game_is_action_legal(const GameState* state, Action action);

/*
 * Terminal state and payoffs
 */

/* Check if state is terminal and calculate payoffs if so */
bool game_is_terminal(GameState* state);

/* Calculate showdown winner and payoffs */
void game_calculate_showdown(GameState* state);

/* Get payoff for IP player (positive = IP wins, negative = OOP wins) */
double game_get_payoff(const GameState* state);

/*
 * Information set
 */

/* 
 * Create info set string for current player
 * Format: "cards|street|action_history"
 * Example: "AsKs|flop|b20c" (cards, street, bet 20%, call)
 */
void game_get_info_set(const GameState* state, char* buf, size_t buf_size);

/* Create compact info set hash */
uint64_t game_get_info_set_hash(const GameState* state);

/*
 * Utility functions
 */

/* Get current player's hole cards */
const Card* game_get_hole_cards(const GameState* state, Player player);

/* Get total board cards */
int game_get_all_cards(const GameState* state, Card* all_cards);

/* Copy game state */
void game_copy(const GameState* src, GameState* dst);

/* Get street name */
const char* street_to_string(Street street);

/* Get player name */
const char* player_to_string(Player player);

/* Print game state for debugging */
void game_print(const GameState* state);

#endif /* GAME_STATE_H */
