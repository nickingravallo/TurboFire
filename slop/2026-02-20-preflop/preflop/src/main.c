/*
 * TurboFire Preflop Solver TUI
 */

#define _POSIX_C_SOURCE 200809L
#include "../include/preflop_solver.h"
#include "../include/gto_solver.h"
#include "../include/range.h"
#include "../include/ranks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <time.h>

#define STATUS_LEN 512
#define BOARD_STR_LEN 32
#define MAX_COMBOS 12
#define STATUS_ROWS 6
static const char SUITS[] = "shdc";

/* Preflop Actions */
/* 
   We map standard actions:
   SB (First): Fold(0), Call(1), Raise(2), Raise(3), Raise(4)
   BB (Facing Limp): Check(0), Raise(1), Raise(2), Raise(3) -> Mapped to Bet1/2/3?
   Actually logic in gto_solver.c:
   Facing Bet: Fold(0), Call(1), Raise(2..4).
   Not Facing Bet: Check(0), Bet(1..3).
*/

static const char *const ACTION_LABELS_FACING_BET[] = { "Fold", "Call", "R 100%", "R 200%", "R 500%" };
static const char *const ACTION_LABELS_NOT_FACING[] = { "Check", "B 75%", "B 150%", "B 300%" };

static PreflopSolver g_ps;
static int g_cursor_row, g_cursor_col;
static uint64_t g_current_history;
static int g_current_num_actions;
static int g_view_oop; /* 1 = P1/OOP, 0 = P2/IP */
static int g_solving;
static int g_merging;
static int g_merge_current;
static int g_merge_total;
static int g_iterations = 1000000;
static int g_has_colors;
static int g_solve_done_iters;
static int g_solve_target_iters;
static double g_solve_elapsed_sec;

static const char *p1_label(void) {
	return (g_ps.game_mode == PREFLOP_MODE_BTN_SB) ? "BTN" : "SB";
}
static const char *p2_label(void) {
	return (g_ps.game_mode == PREFLOP_MODE_BTN_SB) ? "SB" : "BB";
}

static double now_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int card_to_rank_suit(uint64_t card, int *out_rank, int *out_suit) {
	if (!card || !out_rank || !out_suit) return 0;
	for (int i = 0; i < 64; i++) {
		if ((card & (1ULL << i)) != 0) {
			*out_rank = i % 16;
			*out_suit = i / 16;
			return 1;
		}
	}
	return 0;
}

static void hand_bitmask_to_string(uint64_t c1, uint64_t c2, char *buf, size_t out_sz) {
	int r1, s1, r2, s2;
	if (out_sz < 6 || !card_to_rank_suit(c1, &r1, &s1) || !card_to_rank_suit(c2, &r2, &s2)) {
		if (out_sz) buf[0] = '\0';
		return;
	}
	static const char R[] = "23456789TJQKA";
	if (r1 >= r2)
		snprintf(buf, out_sz, "%c%c%c%c", R[r1], SUITS[s1], R[r2], SUITS[s2]);
	else
		snprintf(buf, out_sz, "%c%c%c%c", R[r2], SUITS[s2], R[r1], SUITS[s1]);
}

static void print_line_clipped(WINDOW *win, int row, const char *text) {
	int h, w;
	(void)h;
	getmaxyx(win, h, w);
	wmove(win, row, 0);
	wclrtoeol(win);
	if (!text) return;
	if (w <= 0) return;
	waddnstr(win, text, w - 1);
}

static int action_color_pair(int facing_bet, int action_idx) {
	if (facing_bet) {
		if (action_idx == 0) return 2; // Fold Blue
		if (action_idx == 1) return 3; // Call Green
		return 6; // Raise Red
	}
	if (action_idx == 0) return 3; // Check Green
	return 6; // Bet Red
}

static int action_idx_to_bg_pair(int facing_bet, int action_idx) {
	if (facing_bet) {
		if (action_idx == 0) return 7; // Fold
		if (action_idx == 1) return 8; // Call
		return 11; // Raise
	}
	if (action_idx == 0) return 8; // Check
	return 11; // Bet
}

