/**
 * game_state.c - Game State Implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "game_state.h"
#include "evaluator.h"

/* Street names */
static const char* STREET_NAMES[] = {
    "preflop",
    "flop",
    "turn",
    "river",
    "showdown"
};

/* Player names */
static const char* PLAYER_NAMES[] = {
    "OOP",
    "IP"
};

/* Action encoding for history (4 bits per action) */
static const char ACTION_CODES[] = {'f', 'c', 'C', 'b', 'B', 's', 'p', 'r'};

const char* street_to_string(Street street) {
    if (street < 0 || street >= NUM_STREETS) return "unknown";
    return STREET_NAMES[street];
}

const char* player_to_string(Player player) {
    if (player < 0 || player >= NUM_PLAYERS) return "unknown";
    return PLAYER_NAMES[player];
}

/* Initialize a new game with specific cards */
void game_init(GameState* state,
               const Card* ip_cards,
               const Card* oop_cards,
               const Card* board,
               int num_board) {
    memset(state, 0, sizeof(GameState));
    
    /* Set cards */
    memcpy(state->ip_cards, ip_cards, HOLE_CARDS * sizeof(Card));
    memcpy(state->oop_cards, oop_cards, HOLE_CARDS * sizeof(Card));
    memcpy(state->board, board, num_board * sizeof(Card));
    state->num_board_cards = num_board;
    
    /* Set initial stacks (100bb each) */
    state->stacks[PLAYER_IP] = STARTING_STACK - SB_SIZE;
    state->stacks[PLAYER_OOP] = STARTING_STACK - BB_SIZE;
    
    /* Initial bets (blinds) */
    state->current_bets[PLAYER_IP] = SB_SIZE;
    state->current_bets[PLAYER_OOP] = BB_SIZE;
    state->to_call = BB_SIZE - SB_SIZE;
    
    /* Initial pot */
    state->pot = SB_SIZE + BB_SIZE;
    
    /* Game starts preflop, IP acts first preflop */
    state->street = STREET_PREFLOP;
    state->current_player = PLAYER_IP;
    state->last_aggressor = PLAYER_OOP; /* BB is effectively last aggressor */
    state->num_actions_this_street = 0;
    state->street_bet_made = true; /* BB is considered a bet */
    
    /* Not terminal */
    state->is_terminal = false;
    state->winner = -1;
    state->ip_winnings = 0;
    
    /* No history yet */
    state->action_history = 0;
    state->history_depth = 0;
}

/* Initialize random game from deck */
void game_init_random(GameState* state, Deck* deck) {
    Card ip[HOLE_CARDS], oop[HOLE_CARDS], board[5];
    
    /* Deal hole cards */
    ip[0] = deck_deal(deck);
    ip[1] = deck_deal(deck);
    oop[0] = deck_deal(deck);
    oop[1] = deck_deal(deck);
    
    /* Deal board */
    board[0] = deck_deal(deck);
    board[1] = deck_deal(deck);
    board[2] = deck_deal(deck);
    board[3] = deck_deal(deck);
    board[4] = deck_deal(deck);
    
    game_init(state, ip, oop, board, 0);
}

/* Get legal actions */
int game_get_legal_actions(const GameState* state, Action* actions) {
    if (state->is_terminal) return 0;
    
    Player player = state->current_player;
    double to_call = state->to_call;
    double stack = state->stacks[player];
    double pot = state->pot;
    
    return get_legal_actions(actions, to_call, stack, pot);
}

bool game_is_action_legal(const GameState* state, Action action) {
    Action legal[MAX_ACTIONS];
    int num = game_get_legal_actions(state, legal);
    
    for (int i = 0; i < num; i++) {
        if (legal[i] == action) return true;
    }
    return false;
}

