/*
 * Flop solver TUI: 13x13 grid, highlight hand for action breakdown, traverse streets.
 * Usage: flop_tui <oop_range.json> <ip_range.json> <board>  e.g. flop_tui oop.json ip.json AhKhQd
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

#define STATUS_LEN 128
#define BOARD_STR_LEN 32

static FlopSolver g_fs;
static int g_cursor_row, g_cursor_col;
static int g_current_street;  /* 0=flop, 1=turn, 2=river - for display; solver is flop-only for now */
static char g_board_str[BOARD_STR_LEN];
static char g_oop_name[64];
static char g_ip_name[64];
static int g_solving;
static int g_iterations = 50000;

static void redraw_grid(WINDOW *win) {
	int r, c;
	char hand_str[8];
	float probs[FLOP_MAX_ACTIONS];
	wclear(win);
	/* Header row: column labels A -> 2 */
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
			int in_range = (g_fs.oop_weights[r][c] > 0.0f);
			int is_cursor = (r == g_cursor_row && c == g_cursor_col);

			if (is_cursor) {
				wattron(win, A_REVERSE | A_BOLD);
				wprintw(win, "%3s", hand_str);
				wattroff(win, A_REVERSE | A_BOLD);
			} else if (in_range) {
				/* Color by primary action if solved */
				if (g_fs.solved && flop_solver_get_oop_strategy(&g_fs, r, c, probs) == 0) {
					int best = 0;
					if (probs[1] > probs[best]) best = 1;
					if (probs[2] > probs[best]) best = 2;
					if (best == 0) wattron(win, COLOR_PAIR(2));   /* fold = blue */
					else if (best == 1) wattron(win, COLOR_PAIR(3)); /* call = green */
					else wattron(win, COLOR_PAIR(4));                 /* raise = red */
				}
				wprintw(win, "%3s", hand_str);
				if (g_fs.solved) wattrset(win, A_NORMAL);
			} else {
				wprintw(win, "%3s", hand_str);
			}
		}
		wprintw(win, "\n");
	}
	wrefresh(win);
}

static void redraw_status(WINDOW *win) {
	char buf[STATUS_LEN];
	char hand_str[8];
	float probs[FLOP_MAX_ACTIONS];

	hand_at(g_cursor_row, g_cursor_col, hand_str, sizeof(hand_str));

	if (g_solving) {
		snprintf(buf, sizeof(buf), " Solving... %d iters | %s | q quit ", g_iterations, g_board_str);
	} else if (g_fs.solved) {
		if (flop_solver_get_oop_strategy(&g_fs, g_cursor_row, g_cursor_col, probs) == 0) {
			snprintf(buf, sizeof(buf),
				" %s | %s | Fold %.0f%% Call %.0f%% Raise %.0f%% | f/t/r street | wasd move | q quit ",
				hand_str, g_board_str,
				probs[0] * 100.0f, probs[1] * 100.0f, probs[2] * 100.0f);
		} else {
			snprintf(buf, sizeof(buf), " %s | %s | (no data) | f/t/r street | wasd move | q quit ", hand_str, g_board_str);
		}
	} else {
		snprintf(buf, sizeof(buf), " %s | %s | Not solved (S to solve) | wasd move | q quit ", hand_str, g_board_str);
	}
	wclear(win);
	mvwprintw(win, 0, 0, "%s", buf);
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
	if (has_colors()) {
		start_color();
		init_pair(1, COLOR_WHITE, COLOR_BLACK);
		init_pair(2, COLOR_BLUE, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_RED, COLOR_BLACK);
	}

	g_cursor_row = 0;
	g_cursor_col = 0;
	g_current_street = 0;
	g_solving = 0;
	snprintf(g_board_str, sizeof(g_board_str), "%s", board_str ? board_str : "(no board)");
	snprintf(g_oop_name, sizeof(g_oop_name), "%s", oop_path ? oop_path : "OOP");
	snprintf(g_ip_name, sizeof(g_ip_name), "%s", ip_path ? ip_path : "IP");

	WINDOW *grid_win = newwin(GRID_SIZE + 2, 4 + GRID_SIZE * 4, 0, 0);
	WINDOW *status_win = newwin(1, 80, GRID_SIZE + 2, 0);

	for (;;) {
		redraw_grid(grid_win);
		redraw_status(status_win);

		int ch = getch();
		if (ch == 'q' || ch == 'Q' || ch == 27) break;
		if (ch == KEY_LEFT || ch == 'a' || ch == 'A') {
			g_cursor_col = (g_cursor_col > 0) ? g_cursor_col - 1 : 0;
		} else if (ch == KEY_RIGHT || ch == 'd' || ch == 'D') {
			g_cursor_col = (g_cursor_col < GRID_SIZE - 1) ? g_cursor_col + 1 : GRID_SIZE - 1;
		} else if (ch == KEY_UP || ch == 'w' || ch == 'W') {
			g_cursor_row = (g_cursor_row > 0) ? g_cursor_row - 1 : 0;
		} else if (ch == KEY_DOWN || ch == 's') {
			g_cursor_row = (g_cursor_row < GRID_SIZE - 1) ? g_cursor_row + 1 : GRID_SIZE - 1;
		} else if (ch == 'S') {
			if (!g_solving && !g_fs.solved) {
				g_solving = 1;
				redraw_status(status_win);
				flop_solver_solve(&g_fs, g_iterations);
				g_solving = 0;
			}
		} else if (ch == 'f' || ch == 'F') {
			g_current_street = 0;
		} else if (ch == 't' || ch == 'T') {
			g_current_street = 1;
		} else if (ch == 'r' || ch == 'R') {
			g_current_street = 2;
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