static void draw_action_breakdown_line(
	WINDOW *win, int row, const char *prefix, int facing_bet,
	const char *const *labels, const float probs[PREFLOP_MAX_ACTIONS], int n_actions
) {
	int h, w;
	int x = 0;
	(void)h;
	getmaxyx(win, h, w);
	wmove(win, row, 0);
	wclrtoeol(win);

	if (w <= 0) return;
	if (!prefix) prefix = "";
	{
		size_t prefix_len = strlen(prefix);
		int add_len = (int)prefix_len;
		if (add_len > w - 1) add_len = w - 1;
		if (add_len > 0) {
			mvwaddnstr(win, row, x, prefix, add_len);
			x += add_len;
		}
	}
	for (int j = 0; j < n_actions && x < w - 1; j++) {
		char chunk[32];
		int n = snprintf(chunk, sizeof(chunk), "%-7s %5.1f%%   ", labels[j], probs[j] * 100.0f);
		if (n <= 0) continue;
		if (g_has_colors) wattron(win, COLOR_PAIR(action_color_pair(facing_bet, j)) | A_BOLD);
		mvwaddnstr(win, row, x, chunk, w - 1 - x);
		if (g_has_colors) wattroff(win, COLOR_PAIR(action_color_pair(facing_bet, j)) | A_BOLD);
		x += n;
	}
}


static void redraw_grid(WINDOW *win) {
	int r, c;
	char hand_str[8];
	float probs[PREFLOP_MAX_ACTIONS];
	GameState state;
	const float (*weights)[PREFLOP_GRID_SIZE];
	int n_actions;
	
    preflop_solver_get_state_at_history(&g_ps, g_current_history, g_current_num_actions, &state);
	weights = g_view_oop ? g_ps.oop_weights : g_ps.ip_weights;
    
	wclear(win);
	mvwprintw(win, 0, 0, "    ");
	for (c = 0; c < PREFLOP_GRID_SIZE; c++) {
		char lab = RANKS_STR[12 - c];
		mvwprintw(win, 0, 4 + c * 4, " %c ", lab);
	}
	wprintw(win, "\n");

	for (r = 0; r < PREFLOP_GRID_SIZE; r++) {
		char row_lab = (r == 0) ? RANKS_STR[12] : (r < PREFLOP_GRID_SIZE ? RANKS_STR[12 - r] : '?');
		mvwprintw(win, r + 1, 0, " %c ", row_lab);
		for (c = 0; c < PREFLOP_GRID_SIZE; c++) {
			hand_at(r, c, hand_str, sizeof(hand_str));
			int in_range = (weights[r][c] > 0.0f);
			int is_cursor = (r == g_cursor_row && c == g_cursor_col);
			int viewing_acting = (g_view_oop && state.active_player == P1) || (!g_view_oop && state.active_player == P2);

			if (is_cursor) {
				wattron(win, A_REVERSE | A_BOLD);
				wprintw(win, "%-3s ", hand_str);
				wattroff(win, A_REVERSE | A_BOLD);
			} else if (in_range && g_ps.solved && viewing_acting &&
				preflop_solver_get_strategy_at_history(&g_ps, g_current_history, g_current_num_actions, r, c, probs, &n_actions) == 0) {
				if (g_has_colors && n_actions > 0) {
					float cum[PREFLOP_MAX_ACTIONS + 1];
					int seg_pair[4];
					cum[0] = 0.0f;
					for (int i = 0; i < n_actions; i++)
						cum[i + 1] = cum[i] + probs[i];
					for (int s = 0; s < 4; s++) {
						float q = (s + 0.5f) / 4.0f;
						int k = 0;
						while (k < n_actions - 1 && q >= cum[k + 1])
							k++;
                        
                        // Check if we need to map action index differently?
                        // probs are 0..n_actions-1.
						seg_pair[s] = action_idx_to_bg_pair(state.facing_bet || (state.street==STREET_PREFLOP && state.to_call > 0), k);
					}
					for (int s = 0; s < 4; s++) {
						char ch = (s < 3 && hand_str[s] != '\0') ? hand_str[s] : ' ';
						wattron(win, COLOR_PAIR(seg_pair[s]));
						waddch(win, (chtype)(unsigned char)ch);
						wattroff(win, COLOR_PAIR(seg_pair[s]));
					}
				} else {
					wprintw(win, "%-3s ", hand_str);
				}
				wattrset(win, A_NORMAL);
			} else if (in_range) {
				wprintw(win, "%-3s ", hand_str);
			} else {
				wprintw(win, " .  ");
			}
		}
		wprintw(win, "\n");
	}
	wrefresh(win);
}

