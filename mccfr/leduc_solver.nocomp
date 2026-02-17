#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define NUM_CARDS       6    /* 2 suits × 3 ranks (J, Q, K) = 6 cards */
#define NUM_RANKS       3    /* J, Q, K */
#define NUM_ACTIONS     3    /* fold, call/check, bet/raise */

#define FOLD            0
#define CHECK_CALL      1
#define BET_RAISE       2

#define P1              0
#define P2              1

#define PREFLOP         0
#define FLOP            1

#define TABLE_SIZE      2000003
#define EMPTY_MAGIC     0xDEADBEEF

typedef struct {
    float regret_sum[NUM_ACTIONS];
    float strategy_sum[NUM_ACTIONS];
} Node;

typedef struct {
    uint32_t key;
    Node node;
} HashEntry;

HashEntry table[TABLE_SIZE];
int node_count = 0;

void init_table(void) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        table[i].key = EMPTY_MAGIC;
    }
    node_count = 0;
}

uint32_t hash(uint32_t key) {
    key ^= key >> 16;
    key *= 0x7feb352d;
    key ^= key >> 15;
    key *= 0x846ca68b;
    key ^= key >> 16;
    return key % TABLE_SIZE;
}

uint32_t make_key(int street, int history, int card, int board) {
    uint32_t key = 0;
    key |= (uint32_t)(street & 0x1);
    key |= (uint32_t)((history & 0xFF) << 1);
    key |= (uint32_t)((card & 0x7) << 9);
    key |= (uint32_t)((board & 0x7) << 12);
    return key;
}

Node* get_node(uint32_t key) {
    uint32_t h = hash(key);
    int probes = 0;
    while (table[h].key != EMPTY_MAGIC) {
        if (table[h].key == key) {
            return &table[h].node;
        }
        h = (h + 1) % TABLE_SIZE;
        if (++probes > 1000) {
            fprintf(stderr, "ERROR: Hash table full!\n");
            exit(1);
        }
    }
    table[h].key = key;
    memset(&table[h].node, 0, sizeof(Node));
    node_count++;
    return &table[h].node;
}

int get_rank(int card) {
    return card % NUM_RANKS;
}

int is_pair(int card, int board) {
    if (board < 0 || board >= NUM_CARDS) return 0;
    return get_rank(card) == get_rank(board);
}

int evaluate_showdown(int p1_card, int p2_card, int board) {
    int p1_pair = is_pair(p1_card, board);
    int p2_pair = is_pair(p2_card, board);
    
    if (p1_pair && !p2_pair) return 1;   /* P1 wins with pair */
    if (!p1_pair && p2_pair) return -1;  /* P2 wins with pair */
    if (p1_pair && p2_pair) {            /* Both have pairs - higher pair wins */
        int r1 = get_rank(p1_card);
        int r2 = get_rank(p2_card);
        if (r1 > r2) return 1;
        if (r2 > r1) return -1;
        return 0;
    }
    /* No pairs - compare high cards */
    int r1 = get_rank(p1_card);
    int r2 = get_rank(p2_card);
    if (r1 > r2) return 1;
    if (r2 > r1) return -1;
    return 0;
}

void get_strategy(Node* node, float* strategy) {
    float sum = 0.0f;
    for (int i = 0; i < NUM_ACTIONS; i++) {
        strategy[i] = node->regret_sum[i] > 0 ? node->regret_sum[i] : 0;
        sum += strategy[i];
    }
    if (sum > 0) {
        for (int i = 0; i < NUM_ACTIONS; i++) {
            strategy[i] /= sum;
        }
    } else {
        for (int i = 0; i < NUM_ACTIONS; i++) {
            strategy[i] = 1.0f / NUM_ACTIONS;
        }
    }
}

