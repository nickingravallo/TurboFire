#include "walk_tree.h"
#include "dcfr.h"
#include "parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char RANK_CHARS[] = "23456789TJQKA";
static const char SUIT_CHARS[] = "shdc";
static const float PROB_EPS = 1e-6f;

// Diversity knobs (override with env):
//   SOFT_MAX_DEPTH   - emit only paths with <= this many actions (default 1)
//   SOFT_MAX_COMBOS  - max combos emitted per action node (default 96)
static int g_soft_max_depth = 1;
static int g_soft_max_combos = 96;

static int env_int(const char* name, int fallback) {
	const char* v = getenv(name);
	if (!v || !*v)
		return fallback;
	int x = atoi(v);
	return x >= 0 ? x : fallback;
}

static void shuffle_ints(int* arr, int n) {
	for (int i = n - 1; i > 0; i--) {
		int j = rand() % (i + 1);
		int tmp = arr[i];
		arr[i] = arr[j];
		arr[j] = tmp;
	}
}

static int path_action_depth(const char* path) {
	int depth = 0;
	for (const char* p = path; *p; p++) {
		if (p[0] == 'S' && p[1] == 'B' && p[2] == '_')
			depth++;
		else if (p[0] == 'B' && p[1] == 'B' && p[2] == '_')
			depth++;
	}
	return depth;
}

static uint64_t card_bit(int card) {
	int rank = card % 13;
	int suit = card / 13;

	return 1ULL << (rank + suit * 16);
}

static int card_rank(int card) {
	return card % 13;
}

static int cards_from_mask(uint64_t mask, int* out_cards, int max_cards) {
	int count = 0;

	for (int card = 0; card < 52 && count < max_cards; card++) {
		if (mask & card_bit(card))
			out_cards[count++] = card;
	}

	return count;
}

static void sort_cards_desc(int* cards, int count) {
	for (int i = 0; i < count; i++) {
		for (int j = i + 1; j < count; j++) {
			int ri = card_rank(cards[i]);
			int rj = card_rank(cards[j]);

			if (rj > ri || (rj == ri && cards[j] > cards[i])) {
				int tmp = cards[i];
				cards[i] = cards[j];
				cards[j] = tmp;
			}
		}
	}
}

static void append_string(char* buf, size_t buf_size, const char* text) {
	size_t len = strlen(buf);
	size_t add_len = strlen(text);

	if (len + add_len >= buf_size)
		return;

	memcpy(buf + len, text, add_len + 1);
}

static void append_card_string(char* buf, size_t buf_size, int card) {
	size_t len = strlen(buf);
	int rank = card % 13;
	int suit = card / 13;

	if (len + 3 >= buf_size)
		return;

	buf[len] = RANK_CHARS[rank];
	buf[len + 1] = SUIT_CHARS[suit];
	buf[len + 2] = '\0';
}

static void format_hole_combo(int combo_idx, char* buf, size_t buf_size) {
	uint64_t mask = get_mask_for_combo(combo_idx);
	int cards[2];
	int count = cards_from_mask(mask, cards, 2);

	buf[0] = '\0';
	if (count != 2)
		return;

	sort_cards_desc(cards, count);
	append_card_string(buf, buf_size, cards[0]);
	append_string(buf, buf_size, " ");
	append_card_string(buf, buf_size, cards[1]);
}

static void format_board_cards(uint64_t board, int num_cards, char* buf, size_t buf_size) {
	int cards[5];
	int count = cards_from_mask(board, cards, 5);

	buf[0] = '\0';
	if (count < num_cards)
		num_cards = count;

	sort_cards_desc(cards, count);

	for (int i = 0; i < num_cards; i++) {
		if (i > 0)
			append_string(buf, buf_size, " ");
		append_card_string(buf, buf_size, cards[i]);
	}
}

static void format_card_index(int card_idx, char* buf, size_t buf_size) {
	buf[0] = '\0';
	append_card_string(buf, buf_size, card_idx);
}

static int has_outstanding_bet(GameState state) {
	if (state.active_player == P1)
		return state.p1_commit != state.p2_commit;

	return state.p2_commit != state.p1_commit;
}

// P1 acts first (OOP) -> SB_; P2 is IP -> BB_ (dataset positional tags).
static const char* actor_tag(int player) {
	return player == P1 ? "SB_" : "BB_";
}

static void append_taken_action(char* buf, size_t buf_size, GameState state, int player, int action) {
	append_string(buf, buf_size, actor_tag(player));

	switch (action) {
		case FOLD:
			append_string(buf, buf_size, "FOLD");
			break;
		case PASS:
			append_string(buf, buf_size, has_outstanding_bet(state) ? "CALL" : "CHECK");
			break;
		case B10:
			append_string(buf, buf_size, "BET10");
			break;
		case B25:
			append_string(buf, buf_size, "BET25");
			break;
		case B52:
			append_string(buf, buf_size, "BET52");
			break;
		case B100:
			append_string(buf, buf_size, "BET100");
			break;
		case B123:
			append_string(buf, buf_size, "BET123");
			break;
		case R2x:
			append_string(buf, buf_size, "RAISE_2X");
			break;
		case R3x:
			append_string(buf, buf_size, "RAISE_3X");
			break;
		case R4x:
			append_string(buf, buf_size, "RAISE_4X");
			break;
		default:
			break;
	}
}

static int combo_is_live(int combo_idx, uint64_t board) {
	return (get_mask_for_combo(combo_idx) & board) == 0;
}