static void redraw_status(WINDOW *win) {
	char line1[STATUS_LEN];
	char line2[STATUS_LEN];
	char controls[STATUS_LEN];
	char hand_str[8];
	float probs[PREFLOP_MAX_ACTIONS];
	uint64_t c1[MAX_COMBOS], c2[MAX_COMBOS];
	GameState state;
	int n_actions = 6;
	const char *const *labels;
	int row = 0;

	hand_at(g_cursor_row, g_cursor_col, hand_str, sizeof(hand_str));
	int is_suited = (g_cursor_row < g_cursor_col);

	preflop_solver_get_state_at_history(&g_ps, g_current_history, g_current_num_actions, &state);
	snprintf(controls, sizeof(controls), " v view   b back   0-4 act  |  S solve  e export  |  q quit ");
	werase(win);

    bool facing_bet = state.facing_bet || (state.street == STREET_PREFLOP && state.to_call > 0);

	if (g_solving) {
		double pct;
		int pct_int, bar_width = 50, filled;
		char bar[64];
		if (g_merging && g_merge_total > 0) {
			pct = (double)g_merge_current / (double)g_merge_total;
		} else {
			pct = (g_solve_target_iters > 0)
				? (double)g_solve_done_iters / (double)g_solve_target_iters
				: 0.0;
		}
		pct_int = (int)(pct * 100.0 + 0.5);
		filled = (int)(pct * bar_width + 0.5);
		if (filled > bar_width) filled = bar_width;
		for (int i = 0; i < bar_width; i++) bar[i] = (i < filled) ? '#' : '-';
		bar[bar_width] = '\0';
		
		snprintf(line1, sizeof(line1), " Preflop Solver | %s | Pot %.2fbb  %s %.2fbb  %s %.2fbb ",
			hand_str, state.pot, p1_label(), state.p1_stack, p2_label(), state.p2_stack);
		snprintf(line2, sizeof(line2), " Solving [%s] %3d%%  iters %d ", bar, pct_int, g_solve_done_iters);
		
		print_line_clipped(win, row++, line1);
		print_line_clipped(win, row++, line2);
		print_line_clipped(win, row++, controls);
	} else if (state.is_terminal) {
		snprintf(line1, sizeof(line1), " Terminal Node | Pot %.2f bb ", state.pot);
		print_line_clipped(win, row++, line1);
		snprintf(line2, sizeof(line2), "  %s %.2f bb  |  %s %.2f bb  ", p1_label(), state.p1_stack, p2_label(), state.p2_stack);
		print_line_clipped(win, row++, line2);
		snprintf(controls, sizeof(controls), " b back ");
		print_line_clipped(win, row++, controls);
	} else if (g_ps.solved) {
		if (preflop_solver_get_strategy_at_history(&g_ps, g_current_history, g_current_num_actions,
			g_cursor_row, g_cursor_col, probs, &n_actions) == 0) {
			labels = facing_bet ? ACTION_LABELS_FACING_BET : ACTION_LABELS_NOT_FACING;
			snprintf(line1, sizeof(line1), " Action: %s | %s | Pot %.2fbb  %s %.2fbb  %s %.2fbb ",
				state.active_player == P1 ? p1_label() : p2_label(), hand_str, state.pot, p1_label(), state.p1_stack, p2_label(), state.p2_stack);
			print_line_clipped(win, row++, line1);
			if (is_suited) {
				int n = hand_string_to_combos(hand_str, 0, c1, c2, MAX_COMBOS);
				for (int i = 0; i < n && row < STATUS_ROWS - 1; i++) {
					char combo_str[8];
					float p[PREFLOP_MAX_ACTIONS];
					char prefix[32];
					hand_bitmask_to_string(c1[i], c2[i], combo_str, sizeof(combo_str));
					if (preflop_solver_get_hand_strategy(
						g_ps.game_mode, g_current_history, g_current_num_actions, c1[i] | c2[i], p
					) == 0) {
						snprintf(prefix, sizeof(prefix), " %s ", combo_str);
						draw_action_breakdown_line(win, row++, prefix, facing_bet, labels, p, n_actions);
					}
				}
			} else {
				draw_action_breakdown_line(win, row++, " ", facing_bet, labels, probs, n_actions);
			}
			print_line_clipped(win, row++, controls);
		} else {
            // Not in range or no data
			snprintf(line1, sizeof(line1), " %s | Pot %.2fbb | (no data) ", hand_str, state.pot);
			print_line_clipped(win, row++, line1);
			print_line_clipped(win, row++, controls);
		}
	} else {
		snprintf(line1, sizeof(line1), " Ready to Solve. Press S. ");
		print_line_clipped(win, row++, line1);
		print_line_clipped(win, row++, controls);
	}
	wrefresh(win);
}

