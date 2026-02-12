/**
 * main.c - MCCFR Poker Solver Main Entry Point
 * 
 * This is the entry point for the poker solver. It handles:
 * - Command line argument parsing
 * - Initialization (hand evaluator, MCCFR state)
 * - Training loop with TUI
 * - Checkpoint management
 * - Strategy export
 * - Query mode for specific hands
 * 
 * Usage:
 *   ./poker_solver                    # Train with default settings
 *   ./poker_solver query              # Query trained strategies
 *   ./poker_solver generate_table     # Generate hand evaluation table
 *   ./poker_solver resume checkpoint  # Resume from checkpoint
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "cards.h"
#include "evaluator.h"
#include "mccfr.h"
#include "game_state.h"
#include "strategy.h"

/* Global state for signal handling */
static MCCFRState* g_mccfr_state = NULL;
static volatile int g_should_stop = 0;

/* Signal handler for graceful shutdown */
void signal_handler(int sig) {
    printf("\n\nReceived signal %d, saving checkpoint...\n", sig);
    g_should_stop = 1;
    
    if (g_mccfr_state) {
        char filename[256];
        snprintf(filename, sizeof(filename), "data/checkpoint_%d.dat",
                g_mccfr_state->current_iteration);
        mccfr_save_checkpoint(g_mccfr_state, filename);
        printf("Checkpoint saved: %s\n", filename);
    }
    
    exit(0);
}

/* Print usage information */
void print_usage(const char* program) {
    printf("Usage: %s [command] [options]\n\n", program);
    printf("Commands:\n");
    printf("  train [options]               Train the solver (default)\n");
    printf("  query [options]               Query specific hand strategies\n");
    printf("  generate_table [output_file]  Generate hand evaluation lookup table\n");
    printf("  resume <checkpoint_file>      Resume training from checkpoint\n");
    printf("\n");
    printf("Training Options:\n");
    printf("  -i <iterations>               Number of iterations (default: 1000000)\n");
    printf("  -c <interval>                 Checkpoint interval (default: 100000)\n");
    printf("\n");
    printf("Query Options:\n");
    printf("  --hand <cards>                Your hole cards (e.g., AsKs, 7h2d)\n");
    printf("  --board <cards>               Board cards (e.g., Kh7d2c)\n");
    printf("  --history <actions>           Action history (e.g., b33c = 33%% bet, call)\n");
    printf("  --street <street>             preflop/flop/turn/river (auto-detected from board)\n");
    printf("  -f <file>                     Load strategy from file\n");
    printf("\n");
    printf("Query Action Codes:\n");
    printf("  f = Fold, c = Check, C = Call\n");
    printf("  b = Bet 20%%, B = Bet 33%%, s = Bet 52%%, p = Bet 100%%\n");
    printf("  r = Raise 2.5x\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                              # Train with 1M iterations\n", program);
    printf("  %s -i 500000                   # Train for 500k iterations\n", program);
    printf("  %s query --hand AsKs --board Kh7d2c\n", program);
    printf("  %s query --hand 7h2d --board Kh7d2c --history b33c\n", program);
    printf("  %s query --hand AsAc --street preflop\n", program);
    printf("  %s resume data/checkpoint_100000.dat\n", program);
}

/* Parse a card string like "As" or "Kh" into a Card value */
int parse_card_string(const char* str, Card* card) {
    if (strlen(str) < 2) return -1;
    
    /* Parse rank */
    int rank = -1;
    char rank_char = str[0];
    for (int i = 0; i < NUM_RANKS; i++) {
        if (RANK_CHARS[i] == rank_char) {
            rank = i;
            break;
        }
    }
    
    /* Parse suit */
    int suit = -1;
    char suit_char = str[1];
    for (int i = 0; i < NUM_SUITS; i++) {
        if (SUIT_CHARS[i] == suit_char) {
            suit = i;
            break;
        }
    }
    
    if (rank == -1 || suit == -1) return -1;
    
    *card = make_card(rank, suit);
    return 0;
}

/* Parse hand string like "AsKs" into cards */
int parse_hand(const char* hand_str, Card* cards, int max_cards) {
    int len = strlen(hand_str);
    int card_idx = 0;
    
    for (int i = 0; i < len && card_idx < max_cards; i += 2) {
        char card_str[3] = {hand_str[i], hand_str[i+1], '\0'};
        if (parse_card_string(card_str, &cards[card_idx]) != 0) {
            fprintf(stderr, "Error: Invalid card '%s'\n", card_str);
            return -1;
        }
        card_idx++;
    }
    
    return card_idx;
}

/* Parse board string like "Kh7d2c" into cards */
int parse_board(const char* board_str, Card* cards, int max_cards) {
    return parse_hand(board_str, cards, max_cards);
}

