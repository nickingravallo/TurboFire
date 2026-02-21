/*
 * TurboFire solver TUI: 13x13 grid, highlight hand for action breakdown, traverse streets.
 * Usage: turbofire [-i iters] <oop_range.json> <ip_range.json> <board>
 */

#define _POSIX_C_SOURCE 200809L
#include "flop_solver.h"
#include "gto_solver.h"
#include "range.h"
#include "ranks.h"
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
static const char SUITS[] = "shdc";  /* suit 0=S, 1=H, 2=D, 3=C */

/* OOP action labels (4): Check, Bet33, Bet52, Bet100 */
static const char *const OOP_ACTION_LABELS[] = { "Check", "Bet33%", "Bet52%", "Bet100%" };
/* IP facing bet (5): Fold, Call, R33, R52, R100 */
static const char *const IP_BET_ACTION_LABELS[] = { "Fold", "Call", "R33%", "R52%", "R100%" };

static FlopSolver g_fs;
static int g_cursor_row, g_cursor_col;
static uint64_t g_current_history;   /* 0 = OOP to act; else encoded action path */
static int g_current_num_actions;   /* 0 = root, 1 = after one OOP action, etc. */
static int g_view_oop;              /* 1 = viewing OOP range/strategy, 0 = viewing IP */
static char g_oop_name[64];
static char g_ip_name[64];
static int g_solving;
static int g_merging;  /* 1 = inside merge phase (workers done, merging into global table) */
static int g_merge_current;  /* merge step (0..g_merge_total) */
static int g_merge_total;    /* total merge steps (thread count) */
static int g_iterations = 10000000;
static int g_has_colors;
static int g_solve_done_iters;
static int g_solve_target_iters;
static double g_solve_elapsed_sec;

static double now_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Decode one card bitmask into rank (0-12) and suit (0-3). Return 0 if invalid. */
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

/* Format hand (c1|c2) as "AsKs" into buf. High rank first. */
static void hand_bitmask_to_string(uint64_t c1, uint64_t c2, char *buf, size_t out_sz) {
	int r1, s1, r2, s2;
	if (out_sz < 6 || !card_to_rank_suit(c1, &r1, &s1) || !card_to_rank_suit(c2, &r2, &s2)) {
		if (out_sz) buf[0] = '\0';
		return;
	}
	if (r1 >= r2)
		snprintf(buf, out_sz, "%c%c%c%c", RANKS_STR[r1], SUITS[s1], RANKS_STR[r2], SUITS[s2]);
	else
		snprintf(buf, out_sz, "%c%c%c%c", RANKS_STR[r2], SUITS[s2], RANKS_STR[r1], SUITS[s1]);
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

/* Board to show for current street: flop + turn (if reached) + river (if reached). */
static uint64_t display_board_for_street(const GameState *state) {
	uint64_t b = g_fs.board;
	if (state->street >= STREET_TURN && g_fs.preset_turn_card)
		b |= g_fs.preset_turn_card;
	if (state->street >= STREET_RIVER && g_fs.preset_river_card)
		b |= g_fs.preset_river_card;
	return b;
}

/* Render board bitmask as spaced cards (e.g. "Kd 7h 2s"). */
static void format_board_for_status(uint64_t board_mask, char *board_out, size_t out_sz) {
	size_t out_i = 0;

	if (!board_out || out_sz == 0) return;
	if (!board_mask) {
		snprintf(board_out, out_sz, "(no board)");
		return;
	}

	for (int rank = 12; rank >= 0 && out_i + 1 < out_sz; rank--) {
		for (int suit = 0; suit < 4 && out_i + 1 < out_sz; suit++) {
			uint64_t card = range_make_card(rank, suit);
			if (!(board_mask & card))
				continue;
			if (out_i > 0 && out_i + 1 < out_sz)
				board_out[out_i++] = ' ';
			if (out_i + 1 < out_sz)
				board_out[out_i++] = RANKS_STR[rank];
			if (out_i + 1 < out_sz)
				board_out[out_i++] = SUITS[suit];
		}
	}
	if (out_i == 0) {
		snprintf(board_out, out_sz, "(no board)");
		return;
	}
	board_out[out_i] = '\0';
}

static int action_color_pair(int facing_bet, int action_idx) {
	if (facing_bet) {
		/* Fold blue, call green, R33/R52/R100 light/medium/dark red. */
		if (action_idx == 0) return 2;
		if (action_idx == 1) return 3;
		if (action_idx == 2) return 4;
		if (action_idx == 3) return 5;
		return 6;
	}
	/* Check green; Bet33/52/100 are light/medium/dark red. */
	if (action_idx == 0) return 3;  /* Check */
	if (action_idx == 1) return 4;  /* Bet33 */
	if (action_idx == 2) return 5;  /* Bet52 */
	return 6;                       /* Bet100 */
}

static int action_text_attr(int facing_bet, int action_idx) {
	(void)facing_bet;
	(void)action_idx;
	return A_BOLD;
}

/* Background pair (7-11) for cell segment by action. */
static int action_idx_to_bg_pair(int facing_bet, int action_idx) {
	if (facing_bet) {
		if (action_idx == 0) return 7;
		if (action_idx == 1) return 8;
		if (action_idx == 2) return 9;
		if (action_idx == 3) return 10;
		return 11;
	}
	/* Check green; Bet33/52/100 are light/medium/dark red. */
	if (action_idx == 0) return 8;   /* Check */
	if (action_idx == 1) return 9;   /* Bet33 */
	if (action_idx == 2) return 10;  /* Bet52 */
	return 11;                       /* Bet100 */
}

static void draw_action_breakdown_line(
	WINDOW *win, int row, const char *prefix, int facing_bet,
	const char *const *labels, const float probs[FLOP_MAX_ACTIONS], int n_actions
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
		if (g_has_colors) wattron(win, COLOR_PAIR(action_color_pair(facing_bet, j)) | action_text_attr(facing_bet, j));
		mvwaddnstr(win, row, x, chunk, w - 1 - x);
		if (g_has_colors) wattroff(win, COLOR_PAIR(action_color_pair(facing_bet, j)) | action_text_attr(facing_bet, j));
		x += n;
	}
}