float cfr(int history, int p1_card, int p2_card, int board, int street,
          int pot, int traverser) {
    
    /* Count number of actions taken */
    int num_actions = 0;
    int h = history;
    while (h > 0) {
        num_actions++;
        h /= 3;
    }
    
    /* Determine active player and game state */
    int active_player = (num_actions % 2 == 0) ? P1 : P2;
    int active_card = (active_player == P1) ? p1_card : p2_card;
    int last_action = (history > 0) ? (history % 3) : -1;
    int prev_history = (history > 2) ? (history / 3) : 0;
    int prev_action = (prev_history > 0) ? (prev_history % 3) : -1;
    
    /* Terminal: Fold ends hand immediately */
    if (last_action == FOLD) {
        int folder = active_player;
        int winner = 1 - folder;
        if (traverser == winner) return (float)(pot - 2) / 2.0f;
        return -(float)(pot - 2) / 2.0f;
    }
    
    /* Terminal: Check-check ends the betting round */
    if (num_actions >= 2 && last_action == CHECK_CALL && prev_action == CHECK_CALL) {
        if (street == PREFLOP) {
            /* Go to flop */
            return cfr(0, p1_card, p2_card, board, FLOP, pot, traverser);
        } else {
            /* Showdown on flop */
            int result = evaluate_showdown(p1_card, p2_card, board);
            if (traverser == P1) {
                if (result > 0) return (float)(pot - 2) / 2.0f;
                if (result < 0) return -(float)(pot - 2) / 2.0f;
                return 0.0f;
            } else {
                if (result < 0) return (float)(pot - 2) / 2.0f;
                if (result > 0) return -(float)(pot - 2) / 2.0f;
                return 0.0f;
            }
        }
    }
    
    /* Terminal: Bet-call ends the betting round */
    if (num_actions >= 2 && last_action == CHECK_CALL && prev_action == BET_RAISE) {
        if (street == PREFLOP) {
            return cfr(0, p1_card, p2_card, board, FLOP, pot, traverser);
        } else {
            /* Showdown */
            int result = evaluate_showdown(p1_card, p2_card, board);
            if (traverser == P1) {
                if (result > 0) return (float)(pot - 2) / 2.0f;
                if (result < 0) return -(float)(pot - 2) / 2.0f;
                return 0.0f;
            } else {
                if (result < 0) return (float)(pot - 2) / 2.0f;
                if (result > 0) return -(float)(pot - 2) / 2.0f;
                return 0.0f;
            }
        }
    }
    
    /* Get or create information set node */
    uint32_t key = make_key(street, history, active_card, 
                            (street == FLOP) ? board : 7);
    Node* node = get_node(key);
    
    float strategy[NUM_ACTIONS];
    get_strategy(node, strategy);
    
    float action_values[NUM_ACTIONS] = {0};
    float node_value = 0.0f;
    
    if (active_player == traverser) {
        /* Traverser's turn: explore all possible actions */
        for (int a = 0; a < NUM_ACTIONS; a++) {
            int new_history = history * 3 + a;
            int new_pot = pot;
            int valid = 1;
            
            /* Determine valid actions and pot size changes */
            if (num_actions == 0) {
                /* P1 first to act: can check or bet */
                if (a == FOLD) valid = 0;
                if (a == BET_RAISE) new_pot += 4;  /* Bet 2 chips (1 ante + 2 bet) */
            }
            else if (num_actions == 1) {
                /* P2 responding to P1's action */
                if (last_action == CHECK_CALL) {
                    /* P1 checked, P2 can check or bet */
                    if (a == FOLD) valid = 0;
                    if (a == BET_RAISE) new_pot += 4;
                } else {  /* last_action == BET_RAISE */
                    /* P1 bet, P2 can fold, call, or raise */
                    if (a == CHECK_CALL) new_pot += 4;  /* Call the 2-chip bet */
                    if (a == BET_RAISE) new_pot += 8;   /* Raise to 4 more (total 5) */
                }
            }
            else {
                /* Later betting (facing a bet or raise) */
                if (last_action != BET_RAISE) {
                    valid = 0;  /* Should always be facing a bet here */
                } else {
                    if (a == FOLD) {
                        valid = 1;
                    } else if (a == CHECK_CALL) {
                        /* Call: match the last bet/raise */
                        new_pot += (prev_action == BET_RAISE) ? 8 : 4;
                    } else if (a == BET_RAISE) {
                        /* Raise: only if facing a bet (not a raise) */
                        if (prev_action == BET_RAISE) {
                            valid = 0;  /* Can't reraise in Leduc */
                        } else {
                            new_pot += 8;
                        }
                    }
                }
            }
            
            if (!valid) {
                action_values[a] = -1000000.0f;
                continue;
            }
            
            action_values[a] = cfr(new_history, p1_card, p2_card, board, street, new_pot, traverser);
            node_value += strategy[a] * action_values[a];
        }
        
        /* Update regrets using regret matching */
        for (int a = 0; a < NUM_ACTIONS; a++) {
            if (action_values[a] > -100000.0f) {
                float regret = action_values[a] - node_value;
                node->regret_sum[a] += regret;
            }
        }
    } else {
        /* Opponent's turn: sample one action (MCCFR) */
        int action = 0;
        float r = (float)rand() / RAND_MAX;
        float cum = 0.0f;
        for (int i = 0; i < NUM_ACTIONS; i++) {
            cum += strategy[i];
            if (r < cum) {
                action = i;
                break;
            }
        }
        
        int new_history = history * 3 + action;
        int new_pot = pot;
        
        /* Calculate new pot size */
        if (num_actions == 0 && action == BET_RAISE) {
            new_pot += 4;
        } else if (num_actions == 1 && last_action == CHECK_CALL && action == BET_RAISE) {
            new_pot += 4;
        } else if (num_actions == 1 && last_action == BET_RAISE) {
            if (action == CHECK_CALL) new_pot += 4;
            if (action == BET_RAISE) new_pot += 8;
        } else if (num_actions >= 2) {
            if (action == CHECK_CALL) {
                new_pot += (prev_action == BET_RAISE) ? 8 : 4;
            } else if (action == BET_RAISE) {
                new_pot += 8;
            }
        }
        
        node_value = cfr(new_history, p1_card, p2_card, board, street, new_pot, traverser);
        
        /* Update average strategy sum */
        for (int a = 0; a < NUM_ACTIONS; a++) {
            node->strategy_sum[a] += strategy[a];
        }
    }
    
    return node_value;
}