/* Parse action history string into encoded history */
uint64_t parse_history(const char* history_str) {
    uint64_t history = 0;
    int len = strlen(history_str);
    
    for (int i = 0; i < len && i < 16; i++) {
        Action action;
        switch (history_str[i]) {
            case 'f': action = ACTION_FOLD; break;
            case 'c': action = ACTION_CHECK; break;
            case 'C': action = ACTION_CALL; break;
            case 'b': action = ACTION_BET_20; break;
            case 'B': action = ACTION_BET_33; break;
            case 's': action = ACTION_BET_52; break;
            case 'p': action = ACTION_BET_100; break;
            case 'r': action = ACTION_RAISE_2_5X; break;
            default: continue;
        }
        history = (history << 4) | action;
    }
    
    return history;
}

/* Display strategy for an info set */
void display_strategy(StrategyTable* strategy_table, uint64_t info_set_key) {
    InfoSetData* data = strategy_get_existing(strategy_table, info_set_key);
    
    if (!data || data->visit_count == 0) {
        printf("  [No strategy data - this spot hasn't been trained yet]\n");
        printf("  Tip: Run more training iterations to cover this spot\n");
        return;
    }
    
    printf("\n  Strategy (based on %lu visits):\n", data->visit_count);
    printf("  ┌─────────────────────────────────────────┐\n");
    
    /* Get average strategy */
    double avg_strategy[MAX_INFO_SET_ACTIONS];
    get_average_strategy(data, avg_strategy);
    
    /* Sort by frequency for display */
    typedef struct {
        Action action;
        double freq;
        double regret;
    } ActionFreq;
    
    ActionFreq freqs[MAX_INFO_SET_ACTIONS];
    for (int i = 0; i < data->num_actions; i++) {
        freqs[i].action = data->actions[i];
        freqs[i].freq = avg_strategy[i];
        freqs[i].regret = data->regrets[i];
    }
    
    /* Sort by frequency descending */
    for (int i = 0; i < data->num_actions - 1; i++) {
        for (int j = i + 1; j < data->num_actions; j++) {
            if (freqs[j].freq > freqs[i].freq) {
                ActionFreq tmp = freqs[i];
                freqs[i] = freqs[j];
                freqs[j] = tmp;
            }
        }
    }
    
    /* Display each action */
    for (int i = 0; i < data->num_actions; i++) {
        const char* action_name = action_to_display_string(freqs[i].action);
        double freq = freqs[i].freq * 100.0;
        double regret = freqs[i].regret;
        
        /* Create bar graph */
        int bar_len = (int)(freq / 2);
        if (bar_len > 25) bar_len = 25;
        if (bar_len < 1 && freq > 0) bar_len = 1;
        
        char bar[26];
        for (int j = 0; j < bar_len; j++) bar[j] = '█';
        bar[bar_len] = '\0';
        
        printf("  │ %-12s %5.1f%% %-25s │ (regret: %+7.2f)\n", 
               action_name, freq, bar, regret);
    }
    
    printf("  └─────────────────────────────────────────┘\n");
    
    /* Best action */
    Action best = get_best_action(data);
    printf("\n  Best Action: %s (highest regret)\n", action_to_display_string(best));
}