/* Apply action to state */
bool game_apply_action(GameState* state, Action action) {
    if (state->is_terminal) return false;
    if (!game_is_action_legal(state, action)) return false;
    
    Player player = state->current_player;
    Player opponent = (player == PLAYER_IP) ? PLAYER_OOP : PLAYER_IP;
    
    /* Record action in history */
    if (state->history_depth < 16) {
        state->action_history = (state->action_history << 4) | action;
        state->history_depth++;
    }
    
    switch (action) {
        case ACTION_FOLD:
            /* Player folds - opponent wins */
            state->is_terminal = true;
            state->winner = opponent;
            state->ip_winnings = (player == PLAYER_IP) ? -state->current_bets[PLAYER_IP] 
                                                       : state->current_bets[PLAYER_OOP];
            break;
            
        case ACTION_CHECK:
            /* Player checks */
            if (state->to_call > 0) {
                return false; /* Cannot check with bet to call */
            }
            
            state->num_actions_this_street++;
            
            /* If both players checked, or checked back, street ends */
            if (state->num_actions_this_street >= 2 && !state->street_bet_made) {
                if (state->street == STREET_RIVER) {
                    /* Showdown */
                    game_calculate_showdown(state);
                } else {
                    /* Move to next street */
                    state->street++;
                    state->num_board_cards += (state->street == STREET_FLOP) ? 3 : 1;
                    state->current_player = PLAYER_OOP; /* OOP acts first postflop */
                    state->num_actions_this_street = 0;
                    state->street_bet_made = false;
                    state->current_bets[PLAYER_IP] = 0;
                    state->current_bets[PLAYER_OOP] = 0;
                    state->to_call = 0;
                }
            } else {
                /* Switch to other player */
                state->current_player = opponent;
            }
            break;
            
        case ACTION_CALL:
            /* Player calls current bet */
            {
                double call_amount = state->to_call;
                if (call_amount > state->stacks[player]) {
                    call_amount = state->stacks[player]; /* All-in */
                }
                
                state->stacks[player] -= call_amount;
                state->current_bets[player] += call_amount;
                state->pot += call_amount;
                state->num_actions_this_street++;
                
                /* Check if street ends (call closes the action) */
                if (state->current_bets[player] >= state->current_bets[opponent]) {
                    if (state->street == STREET_RIVER) {
                        game_calculate_showdown(state);
                    } else {
                        /* Move to next street */
                        state->street++;
                        state->num_board_cards += (state->street == STREET_FLOP) ? 3 : 1;
                        state->current_player = PLAYER_OOP;
                        state->num_actions_this_street = 0;
                        state->street_bet_made = false;
                        state->current_bets[PLAYER_IP] = 0;
                        state->current_bets[PLAYER_OOP] = 0;
                        state->to_call = 0;
                    }
                } else {
                    state->current_player = opponent;
                }
            }
            break;
            
        case ACTION_BET_20:
        case ACTION_BET_33:
        case ACTION_BET_52:
        case ACTION_BET_100:
            /* Player bets */
            {
                double bet_size = calculate_bet_size(state->pot, action);
                if (bet_size > state->stacks[player]) {
                    bet_size = state->stacks[player]; /* All-in */
                }
                
                state->stacks[player] -= bet_size;
                state->current_bets[player] = bet_size;
                state->pot += bet_size;
                state->to_call = bet_size;
                state->street_bet_made = true;
                state->last_aggressor = player;
                state->num_actions_this_street++;
                state->current_player = opponent;
            }
            break;
            
        case ACTION_RAISE_2_5X:
            /* Player raises 2.5x */
            {
                double call_amount = state->to_call;
                double raise_amount = calculate_raise_size(call_amount, state->pot);
                double total_wager = call_amount + raise_amount;
                
                if (total_wager > state->stacks[player]) {
                    total_wager = state->stacks[player]; /* All-in */
                }
                
                state->stacks[player] -= total_wager;
                state->current_bets[player] += total_wager;
                state->pot += total_wager;
                state->to_call = state->current_bets[player] - state->current_bets[opponent];
                state->last_aggressor = player;
                state->num_actions_this_street++;
                state->current_player = opponent;
            }
            break;
            
        default:
            return false;
    }
    
    return true;
}

/* Calculate showdown winner */
void game_calculate_showdown(GameState* state) {
    /* Validate board cards */
    int num_cards = HOLE_CARDS + state->num_board_cards;
    if (num_cards < 5 || num_cards > 7) {
        /* Invalid number of cards, default to IP win to avoid crash */
        state->is_terminal = true;
        state->street = STREET_SHOWDOWN;
        state->winner = PLAYER_IP;
        state->ip_winnings = state->pot - state->current_bets[PLAYER_IP];
        return;
    }
    
    /* Create 7-card hands for evaluation */
    Card ip_hand[7] = {0};
    Card oop_hand[7] = {0};
    
    /* Copy hole cards */
    ip_hand[0] = state->ip_cards[0];
    ip_hand[1] = state->ip_cards[1];
    oop_hand[0] = state->oop_cards[0];
    oop_hand[1] = state->oop_cards[1];
    
    /* Copy board cards safely */
    for (int i = 0; i < state->num_board_cards && i < 5; i++) {
        ip_hand[HOLE_CARDS + i] = state->board[i];
        oop_hand[HOLE_CARDS + i] = state->board[i];
    }
    
    /* Evaluate hands */
    int ip_rank = evaluate_hand(ip_hand, num_cards);
    int oop_rank = evaluate_hand(oop_hand, num_cards);
    
    state->is_terminal = true;
    state->street = STREET_SHOWDOWN;
    
    double pot_won = state->pot;
    
    if (ip_rank < oop_rank) {
        /* IP wins (lower rank is better) */
        state->winner = PLAYER_IP;
        state->ip_winnings = pot_won - state->current_bets[PLAYER_IP];
    } else if (oop_rank < ip_rank) {
        /* OOP wins */
        state->winner = PLAYER_OOP;
        state->ip_winnings = -state->current_bets[PLAYER_IP];
    } else {
        /* Tie - split pot */
        state->winner = -1;
        state->ip_winnings = 0;
    }
}