static void redraw_grid(WINDOW *win) {
	int r, c;
	char hand_str[8];
	float probs[FLOP_MAX_ACTIONS];
	GameState state;
	const float (*weights)[GRID_SIZE];
	int n_actions;
	flop_solver_get_state_at_history(&g_fs, g_current_history, g_current_num_actions, &state);
	weights = g_view_oop ? g_fs.oop_weights : g_fs.ip_weights;
	wclear(win);
	mvwprintw(win, 0, 0, "    ");
	for (c = 0; c < GRID_SIZE; c++) {
		char lab = RANKS_STR[12 - c];
		mvwprintw(win, 0, 4 + c * 4, " %c ", lab);
	}
	wprintw(win, "\n");

	for (r = 0; r < GRID_SIZE; r++) {
		char row_lab = (r == 0) ? RANKS_STR[12] : (r < GRID_SIZE ? RANKS_STR[12 - r] : '?');
		mvwprintw(win, r + 1, 0, " %c ", row_lab);
		for (c = 0; c < GRID_SIZE; c++) {
			hand_at(r, c, hand_str, sizeof(hand_str));
			int in_range = (weights[r][c] > 0.0f);
			int is_cursor = (r == g_cursor_row && c == g_cursor_col);
			int viewing_acting = (g_view_oop && state.active_player == P1) || (!g_view_oop && state.active_player == P2);

			if (is_cursor) {
				wattron(win, A_REVERSE | A_BOLD);
				wprintw(win, "%-3s ", hand_str);
				wattroff(win, A_REVERSE | A_BOLD);
			} else if (in_range && g_fs.solved && viewing_acting &&
				flop_solver_get_strategy_at_history(&g_fs, g_current_history, g_current_num_actions, r, c, probs, &n_actions) == 0) {
				if (g_has_colors && n_actions > 0) {
					float cum[FLOP_MAX_ACTIONS + 1];
					int seg_pair[4];
					cum[0] = 0.0f;
					for (int i = 0; i < n_actions; i++)
						cum[i + 1] = cum[i] + probs[i];
					for (int s = 0; s < 4; s++) {
						float q = (s + 0.5f) / 4.0f;
						int k = 0;
						while (k < n_actions - 1 && q >= cum[k + 1])
							k++;
						seg_pair[s] = action_idx_to_bg_pair(state.facing_bet, k);
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
				wprintw(win, "%-3s ", hand_str);
			}
		}
		wprintw(win, "\n");
	}
	wrefresh(win);
}

static const char *const STREET_NAMES[] = { "Preflop", "Flop", "Turn", "River" };

static void format_path(char *buf, size_t sz) {
	GameState s;
	flop_solver_get_state_at_history(&g_fs, g_current_history, g_current_num_actions, &s);
	if (g_current_num_actions == 0) {
		snprintf(buf, sz, "%s: OOP to act", STREET_NAMES[s.street]);
		return;
	}
	if (g_current_num_actions == 1) {
		int a = (int)(g_current_history & 7u);
		if (a < 0) a = 0;
		if (a > 3) a = 3;
		snprintf(buf, sz, "OOP %s -> IP to act", OOP_ACTION_LABELS[a]);
		return;
	}
	/* Two or more actions: show street and who acts */
	snprintf(buf, sz, "%s: %s to act", STREET_NAMES[s.street],
		s.active_player == P1 ? "OOP" : "IP");
}

static void redraw_status(WINDOW *win) {
	char line1[STATUS_LEN];
	char line2[STATUS_LEN];
	char controls[STATUS_LEN];
	char path_buf[64];
	char board_disp[BOARD_STR_LEN * 2];
	char hand_str[8];
	float probs[FLOP_MAX_ACTIONS];
	uint64_t c1[MAX_COMBOS], c2[MAX_COMBOS];
	GameState state;
	int n_actions = 6;
	const char *const *labels;
	int row = 0;

	hand_at(g_cursor_row, g_cursor_col, hand_str, sizeof(hand_str));
	int is_suited = (g_cursor_row < g_cursor_col);

	flop_solver_get_state_at_history(&g_fs, g_current_history, g_current_num_actions, &state);
	format_path(path_buf, sizeof(path_buf));
	format_board_for_status(display_board_for_street(&state), board_disp, sizeof(board_disp));
	snprintf(controls, sizeof(controls), " v view   b back   0-4 act  |  wasd move  |  q quit ");
	werase(win);

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
		if (pct_int < 0) pct_int = 0;
		if (pct_int > 100) pct_int = 100;
		filled = (int)(pct * bar_width + 0.5);
		if (filled < 0) filled = 0;
		if (filled > bar_width) filled = bar_width;
		for (int i = 0; i < bar_width; i++) bar[i] = (i < filled) ? '#' : '-';
		bar[bar_width] = '\0';
		double eta = (g_solve_done_iters > 0 && !g_merging)
			? (g_solve_elapsed_sec * (double)(g_solve_target_iters - g_solve_done_iters) / (double)g_solve_done_iters)
			: -1.0;
		snprintf(line1, sizeof(line1), " %s | %s | %s | Pot %.2fbb  OOP %.2fbb  IP %.2fbb ",
			path_buf, board_disp, hand_str, state.pot, state.p1_stack, state.p2_stack);
		if (g_merging) {
			if (g_merge_total > 0 && g_merge_total <= 128)
				snprintf(line2, sizeof(line2),
					" Merging [%s] %3d%%  (%d/%d threads)  elapsed %.1fs  (merging results...) ",
					bar, pct_int, g_merge_current, g_merge_total, g_solve_elapsed_sec);
			else
				snprintf(line2, sizeof(line2),
					" Merging [%s] %3d%%  elapsed %.1fs  (merging results...) ",
					bar, pct_int, g_solve_elapsed_sec);
		} else if (eta >= 0.0) {
			snprintf(line2, sizeof(line2),
				" Solving [%s] %3d%%  (%d/%d)  elapsed %.1fs  ETA %.1fs ",
				bar, pct_int, g_solve_done_iters, g_solve_target_iters, g_solve_elapsed_sec, eta);
		} else {
			snprintf(line2, sizeof(line2),
				" Solving [%s] %3d%%  (%d/%d)  elapsed %.1fs  ETA -- ",
				bar, pct_int, g_solve_done_iters, g_solve_target_iters, g_solve_elapsed_sec);
		}
		print_line_clipped(win, row++, line1);
		print_line_clipped(win, row++, line2);
		print_line_clipped(win, row++, controls);
	} else if (state.is_terminal) {
		/* Showdown / hand over screen */
		const char *result_msg;
		if (state.last_action == 0 && state.facing_bet) {
			/* Fold: active_player is the one who folded */
			result_msg = (state.active_player == P1) ? "IP wins (OOP folded)" : "OOP wins (IP folded)";
		} else {
			result_msg = "Showdown";
		}
		snprintf(line1, sizeof(line1), "  Board: %s  |  Pot %.2f bb  ", board_disp, state.pot);
		print_line_clipped(win, row++, line1);
		if (g_has_colors) wattron(win, A_BOLD);
		snprintf(line2, sizeof(line2), "  *** %s ***  ", result_msg);
		print_line_clipped(win, row++, line2);
		if (g_has_colors) wattroff(win, A_BOLD);
		snprintf(line2, sizeof(line2), "  OOP %.2f bb  |  IP %.2f bb  ", state.p1_stack, state.p2_stack);
		print_line_clipped(win, row++, line2);
		snprintf(controls, sizeof(controls), " b back to previous action   q quit ");
		print_line_clipped(win, row++, controls);
	} else if (g_fs.solved) {
		if (flop_solver_get_strategy_at_history(&g_fs, g_current_history, g_current_num_actions,
			g_cursor_row, g_cursor_col, probs, &n_actions) == 0) {
			labels = state.facing_bet ? IP_BET_ACTION_LABELS : OOP_ACTION_LABELS;
			snprintf(line1, sizeof(line1), " %s | %s | %s | Pot %.2fbb  OOP %.2fbb  IP %.2fbb ",
				path_buf, board_disp, hand_str, state.pot, state.p1_stack, state.p2_stack);
			print_line_clipped(win, row++, line1);
			if (is_suited) {
				uint64_t current_board = display_board_for_street(&state);
				int n = hand_string_to_combos(hand_str, current_board, c1, c2, MAX_COMBOS);
				for (int i = 0; i < n && row < STATUS_ROWS - 1; i++) {
					char combo_str[8];
					float p[FLOP_MAX_ACTIONS];
					char prefix[32];
					hand_bitmask_to_string(c1[i], c2[i], combo_str, sizeof(combo_str));
					if (flop_solver_get_hand_strategy_with_runout(
						g_current_history, g_fs.board, g_fs.preset_turn_card, g_fs.preset_river_card,
						g_current_num_actions, c1[i] | c2[i], p
					) == 0) {
						snprintf(prefix, sizeof(prefix), " %s ", combo_str);
						draw_action_breakdown_line(win, row++, prefix, state.facing_bet, labels, p, n_actions);
					} else {
						print_line_clipped(win, row++, " (no combo data)");
					}
				}
			} else {
				draw_action_breakdown_line(win, row++, " ", state.facing_bet, labels, probs, n_actions);
			}
			print_line_clipped(win, row++, controls);
		} else {
			snprintf(line1, sizeof(line1), " %s | %s | %s | Pot %.2fbb  OOP %.2fbb  IP %.2fbb | (no data) ",
				path_buf, board_disp, hand_str, state.pot, state.p1_stack, state.p2_stack);
			print_line_clipped(win, row++, line1);
			print_line_clipped(win, row++, controls);
		}
	} else {
		snprintf(line1, sizeof(line1), " %s | %s | %s | Pot %.2fbb  OOP %.2fbb  IP %.2fbb | Not solved (S to solve) ",
			path_buf, board_disp, hand_str, state.pot, state.p1_stack, state.p2_stack);
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

static int run_tui(const char *oop_path, const char *ip_path, const char *board_str, uint64_t board, int board_cards) {
	(void)board_str;
	(void)board;
	(void)board_cards;
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	g_has_colors = has_colors() ? 1 : 0;
	if (g_has_colors) {
		start_color();
		/* Pairs 1-6: text (fg on black); 7-11: cell segment bg (white on color) */
		init_pair(1, COLOR_WHITE, COLOR_BLACK);
		init_pair(2, COLOR_BLUE, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		if (can_change_color()) {
			/* Three red shades: light (R33), medium (R52), dark (R100). Reuse RED, YELLOW, MAGENTA. */
			init_color(COLOR_RED, 627, 0, 0);       /* dark red */
			init_color(COLOR_YELLOW, 863, 235, 235); /* medium red */
			init_color(COLOR_MAGENTA, 1000, 471, 471); /* light red */
			init_pair(4, COLOR_MAGENTA, COLOR_BLACK);  /* light red text */
			init_pair(5, COLOR_YELLOW, COLOR_BLACK);   /* medium red text */
			init_pair(6, COLOR_RED, COLOR_BLACK);      /* dark red text */
			init_pair(7, COLOR_WHITE, COLOR_BLUE);
			init_pair(8, COLOR_WHITE, COLOR_GREEN);
			init_pair(9, COLOR_WHITE, COLOR_MAGENTA);
			init_pair(10, COLOR_WHITE, COLOR_YELLOW);
			init_pair(11, COLOR_WHITE, COLOR_RED);
		} else {
			init_pair(4, COLOR_RED, COLOR_BLACK);
			init_pair(5, COLOR_RED, COLOR_BLACK);
			init_pair(6, COLOR_RED, COLOR_BLACK);
			init_pair(7, COLOR_WHITE, COLOR_BLUE);
			init_pair(8, COLOR_WHITE, COLOR_GREEN);
			init_pair(9, COLOR_WHITE, COLOR_RED);
			init_pair(10, COLOR_WHITE, COLOR_RED);
			init_pair(11, COLOR_WHITE, COLOR_RED);
		}
	}

	g_cursor_row = 0;
	g_cursor_col = 0;
	g_current_history = 0;
	g_current_num_actions = 0;
	g_view_oop = 1;
	g_solving = 0;
	snprintf(g_oop_name, sizeof(g_oop_name), "%s", oop_path ? oop_path : "OOP");
	snprintf(g_ip_name, sizeof(g_ip_name), "%s", ip_path ? ip_path : "IP");

	init_gto_table();  /* ensure table is in known state before any lookups */

	WINDOW *grid_win = newwin(GRID_SIZE + 2, 4 + GRID_SIZE * 4, 0, 0);
	WINDOW *status_win = newwin(STATUS_ROWS, COLS, GRID_SIZE + 2, 0);
	if (!grid_win || !status_win) {
		if (grid_win) delwin(grid_win);
		if (status_win) delwin(status_win);
		endwin();
		return -1;
	}

	/* Draw once immediately so the TUI shows without waiting for a key */
	redraw_grid(grid_win);
	redraw_status(status_win);
	refresh();

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
			if (g_current_num_actions > 1) {
				/* Pop last action */
				g_current_history &= (1ULL << ((g_current_num_actions - 1) * BITS_PER_ACTION)) - 1;
				g_current_num_actions--;
				flop_solver_get_state_at_history(&g_fs, g_current_history, g_current_num_actions, &state);
				g_view_oop = (state.active_player == P1) ? 1 : 0;
			} else if (g_current_num_actions == 1) {
				g_current_history = 0;
				g_current_num_actions = 0;
				g_view_oop = 1;
			}
		} else if (ch >= '0' && ch <= '4') {
			/* Any non-terminal node: 0-3 when facing check, 0-4 when facing bet */
			flop_solver_get_state_at_history(&g_fs, g_current_history, g_current_num_actions, &state);
			if (state.is_terminal)
				; /* ignore */
			else {
				int a = ch - '0';
				int max_key = state.facing_bet ? 4 : 3;
				if (a <= max_key) {
					g_current_history |= ((uint64_t)(a & 7) << (g_current_num_actions * BITS_PER_ACTION));
					g_current_num_actions++;
					flop_solver_get_state_at_history(&g_fs, g_current_history, g_current_num_actions, &state);
					g_view_oop = (state.active_player == P1) ? 1 : 0;
				}
			}
		} else if (ch == KEY_LEFT || ch == 'a' || ch == 'A') {
			g_cursor_col = (g_cursor_col > 0) ? g_cursor_col - 1 : 0;
		} else if (ch == KEY_RIGHT || ch == 'd' || ch == 'D') {
			g_cursor_col = (g_cursor_col < GRID_SIZE - 1) ? g_cursor_col + 1 : GRID_SIZE - 1;
		} else if (ch == KEY_UP || ch == 'w' || ch == 'W') {
			g_cursor_row = (g_cursor_row > 0) ? g_cursor_row - 1 : 0;
		} else if (ch == KEY_DOWN || ch == 's') {
			g_cursor_row = (g_cursor_row < GRID_SIZE - 1) ? g_cursor_row + 1 : GRID_SIZE - 1;
		} else if (ch == 'S') {
			if (!g_solving && !g_fs.solved) {
				double start_t = now_seconds();
				g_solving = 1;
				g_merging = 0;
				g_fs.before_merge_cb = on_before_merge;
				g_fs.before_merge_user = status_win;
				g_fs.merge_progress_cb = on_merge_progress;
				g_fs.merge_progress_user = status_win;
				g_solve_done_iters = 0;
				g_solve_target_iters = g_iterations;
				g_solve_elapsed_sec = 0.0;
				flop_solver_begin_parallel_solve(&g_fs);
				/* Chunk size = 1% of target (min 250) so progress updates every ~1% */
				const int chunk_iters = (g_solve_target_iters >= 100)
					? (g_solve_target_iters + 99) / 100
					: g_solve_target_iters;
				const int step = (chunk_iters >= 250) ? chunk_iters : 250;
				while (g_solve_done_iters < g_solve_target_iters) {
					g_merging = 0;
					int n = step;
					if (n > g_solve_target_iters - g_solve_done_iters)
						n = g_solve_target_iters - g_solve_done_iters;
					int prev_done = g_fs.iterations_done;
					flop_solver_solve(&g_fs, n);
					g_merging = 0;
					int chunk_actual = g_fs.iterations_done - prev_done;
					g_solve_done_iters = g_fs.iterations_done;
					g_solve_elapsed_sec = now_seconds() - start_t;
					if (chunk_actual < n)
						g_solve_target_iters = g_solve_done_iters;
					redraw_status(status_win);
				}
				flop_solver_end_parallel_solve(&g_fs);
				g_solving = 0;
				g_merging = 0;
				g_merge_current = 0;
				g_merge_total = 0;
				g_fs.before_merge_cb = NULL;
				g_fs.before_merge_user = NULL;
				g_fs.merge_progress_cb = NULL;
				g_fs.merge_progress_user = NULL;
				g_solve_elapsed_sec = now_seconds() - start_t;
			}
		}
	}

	delwin(grid_win);
	delwin(status_win);
	endwin();
	return 0;
}

int main(int argc, char **argv) {
	const char *oop_path = NULL;
	const char *ip_path = NULL;
	const char *board_str = NULL;
	int user_iters = 0;
	int opt;

	while ((opt = getopt(argc, argv, "i:")) != -1) {
		switch (opt) {
		case 'i': {
			char *end;
			long val = strtol(optarg, &end, 10);
			if (*end != '\0' || val <= 0) {
				fprintf(stderr, "Error: -i requires a positive integer\n");
				return 1;
			}
			user_iters = (int)val;
			break;
		}
		default:
			fprintf(stderr, "Usage: %s [-i iterations] <oop_range.json> <ip_range.json> <board>\n", argv[0]);
			return 1;
		}
	}

	if (argc - optind >= 3) {
		oop_path = argv[optind];
		ip_path = argv[optind + 1];
		board_str = argv[optind + 2];
	} else {
		fprintf(stderr, "Usage: %s [-i iterations] <oop_range.json> <ip_range.json> <board>\n", argv[0]);
		fprintf(stderr, "  e.g. %s -i 5000000 data/ranges/oop.json data/ranges/ip.json AhKhQd3c4d\n", argv[0]);
		return 1;
	}

	memset(&g_fs, 0, sizeof(g_fs));

	float oop_grid[GRID_SIZE][GRID_SIZE];
	float ip_grid[GRID_SIZE][GRID_SIZE];
	for (int r = 0; r < GRID_SIZE; r++)
		for (int c = 0; c < GRID_SIZE; c++)
			oop_grid[r][c] = ip_grid[r][c] = 0.0f;

	if (oop_path && load_range_json(oop_path, oop_grid) != 0) {
		fprintf(stderr, "Failed to load OOP range: %s\n", oop_path);
		return 1;
	}
	if (ip_path && load_range_json(ip_path, ip_grid) != 0) {
		fprintf(stderr, "Failed to load IP range: %s\n", ip_path);
		return 1;
	}

	uint64_t board = 0;
	uint64_t preset_turn_card = 0;
	uint64_t preset_river_card = 0;
	int board_cards = 0;
	if (board_str) {
		uint64_t ordered_cards[5] = { 0, 0, 0, 0, 0 };
		if (range_parse_board_cards(board_str, ordered_cards, &board_cards) != 0) {
			fprintf(stderr, "Invalid board string. Use 3-5 unique cards like AhKhQd3c4d\n");
			return 1;
		}
		if (board_cards < 3 || board_cards > 5) {
			fprintf(stderr, "Board must contain 3-5 cards (e.g. AhKhQd or AhKhQd3c4d)\n");
			return 1;
		}
		board = ordered_cards[0] | ordered_cards[1] | ordered_cards[2];
		if (board_cards >= 4)
			preset_turn_card = ordered_cards[3];
		if (board_cards >= 5)
			preset_river_card = ordered_cards[4];
	}

	flop_solver_set_board_runout(&g_fs, board, preset_turn_card, preset_river_card);
	flop_solver_set_ranges(&g_fs, oop_grid, ip_grid);

	if (user_iters > 0)
		g_iterations = user_iters;

	init_rank_map();
	init_flush_map();
	srand((unsigned)time(NULL));
	return run_tui(oop_path, ip_path, board_str, board, board_cards);
}