/* Query command - look up specific hand strategies */
int cmd_query(int argc, char** argv) {
    const char* hand_str = NULL;
    const char* board_str = NULL;
    const char* history_str = NULL;
    const char* street_str = NULL;
    const char* strategy_file = "data/final_strategy.dat";
    
    /* Parse query arguments */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--hand") == 0 && i + 1 < argc) {
            hand_str = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) {
            board_str = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--history") == 0 && i + 1 < argc) {
            history_str = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--street") == 0 && i + 1 < argc) {
            street_str = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            strategy_file = argv[i + 1];
            i++;
        }
    }
    
    /* Validate required arguments */
    if (!hand_str) {
        fprintf(stderr, "Error: --hand is required\n");
        fprintf(stderr, "Usage: %s query --hand AsKs [--board Kh7d2c] [--history b33c]\n", argv[0]);
        return 1;
    }
    
    /* Parse hole cards */
    Card hole_cards[2];
    if (parse_hand(hand_str, hole_cards, 2) != 2) {
        fprintf(stderr, "Error: Invalid hand '%s'. Use format like 'AsKs' or '7h2d'\n", hand_str);
        return 1;
    }
    
    /* Sort hole cards for canonical representation */
    if (hole_cards[0] > hole_cards[1]) {
        Card tmp = hole_cards[0];
        hole_cards[0] = hole_cards[1];
        hole_cards[1] = tmp;
    }
    
    /* Parse board */
    Card board_cards[5];
    int num_board = 0;
    if (board_str) {
        num_board = parse_board(board_str, board_cards, 5);
        if (num_board < 0) {
            fprintf(stderr, "Error: Invalid board '%s'\n", board_str);
            return 1;
        }
    }
    
    /* Determine street */
    Street street;
    if (street_str) {
        if (strcmp(street_str, "preflop") == 0) street = STREET_PREFLOP;
        else if (strcmp(street_str, "flop") == 0) street = STREET_FLOP;
        else if (strcmp(street_str, "turn") == 0) street = STREET_TURN;
        else if (strcmp(street_str, "river") == 0) street = STREET_RIVER;
        else {
            fprintf(stderr, "Error: Invalid street '%s'. Use: preflop, flop, turn, river\n", street_str);
            return 1;
        }
    } else {
        /* Auto-detect from board */
        switch (num_board) {
            case 0: street = STREET_PREFLOP; break;
            case 3: street = STREET_FLOP; break;
            case 4: street = STREET_TURN; break;
            case 5: street = STREET_RIVER; break;
            default: 
                fprintf(stderr, "Error: Board must have 0, 3, 4, or 5 cards\n");
                return 1;
        }
    }
    
    /* Initialize evaluator */
    evaluator_init(NULL);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         Strategy Query                                   ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    /* Display query */
    printf("Query:\n");
    char card1[3], card2[3];
    card_to_string(hole_cards[0], card1);
    card_to_string(hole_cards[1], card2);
    printf("  Hand:   %s %s\n", card1, card2);
    
    if (num_board > 0) {
        printf("  Board:  ");
        for (int i = 0; i < num_board; i++) {
            char c[3];
            card_to_string(board_cards[i], c);
            printf("%s ", c);
        }
        printf("\n");
    }
    
    printf("  Street: %s\n", street_to_string(street));
    
    if (history_str) {
        printf("  History: %s\n", history_str);
    }
    
    /* Solve this specific hand in real-time */
    printf("\n");
    printf("Solving this specific hand (10000 iterations)...\n");
    printf("\n");
    
    /* Create MCCFR state for quick solve */
    MCCFRConfig config = {
        .num_iterations = 10000,  /* More iterations for better coverage */
        .checkpoint_interval = 1000000,  /* No checkpoints */
        .print_interval = 1000,
        .epsilon = 0.0,
        .use_pruning = true
    };
    
    MCCFRState* solve_state = mccfr_create(&config);
    if (!solve_state) {
        fprintf(stderr, "Error: Failed to create solver state\n");
        evaluator_cleanup();
        return 1;
    }
    
    /* Run multiple iterations on this specific hand */
    for (int iter = 0; iter < 10000; iter++) {
        /* Create game with specific cards */
        Card full_board[5] = {0};
        memcpy(full_board, board_cards, num_board * sizeof(Card));
        
        /* Need opponent cards - sample random ones that don't conflict */
        Deck deck;
        deck_init(&deck);
        deck_shuffle(&deck);  /* Important: shuffle before dealing */
        deck_remove_cards(&deck, hole_cards, 2);
        deck_remove_cards(&deck, board_cards, num_board);
        
        /* Make sure we have enough cards */
        if (deck.num_cards < 2) {
            fprintf(stderr, "Error: Not enough cards in deck\n");
            break;
        }
        
        Card oop_cards[2];
        oop_cards[0] = deck_deal(&deck);
        oop_cards[1] = deck_deal(&deck);
        
        /* Train IP */
        GameState game_ip;
        memset(&game_ip, 0, sizeof(GameState));  /* Clear state first */
        game_init(&game_ip, hole_cards, oop_cards, full_board, num_board);
        mccfr_traverse(solve_state, &game_ip, PLAYER_IP, 1.0);
        
        /* Train OOP */
        GameState game_oop;
        memset(&game_oop, 0, sizeof(GameState));  /* Clear state first */
        game_init(&game_oop, hole_cards, oop_cards, full_board, num_board);
        mccfr_traverse(solve_state, &game_oop, PLAYER_OOP, 1.0);
        
        solve_state->current_iteration++;
    }
    
    printf("Solve complete! Analyzing strategies...\n\n");
    
    /* Debug: show table stats and sample keys */
    printf("Table entries: %zu\n", strategy_table_count(solve_state->regret_table));
    
    /* Show first few entries to understand the hash distribution */
    int shown = 0;
    for (size_t i = 0; i < solve_state->regret_table->capacity && shown < 5; i++) {
        if (solve_state->regret_table->entries[i].occupied) {
            printf("  Sample key %d: 0x%016lx (visits: %lu)\n", 
                   shown + 1,
                   solve_state->regret_table->entries[i].key,
                   solve_state->regret_table->entries[i].visit_count);
            shown++;
        }
    }
    
    /* Display strategies for both players at this spot */
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         Solved Strategy (IP - In Position)               ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    /* Create a game state to get the info set key for IP */
    Card dummy_oop[2] = {0, 1};  /* Placeholder */
    GameState query_state;
    memset(&query_state, 0, sizeof(GameState));
    game_init(&query_state, hole_cards, dummy_oop, board_cards, num_board);
    
    /* Override street if specified */
    if (street_str) {
        query_state.street = street;
    }
    
    query_state.current_player = PLAYER_IP;
    if (history_str) {
        query_state.action_history = parse_history(history_str);
        query_state.history_depth = strlen(history_str);
    }
    
    uint64_t ip_key = game_get_info_set_hash(&query_state);
    printf("IP Info Set Key: 0x%016lx\n", ip_key);
    display_strategy(solve_state->regret_table, ip_key);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         Solved Strategy (OOP - Out of Position)          ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    /* Get OOP info set key */
    query_state.current_player = PLAYER_OOP;
    game_init(&query_state, dummy_oop, hole_cards, board_cards, num_board);
    query_state.street = street;
    if (history_str) {
        query_state.action_history = parse_history(history_str);
        query_state.history_depth = strlen(history_str);
    }
    
    uint64_t oop_key = game_get_info_set_hash(&query_state);
    display_strategy(solve_state->regret_table, oop_key);
    
    /* Cleanup */
    mccfr_free(solve_state);
    evaluator_cleanup();
    
    printf("\n");
    printf("Note: This is a quick solve (1000 iterations).\n");
    printf("For more accurate results, run full training: ./poker_solver -i 100000\n");
    
    return 0;
}