static void on_before_merge(void *user) {
	g_merging = 1;
	g_merge_current = 0;
	g_merge_total = 0;
	WINDOW *status_win = (WINDOW *)user;
	if (status_win) {
		redraw_status(status_win);
		wrefresh(status_win);
	}
}

static void on_merge_progress(void *user, int current, int total) {
	g_merge_current = current;
	g_merge_total = total;
	WINDOW *status_win = (WINDOW *)user;
	if (status_win) {
		redraw_status(status_win);
		wrefresh(status_win);
	}
}

int main(int argc, char **argv) {
    int user_iters = 0;
    int mode = PREFLOP_MODE_BTN_SB;
    int opt;
    while ((opt = getopt(argc, argv, "i:m:")) != -1) {
        if (opt == 'i') user_iters = atoi(optarg);
        else if (opt == 'm') {
            if (strcmp(optarg, "hu") == 0)
                mode = PREFLOP_MODE_HU;
            else if (strcmp(optarg, "btnsb") == 0)
                mode = PREFLOP_MODE_BTN_SB;
        }
    }

	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	g_has_colors = has_colors() ? 1 : 0;
	if (g_has_colors) {
		start_color();
		init_pair(1, COLOR_WHITE, COLOR_BLACK);
		init_pair(2, COLOR_BLUE, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_RED, COLOR_BLACK);
		init_pair(5, COLOR_RED, COLOR_BLACK);
		init_pair(6, COLOR_RED, COLOR_BLACK);
		init_pair(7, COLOR_WHITE, COLOR_BLUE);
		init_pair(8, COLOR_WHITE, COLOR_GREEN);
		init_pair(9, COLOR_WHITE, COLOR_RED);
		init_pair(10, COLOR_WHITE, COLOR_RED);
		init_pair(11, COLOR_WHITE, COLOR_RED);
	}

	g_cursor_row = 0;
	g_cursor_col = 0;
	g_current_history = 0;
	g_current_num_actions = 0;
	g_view_oop = 1;
	g_solving = 0;

    memset(&g_ps, 0, sizeof(g_ps));
    g_ps.game_mode = mode;

    // Default 100% ranges
	for (int r = 0; r < PREFLOP_GRID_SIZE; r++) {
		for (int c = 0; c < PREFLOP_GRID_SIZE; c++) {
			g_ps.oop_weights[r][c] = 1.0f;
			g_ps.ip_weights[r][c] = 1.0f;
		}
    }

	init_gto_table();
    init_rank_map();
    init_flush_map();

	WINDOW *grid_win = newwin(PREFLOP_GRID_SIZE + 2, 4 + PREFLOP_GRID_SIZE * 4, 0, 0);
	WINDOW *status_win = newwin(STATUS_ROWS, COLS, PREFLOP_GRID_SIZE + 2, 0);

    if (user_iters > 0) g_iterations = user_iters;

	for (;;) {
		redraw_grid(grid_win);
		redraw_status(status_win);
		refresh();

		int ch = getch();
		GameState state;
		if (ch == 'q' || ch == 'Q' || ch == 27) break;
		if (ch == 'v' || ch == 'V' || ch == '\t') {
			g_view_oop = 1 - g_view_oop;
		} else if (ch == 'b' || ch == 'B' || ch == KEY_BACKSPACE) {
			if (g_current_num_actions >= 1) {
				g_current_history &= (1ULL << ((g_current_num_actions - 1) * BITS_PER_ACTION)) - 1;
				g_current_num_actions--;
				preflop_solver_get_state_at_history(&g_ps, g_current_history, g_current_num_actions, &state);
				g_view_oop = (state.active_player == P1) ? 1 : 0;
			}
		} else if (ch >= '0' && ch <= '4') {
			preflop_solver_get_state_at_history(&g_ps, g_current_history, g_current_num_actions, &state);
			if (!state.is_terminal) {
				int a = ch - '0';
				g_current_history |= ((uint64_t)(a & 7) << (g_current_num_actions * BITS_PER_ACTION));
				g_current_num_actions++;
				preflop_solver_get_state_at_history(&g_ps, g_current_history, g_current_num_actions, &state);
				g_view_oop = (state.active_player == P1) ? 1 : 0;
			}
		} else if (ch == KEY_LEFT) g_cursor_col = (g_cursor_col > 0) ? g_cursor_col - 1 : 0;
		else if (ch == KEY_RIGHT) g_cursor_col = (g_cursor_col < PREFLOP_GRID_SIZE - 1) ? g_cursor_col + 1 : PREFLOP_GRID_SIZE - 1;
		else if (ch == KEY_UP) g_cursor_row = (g_cursor_row > 0) ? g_cursor_row - 1 : 0;
		else if (ch == KEY_DOWN) g_cursor_row = (g_cursor_row < PREFLOP_GRID_SIZE - 1) ? g_cursor_row + 1 : PREFLOP_GRID_SIZE - 1;
		else if (ch == 'S' || ch == 's') {
			if (!g_solving && !g_ps.solved) {
				g_solving = 1;
				g_merging = 0;
				g_ps.before_merge_cb = on_before_merge;
				g_ps.before_merge_user = status_win;
				g_ps.merge_progress_cb = on_merge_progress;
				g_ps.merge_progress_user = status_win;
				g_solve_done_iters = 0;
				g_solve_target_iters = g_iterations;
                
                double start = now_seconds();
				preflop_solver_begin_parallel_solve(&g_ps);
                
				const int chunk = (g_iterations + 99) / 100;
                int done = 0;
				while (done < g_iterations) {
					int n = chunk;
                    if (n > g_iterations - done) n = g_iterations - done;
					preflop_solver_solve(&g_ps, n);
                    done += n;
                    g_solve_done_iters = done;
                    g_solve_elapsed_sec = now_seconds() - start;
					redraw_status(status_win);
				}
				preflop_solver_end_parallel_solve(&g_ps);
				g_solving = 0;
			}
		} else if (ch == 'e' || ch == 'E') {
			if (g_ps.solved) {
				float range_grid[PREFLOP_GRID_SIZE][PREFLOP_GRID_SIZE];
				preflop_solver_get_state_at_history(&g_ps, g_current_history, g_current_num_actions, &state);
				if (!state.is_terminal &&
					preflop_solver_export_range_grid(&g_ps, g_current_history, g_current_num_actions, range_grid) == 0) {
					const char *player = (state.active_player == P1) ? p1_label() : p2_label();
					char filename[128];
					snprintf(filename, sizeof(filename), "%s_range_node%d.json", player, g_current_num_actions);
					char desc[128];
					snprintf(desc, sizeof(desc), "%s range at action depth %d", player, g_current_num_actions);
					if (preflop_solver_save_range_json(filename, player, desc, range_grid) == 0) {
						print_line_clipped(status_win, STATUS_ROWS - 1, "");
						mvwprintw(status_win, STATUS_ROWS - 1, 0, " Saved to %s", filename);
						wrefresh(status_win);
						napms(1500);
					}
				}
			}
		}
	}

	delwin(grid_win);
	delwin(status_win);
	endwin();
	return 0;
}
