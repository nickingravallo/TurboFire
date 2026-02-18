/*
 * TurboFire solver TUI: 13x13 grid, highlight hand for action breakdown, traverse streets.
 * Usage: turbofire <oop_range.json> <ip_range.json> <board>  e.g. turbofire oop.json ip.json AhKhQd
 */

#define _POSIX_C_SOURCE 200809L
#include "flop_solver.h"
#include "range.h"
#include "ranks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <time.h>

#define STATUS_LEN 512
#define BOARD_STR_LEN 32
#define MAX_COMBOS 12
#define STATUS_ROWS 6
static const char SUITS[] = "shdc";  /* suit 0=S, 1=H, 2=D, 3=C */

/* OOP action labels (6): Check, Bet33, Bet52, Bet75, Bet100, Bet123 */
static const char *const OOP_ACTION_LABELS[] = { "Check", "Bet33%", "Bet52%", "Bet75%", "Bet100%", "Bet123%" };
/* IP facing bet (5): Fold, Call, R33, R75, R123 */
static const char *const IP_BET_ACTION_LABELS[] = { "Fold", "Call", "R33%", "R75%", "R123%" };

static FlopSolver g_fs;
static int g_cursor_row, g_cursor_col;
static uint64_t g_current_history;   /* 0 = OOP to act; else encoded action path */
static int g_current_num_actions;   /* 0 = root, 1 = after one OOP action, etc. */
static int g_view_oop;              /* 1 = viewing OOP range/strategy, 0 = viewing IP */
static char g_board_str[BOARD_STR_LEN];
static char g_oop_name[64];
static char g_ip_name[64];
static int g_solving;
static int g_iterations = 50000;
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

/* Render compact board string (e.g. "Kd7h2s") as spaced cards ("Kd 7h 2s"). */
static void format_board_for_status(const char *board_in, char *board_out, size_t out_sz) {
	size_t in_len;
	size_t out_i = 0;

	if (!board_out || out_sz == 0) return;
	if (!board_in) {
		snprintf(board_out, out_sz, "(no board)");
		return;
	}

	in_len = strlen(board_in);
	if (in_len < 2) {
		snprintf(board_out, out_sz, "%s", board_in);
		return;
	}

	for (size_t i = 0; i < in_len && out_i + 1 < out_sz; i += 2) {
		if (i > 0 && out_i + 1 < out_sz) board_out[out_i++] = ' ';
		board_out[out_i++] = board_in[i];
		if (i + 1 < in_len && out_i + 1 < out_sz) board_out[out_i++] = board_in[i + 1];
	}
	board_out[out_i] = '\0';
}

static int action_color_pair(int facing_bet, int action_idx) {
	if (facing_bet) {
		/* Fold red, call green, raises yellow. */
		if (action_idx == 0) return 4;
		if (action_idx == 1) return 3;
		return 5;
	}
	/* Check green, all bet sizes cyan. */
	if (action_idx == 0) return 3;
	return 6;
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
		if (g_has_colors) wattron(win, COLOR_PAIR(action_color_pair(facing_bet, j)) | A_BOLD);
		mvwaddnstr(win, row, x, chunk, w - 1 - x);
		if (g_has_colors) wattroff(win, COLOR_PAIR(action_color_pair(facing_bet, j)) | A_BOLD);
		x += n;
	}
}