static void emit_soft_example(
	const char* flop,
	const char* path,
	int combo,
	int actor,
	GameState state,
	PublicNode* node,
	int num_combos,
	const int8_t* legal_actions,
	int action_count
) {
	char hole[16];
	char action_names[MAX_LEGAL_ACTIONS][16];
	float raw[MAX_LEGAL_ACTIONS];
	float probs[MAX_LEGAL_ACTIONS];
	float sum = 0.0f;
	int kept = 0;

	format_hole_combo(combo, hole, sizeof(hole));
	if (hole[0] == '\0')
		return;

	for (int a = 0; a < action_count; a++) {
		float w = node->strategy_sum[(a * num_combos) + combo];
		if (w < 0.0f)
			w = 0.0f;
		raw[a] = w;
		sum += w;
	}

	if (sum <= 0.0f)
		return;

	for (int a = 0; a < action_count; a++) {
		float p = raw[a] / sum;
		if (p < PROB_EPS)
			continue;

		action_names[kept][0] = '\0';
		append_taken_action(action_names[kept], sizeof(action_names[kept]), state, actor, legal_actions[a]);
		probs[kept] = p;
		kept++;
	}

	if (kept <= 0)
		return;

	// Renormalize after dropping tiny mass.
	sum = 0.0f;
	for (int i = 0; i < kept; i++)
		sum += probs[i];
	for (int i = 0; i < kept; i++)
		probs[i] /= sum;

	printf("{\"context\":\"<START> <HOLE> %s <FLOP> %s <BETTING>%s\",\"action_probs\":{", hole, flop, path);
	for (int i = 0; i < kept; i++) {
		if (i > 0)
			printf(",");
		printf("\"%s\":%.6f", action_names[i], probs[i]);
	}
	printf("}}\n");
}

static void emit_soft_recursive(
	PublicNode* node,
	GameState state,
	int num_combos,
	float* p1_range,
	float* p2_range,
	const char* flop,
	char* path,
	size_t path_size
) {
	if (!node || node->type == NODE_TERMINAL)
		return;

	int depth = path_action_depth(path);
	if (depth > g_soft_max_depth)
		return;

	if (node->type == NODE_ACTION) {
		int8_t legal_actions[MAX_LEGAL_ACTIONS];
		int action_count = get_legal_actions(state, legal_actions);
		int actor = node->active_player;
		float* range = actor == P1 ? p1_range : p2_range;
		int eligible[1326];
		int eligible_n = 0;

		if (action_count != node->num_children)
			return;

		for (int combo = 0; combo < num_combos; combo++) {
			if (range[combo] <= 0.0f)
				continue;
			if (!combo_is_live(combo, state.board))
				continue;
			eligible[eligible_n++] = combo;
		}

		if (eligible_n > g_soft_max_combos) {
			shuffle_ints(eligible, eligible_n);
			eligible_n = g_soft_max_combos;
		}

		for (int i = 0; i < eligible_n; i++) {
			emit_soft_example(
				flop,
				path,
				eligible[i],
				actor,
				state,
				node,
				num_combos,
				legal_actions,
				action_count
			);
		}

		// Recurse one level deeper only if we still want deeper emissions.
		if (depth >= g_soft_max_depth)
			return;

		for (int a = 0; a < action_count; a++) {
			size_t mark = strlen(path);
			char action_tok[16];

			action_tok[0] = '\0';
			append_taken_action(action_tok, sizeof(action_tok), state, actor, legal_actions[a]);
			append_string(path, path_size, " ");
			append_string(path, path_size, action_tok);

			emit_soft_recursive(
				node->children[a],
				apply_bet(state, legal_actions[a]),
				num_combos,
				p1_range,
				p2_range,
				flop,
				path,
				path_size
			);

			path[mark] = '\0';
		}
		return;
	}

	if (node->type == NODE_CHANCE) {
		if (depth >= g_soft_max_depth)
			return;

		// Chance after street 0 deals the turn; after street 1 deals the river.
		const char* street_tok = (state.street == 0) ? " <TURN> " : " <RIVER> ";

		for (int i = 0; i < node->num_children; i++) {
			size_t mark = strlen(path);
			char card_str[8];

			format_card_index(node->dealt_cards[i], card_str, sizeof(card_str));
			append_string(path, path_size, street_tok);
			append_string(path, path_size, card_str);
			append_string(path, path_size, " <BETTING>");

			emit_soft_recursive(
				node->children[i],
				apply_deal(state, node->dealt_cards[i]),
				num_combos,
				p1_range,
				p2_range,
				flop,
				path,
				path_size
			);

			path[mark] = '\0';
		}
	}
}

void walk_tree(
	PublicNode* root,
	GameState initial_state,
	int num_combos,
	float* p1_range,
	float* p2_range
) {
	char flop[32];
	char path[768];

	g_soft_max_depth = env_int("SOFT_MAX_DEPTH", 1);
	g_soft_max_combos = env_int("SOFT_MAX_COMBOS", 96);
	if (g_soft_max_combos < 1)
		g_soft_max_combos = 1;
	if (g_soft_max_combos > 1326)
		g_soft_max_combos = 1326;

	srand((unsigned)time(NULL) ^ (unsigned)initial_state.board);

	format_board_cards(initial_state.board, 3, flop, sizeof(flop));
	path[0] = '\0';
	fprintf(
		stderr,
		"soft emit: max_depth=%d max_combos=%d\n",
		g_soft_max_depth,
		g_soft_max_combos
	);
	emit_soft_recursive(root, initial_state, num_combos, p1_range, p2_range, flop, path, sizeof(path));
}