void train(int iterations) {
    int deck[NUM_CARDS];
    for (int i = 0; i < NUM_CARDS; i++) deck[i] = i;
    
    for (int iter = 0; iter < iterations; iter++) {
        /* Shuffle deck */
        for (int i = NUM_CARDS - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int tmp = deck[i];
            deck[i] = deck[j];
            deck[j] = tmp;
        }
        
        int p1_card = deck[0];
        int p2_card = deck[1];
        int board = deck[2];
        
        /* Both players traverse the game tree */
        cfr(0, p1_card, p2_card, board, PREFLOP, 2, P1);
        cfr(0, p1_card, p2_card, board, PREFLOP, 2, P2);
        
        if ((iter + 1) % 100000 == 0) {
            printf("Iteration %d/%d, nodes: %d\n", iter + 1, iterations, node_count);
        }
    }
}

void print_strategy(int street, int history, int card, int board, const char* prefix) {
    uint32_t key = make_key(street, history, card, (street == FLOP) ? board : 7);
    uint32_t h = hash(key);
    
    Node* node = NULL;
    int probes = 0;
    while (table[h].key != EMPTY_MAGIC && probes < TABLE_SIZE) {
        if (table[h].key == key) {
            node = &table[h].node;
            break;
        }
        h = (h + 1) % TABLE_SIZE;
        probes++;
    }
    
    if (!node) return;
    
    float sum = 0;
    for (int i = 0; i < NUM_ACTIONS; i++) {
        sum += node->strategy_sum[i];
    }
    if (sum < 0.001) return;
    
    float avg_strategy[NUM_ACTIONS];
    for (int i = 0; i < NUM_ACTIONS; i++) {
        avg_strategy[i] = node->strategy_sum[i] / sum;
    }
    
    const char* card_names[] = {"Jr", "Qr", "Kr", "Jb", "Qb", "Kb"};
    printf("%s %s: FOLD=%.3f CALL=%.3f RAISE=%.3f\n",
           prefix, card_names[card],
           avg_strategy[FOLD], avg_strategy[CHECK_CALL], avg_strategy[BET_RAISE]);
}

void print_results(void) {
    const char* card_names[] = {"Jr", "Qr", "Kr", "Jb", "Qb", "Kb"};
    
    printf("\n=== LEDUC POKER CFR RESULTS ===\n");
    printf("Total information sets: %d\n\n", node_count);
    
    printf("PREFLOP - P1 (first to act):\n");
    for (int c = 0; c < NUM_CARDS; c++) {
        print_strategy(PREFLOP, 0, c, 0, "  ");
    }
    
    printf("\nPREFLOP - P2 (after P1 checks):\n");
    for (int c = 0; c < NUM_CARDS; c++) {
        print_strategy(PREFLOP, 1, c, 0, "  ");
    }
    
    printf("\nPREFLOP - P2 (after P1 bets):\n");
    for (int c = 0; c < NUM_CARDS; c++) {
        print_strategy(PREFLOP, 2, c, 0, "  ");
    }
    
    printf("\nPREFLOP - P1 (facing P2 bet after P1 check):\n");
    for (int c = 0; c < NUM_CARDS; c++) {
        print_strategy(PREFLOP, 5, c, 0, "  ");
    }
    
    printf("\nPREFLOP - P1 (facing P2 raise after P1 bet):\n");
    for (int c = 0; c < NUM_CARDS; c++) {
        print_strategy(PREFLOP, 8, c, 0, "  ");
    }
    
    printf("\nFLOP - P1 (first to act, board=Jr):\n");
    for (int c = 0; c < NUM_CARDS; c++) {
        if (c != 0) print_strategy(FLOP, 0, c, 0, "  ");
    }
    
    printf("\nFLOP - P1 (first to act, board=Qr):\n");
    for (int c = 0; c < NUM_CARDS; c++) {
        if (c != 1) print_strategy(FLOP, 0, c, 1, "  ");
    }
}

int main(int argc, char** argv) {
    srand(time(NULL));
    
    printf("Leduc Poker CFR Solver\n");
    printf("======================\n\n");
    
    init_table();
    
    int iterations = (argc > 1) ? atoi(argv[1]) : 1000000;
    printf("Training for %d iterations...\n", iterations);
    
    train(iterations);
    
    printf("\nTraining complete!\n");
    print_results();
    
    return 0;
}