/* Generate the hand evaluation lookup table */
int cmd_generate_table(int argc, char** argv) {
    const char* output_file = (argc > 2) ? argv[2] : "data/hand_ranks.dat";
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║    Generating Hand Evaluation Lookup Table               ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("This will create a ~125MB lookup table at:\n");
    printf("  %s\n", output_file);
    printf("\n");
    printf("Note: This process takes 5-10 minutes and only needs to be\n");
    printf("done once. The table enables fast hand evaluation during\n");
    printf("MCCFR training.\n");
    printf("\n");
    
    /* Import and run the table generator */
    /* Since generate_table is a separate program, we'll just call it */
    printf("Running table generator...\n\n");
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "./generate_table %s", output_file);
    int result = system(cmd);
    
    return result;
}

/* Main training function */
int cmd_train(int argc, char** argv) {
    /* Parse arguments */
    int num_iterations = 1000000;
    int checkpoint_interval = 100000;
    const char* checkpoint_file = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            num_iterations = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            checkpoint_interval = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "resume") == 0 && i + 1 < argc) {
            checkpoint_file = argv[i + 1];
            i++;
        }
    }
    
    /* Initialize evaluator (simple version, no table needed) */
    printf("Initializing hand evaluator...\n");
    evaluator_init(NULL);  /* Simple evaluator doesn't need a table */
    printf("Hand evaluator initialized.\n\n");
    
    /* Create MCCFR configuration */
    MCCFRConfig config = {
        .num_iterations = num_iterations,
        .checkpoint_interval = checkpoint_interval,
        .print_interval = 1000,
        .epsilon = 0.0,
        .use_pruning = true
    };
    
    /* Create MCCFR state */
    g_mccfr_state = mccfr_create(&config);
    if (!g_mccfr_state) {
        fprintf(stderr, "Error: Failed to create MCCFR state\n");
        evaluator_cleanup();
        return 1;
    }
    
    /* Load checkpoint if specified */
    if (checkpoint_file) {
        printf("Loading checkpoint: %s\n", checkpoint_file);
        if (mccfr_load_checkpoint(g_mccfr_state, checkpoint_file) != 0) {
            fprintf(stderr, "Warning: Failed to load checkpoint, starting fresh\n");
        } else {
            printf("Checkpoint loaded. Resuming from iteration %d\n", 
                   g_mccfr_state->current_iteration);
        }
    }
    
    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Run training */
    mccfr_train(g_mccfr_state);
    
    /* Save final strategy */
    printf("\nSaving final strategy...\n");
    mccfr_export_strategy(g_mccfr_state, "data/final_strategy.dat");
    
    /* Cleanup */
    mccfr_free(g_mccfr_state);
    evaluator_cleanup();
    
    printf("\nDone!\n");
    return 0;
}

int main(int argc, char** argv) {
    /* Print banner */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         MCCFR Poker Solver v1.0                          ║\n");
    printf("║                                                          ║\n");
    printf("║  Monte Carlo Counterfactual Regret Minimization          ║\n");
    printf("║  For Learning Nash Equilibrium in Heads-Up Poker         ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    /* Check for command */
    if (argc > 1) {
        if (strcmp(argv[1], "generate_table") == 0) {
            return cmd_generate_table(argc, argv);
        } else if (strcmp(argv[1], "query") == 0) {
            return cmd_query(argc, argv);
        } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    /* Default: run training */
    return cmd_train(argc, argv);
}