/* Get payoff for IP */
double game_get_payoff(const GameState* state) {
    return state->ip_winnings;
}

/* Get info set string */
void game_get_info_set(const GameState* state, char* buf, size_t buf_size) {
    char cards_str[64];
    char history_str[64];
    
    /* Get cards for current player */
    const Card* hole = (state->current_player == PLAYER_IP) ? state->ip_cards : state->oop_cards;
    char c1[3], c2[3];
    card_to_string(hole[0], c1);
    card_to_string(hole[1], c2);
    
    /* Sort cards for canonical representation */
    if (hole[0] < hole[1]) {
        snprintf(cards_str, sizeof(cards_str), "%s%s", c1, c2);
    } else {
        snprintf(cards_str, sizeof(cards_str), "%s%s", c2, c1);
    }
    
    /* Decode action history */
    history_str[0] = '\0';
    uint64_t hist = state->action_history;
    int depth = state->history_depth;
    
    for (int i = 0; i < depth && i < 15; i++) {
        Action a = (Action)(hist & 0xF);
        hist >>= 4;
        
        if (a < NUM_ACTIONS) {
            size_t len = strlen(history_str);
            if (len < sizeof(history_str) - 2) {
                history_str[len] = ACTION_CODES[a];
                history_str[len + 1] = '\0';
            }
        }
    }
    
    /* Format: cards|street|history */
    snprintf(buf, buf_size, "%s|%s|%s", 
             cards_str, 
             street_to_string(state->street),
             history_str);
}

/* Get compact hash */
uint64_t game_get_info_set_hash(const GameState* state) {
    /* Combine cards, street, and history into hash */
    uint64_t hash = 0;
    
    /* Add cards - always sort for canonical representation */
    const Card* hole = (state->current_player == PLAYER_IP) ? state->ip_cards : state->oop_cards;
    Card c1 = hole[0];
    Card c2 = hole[1];
    
    /* Sort cards */
    if (c1 > c2) {
        Card tmp = c1;
        c1 = c2;
        c2 = tmp;
    }
    
    /* Cards in bits 0-15 */
    hash = ((uint64_t)c1 << 8) | c2;
    
    /* Street in bits 16-19 */
    hash |= ((uint64_t)(state->street & 0xF) << 16);
    
    /* Action history in bits 20-59 (limit to 10 actions = 40 bits) */
    /* Only use lower 40 bits of history to avoid overflow */
    hash |= ((state->action_history & 0xFFFFFFFFFFULL) << 20);
    
    return hash;
}

/* Get hole cards for player */
const Card* game_get_hole_cards(const GameState* state, Player player) {
    return (player == PLAYER_IP) ? state->ip_cards : state->oop_cards;
}

/* Get all cards (for evaluation) */
int game_get_all_cards(const GameState* state, Card* all_cards) {
    const Card* hole = game_get_hole_cards(state, state->current_player);
    all_cards[0] = hole[0];
    all_cards[1] = hole[1];
    memcpy(all_cards + 2, state->board, state->num_board_cards * sizeof(Card));
    return 2 + state->num_board_cards;
}

/* Copy state */
void game_copy(const GameState* src, GameState* dst) {
    memcpy(dst, src, sizeof(GameState));
}

/* Print state for debugging */
void game_print(const GameState* state) {
    printf("=== Game State ===\n");
    printf("Street: %s\n", street_to_string(state->street));
    printf("Pot: %.2f bb\n", state->pot);
    printf("Current player: %s\n", player_to_string(state->current_player));
    
    char ip1[3], ip2[3], oop1[3], oop2[3];
    card_to_string(state->ip_cards[0], ip1);
    card_to_string(state->ip_cards[1], ip2);
    card_to_string(state->oop_cards[0], oop1);
    card_to_string(state->oop_cards[1], oop2);
    
    printf("IP cards: %s %s\n", ip1, ip2);
    printf("OOP cards: %s %s\n", oop1, oop2);
    
    if (state->num_board_cards > 0) {
        printf("Board: ");
        for (int i = 0; i < state->num_board_cards; i++) {
            char c[3];
            card_to_string(state->board[i], c);
            printf("%s ", c);
        }
        printf("\n");
    }
    
    printf("Stacks: IP=%.2f OOP=%.2f\n", state->stacks[PLAYER_IP], state->stacks[PLAYER_OOP]);
    printf("Bets: IP=%.2f OOP=%.2f ToCall=%.2f\n", 
           state->current_bets[PLAYER_IP], 
           state->current_bets[PLAYER_OOP],
           state->to_call);
    
    if (state->is_terminal) {
        printf("TERMINAL - Winner: %s, IP winnings: %.2f\n",
               (state->winner == -1) ? "TIE" : player_to_string(state->winner),
               state->ip_winnings);
    }
    
    printf("==================\n");
}