static void redraw_grid(WINDOW *win) {
	int r, c;
	char hand_str[8];
	float probs[FLOP_MAX_ACTIONS];
	GameState state;
	const float (*weights)[GRID_SIZE];
	int n_actions, best;
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
				best = 0;
				for (int a = 1; a < n_actions; a++)
					if (probs[a] > probs[best]) best = a;
				if (state.facing_bet) {
					if (best == 0) wattron(win, COLOR_PAIR(2));
					else if (best == 1) wattron(win, COLOR_PAIR(3));
					else wattron(win, COLOR_PAIR(4));
				} else {
					/* Check should be green. */
					if (best == 0) wattron(win, COLOR_PAIR(3));
					else wattron(win, COLOR_PAIR(4));
				}
				wprintw(win, "%-3s ", hand_str);
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

static void format_path(char *buf, size_t sz) {
	if (g_current_num_actions == 0) {
		snprintf(buf, sz, "OOP to act");
		return;
	}
	/* One OOP action: show "OOP Bet33 -> IP to act" */
	int a = (int)(g_current_history & 7u);
	if (a < 0) a = 0;
	if (a > 5) a = 5;
	snprintf(buf, sz, "OOP %s -> IP to act", OOP_ACTION_LABELS[a]);
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
	format_board_for_status(g_board_str, board_disp, sizeof(board_disp));
	snprintf(controls, sizeof(controls), " v view   b back   0-5 act  |  wasd move  |  q quit ");
	werase(win);

	if (g_solving) {
		double pct = (g_solve_target_iters > 0)
			? (double)g_solve_done_iters / (double)g_solve_target_iters
			: 0.0;
		int bar_width = 24;
		int filled = (int)(pct * bar_width + 0.5);
		if (filled < 0) filled = 0;
		if (filled > bar_width) filled = bar_width;
		char bar[32];
		for (int i = 0; i < bar_width; i++) bar[i] = (i < filled) ? '#' : '-';
		bar[bar_width] = '\0';
		double eta = (g_solve_done_iters > 0)
			? (g_solve_elapsed_sec * (double)(g_solve_target_iters - g_solve_done_iters) / (double)g_solve_done_iters)
			: -1.0;
		snprintf(line1, sizeof(line1), " %s | %s | %s | Pot %.2fbb  OOP %.2fbb  IP %.2fbb ",
			path_buf, board_disp, hand_str, state.pot, state.p1_stack, state.p2_stack);
		if (eta >= 0.0) {
			snprintf(line2, sizeof(line2),
				" Solving [%s] %5.1f%%  (%d/%d)  elapsed %.1fs  ETA %.1fs ",
				bar, pct * 100.0, g_solve_done_iters, g_solve_target_iters, g_solve_elapsed_sec, eta);
		} else {
			snprintf(line2, sizeof(line2),
				" Solving [%s] %5.1f%%  (%d/%d)  elapsed %.1fs  ETA -- ",
				bar, pct * 100.0, g_solve_done_iters, g_solve_target_iters, g_solve_elapsed_sec);
		}
		print_line_clipped(win, row++, line1);
		print_line_clipped(win, row++, line2);
		print_line_clipped(win, row++, controls);
	} else if (g_fs.solved) {
		if (flop_solver_get_strategy_at_history(&g_fs, g_current_history, g_current_num_actions,
			g_cursor_row, g_cursor_col, probs, &n_actions) == 0) {
			labels = state.facing_bet ? IP_BET_ACTION_LABELS : OOP_ACTION_LABELS;
			snprintf(line1, sizeof(line1), " %s | %s | %s | Pot %.2fbb  OOP %.2fbb  IP %.2fbb ",
				path_buf, board_disp, hand_str, state.pot, state.p1_stack, state.p2_stack);
			print_line_clipped(win, row++, line1);
			if (is_suited) {
				int n = hand_string_to_combos(hand_str, g_fs.board, c1, c2, MAX_COMBOS);
				for (int i = 0; i < n && row < STATUS_ROWS - 1; i++) {
					char combo_str[8];
					float p[FLOP_MAX_ACTIONS];
					char prefix[32];
					hand_bitmask_to_string(c1[i], c2[i], combo_str, sizeof(combo_str));
					if (flop_solver_get_hand_strategy(
						g_current_history, g_fs.board, g_current_num_actions, c1[i] | c2[i], p
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

static int run_tui(const char *oop_path, const char *ip_path, const char *board_str, uint64_t board, int board_cards) {
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
		init_pair(1, COLOR_WHITE, COLOR_BLACK);
		init_pair(2, COLOR_BLUE, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_RED, COLOR_BLACK);
		init_pair(5, COLOR_YELLOW, COLOR_BLACK);
		init_pair(6, COLOR_CYAN, COLOR_BLACK);
	}

	g_cursor_row = 0;
	g_cursor_col = 0;
	g_current_history = 0;
	g_current_num_actions = 0;
	g_view_oop = 1;
	g_solving = 0;
	snprintf(g_board_str, sizeof(g_board_str), "%s", board_str ? board_str : "(no board)");
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
		if (ch == 'q' || ch == 'Q' || ch == 27) break;
		if (ch == 'v' || ch == 'V' || ch == '\t') {
			g_view_oop = 1 - g_view_oop;
		} else if (ch == 'b' || ch == 'B' || ch == KEY_BACKSPACE) {
			if (g_current_num_actions > 0) {
				g_current_history = 0;
				g_current_num_actions = 0;
			}
		} else if (ch >= '0' && ch <= '5' && g_current_num_actions == 0) {
			/* OOP to act: 0=Check, 1=Bet33, ..., 5=Bet123 -> go to IP */
			int a = ch - '0';
			g_current_history = (uint64_t)a;
			g_current_num_actions = 1;
			g_view_oop = 0;  /* auto-switch to IP view */
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
				const int chunk_iters = 2000;
				double start_t = now_seconds();
				g_solving = 1;
				g_solve_done_iters = 0;
				g_solve_target_iters = g_iterations;
				g_solve_elapsed_sec = 0.0;
				while (g_solve_done_iters < g_solve_target_iters) {
					int step = chunk_iters;
					if (step > g_solve_target_iters - g_solve_done_iters)
						step = g_solve_target_iters - g_solve_done_iters;
					flop_solver_solve(&g_fs, step);
					g_solve_done_iters += step;
					g_solve_elapsed_sec = now_seconds() - start_t;
					redraw_status(status_win);
				}
				g_solving = 0;
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

	if (argc >= 4) {
		oop_path = argv[1];
		ip_path = argv[2];
		board_str = argv[3];
	} else if (argc >= 1) {
		fprintf(stderr, "Usage: %s <oop_range.json> <ip_range.json> <board>\n", argv[0]);
		fprintf(stderr, "  e.g. %s data/ranges/oop.json data/ranges/ip.json AhKhQd\n", argv[0]);
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
	int board_cards = 0;
	if (board_str) {
		board = range_parse_board(board_str, &board_cards);
		if (board_cards < 3) {
			fprintf(stderr, "Board must be at least 3 cards (e.g. AhKhQd)\n");
			return 1;
		}
		/* Use only first 3 for flop */
		if (board_cards > 3) {
			uint64_t b3 = 0;
			int n = 0;
			for (int r = 0; r < 13 && n < 3; r++) {
				for (int s = 0; s < 4 && n < 3; s++) {
					uint64_t card = range_make_card(r, s);
					if (board & card) {
						b3 |= card;
						n++;
					}
				}
			}
			board = b3;
			board_cards = 3;
		}
	}

	flop_solver_set_board(&g_fs, board);
	flop_solver_set_ranges(&g_fs, oop_grid, ip_grid);

	init_rank_map();
	init_flush_map();
	srand((unsigned)time(NULL));
	return run_tui(oop_path, ip_path, board_str, board, board_cards);
}
