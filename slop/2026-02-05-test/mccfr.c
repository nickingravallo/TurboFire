/*
 * MCCFR - Monte Carlo Counterfactual Regret Minimization for heads-up poker.
 * Uses evaluate(), hand_category, init_rank_map(), init_flush_map() from ranks.h.
 * Hands/board: uint64_t (OMP layout); payoff via evaluate(hand, board).
 *
 * UI PLUG-IN:
 *   - Use mccfr_get_strategy_at() to query strategy by (board, street, player, history)
 *     so the UI does not need mccfr_infoset_t. out_strategy[ACT_CHECK..ACT_RAISE] gives
 *     probabilities; only legal actions are non-zero (or use legal_actions to know which).
 *   - For progress/cancel: extend mccfr_solve(solver, iterations) to take a callback
 *     (e.g. void (*progress)(int done, int total, void *ctx)) and/or a volatile int *cancel;
 *     call progress every N iterations and abort if *cancel.
 *
 * CARD REMOVAL (removing our hole cards from possible opponent/board):
 *   - This solver is FIXED DEAL: one (hand_p0, hand_p1, board) per run. So "possible
 *     cards" are not enumerated inside the solver; the deal is given.
 *   - To enforce "our cards removed": (1) UI/outer layer: when creating the solver,
 *     sample (hand_p1, board) from deck minus hand_p0 (and minus hand_p1 when building
 *     board), then mccfr_create(our_hand, sampled_opp_hand, sampled_board). Run multiple
 *     times with different samples and average strategies, or run once per sample.
 *   - (2) Solver extension: add deal sampling inside solve (e.g. each iteration sample
 *     opp_hand and board from deck \ our_hand, run CFR for that deal, accumulate
 *     strategy). That would need a deck abstraction (e.g. 52-bit mask or list) and
 *     sampling without replacement. Then our hole cards are always removed from the
 *     deck when sampling.
 *
 * STAKES / MULTIPLE SIZES:
 *   Payoffs are in big blinds (P0 profit = winnings - p0_put_bb). Set via mccfr_set_stakes():
 *   big_blind (e.g. 1.0), starting_pot_bb (e.g. 1.5 for SB+BB), bet_sizes_bb[] (e.g. {0.5, 1.0, 2.0}).
 *   Actions: 0=CHECK, 1..n=BET_0..BET_(n-1), n+1=FOLD, n+2=CALL, n+3..=RAISE_0..RAISE_(n-1) (n = num_bet_sizes).
 *
 * Public API (declare in caller or add a header):
 *   mccfr_solver_t *mccfr_create(uint64_t hand_p0, uint64_t hand_p1, uint64_t board);
 *   void mccfr_set_stakes(solver, big_blind, starting_pot_bb, bet_sizes_bb[], num_bet_sizes);
 *   void mccfr_destroy(mccfr_solver_t *s);
 *   void mccfr_solve(mccfr_solver_t *solver, int iterations);
 *   void mccfr_get_strategy(const mccfr_solver_t *solver, const mccfr_infoset_t *iset, double *out_strategy);
 *   void mccfr_get_strategy_at(solver, board, street, player, actions[], num_actions, pot_bb, current_bet_bb, p0_put_bb, p1_put_bb, out_strategy);
 *   int mccfr_table_size(const mccfr_solver_t *solver);
 * Call init_rank_map(); init_flush_map(); before creating a solver.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ranks.h"

#define MAX_BET_SIZES    4   /* max number of bet/raise sizes */
#define MAX_ACTIONS      (3 + 2 * MAX_BET_SIZES)  /* CHECK, BET_0..BET_n, FOLD, CALL, RAISE_0..RAISE_n */
#define MAX_HISTORY      10
#define MCCFR_HASH_CAP   65536
#define MCCFR_EPS        1e-10
#define MAX_RAISES       2
#define POT_QUANTIZE     100.0  /* hash pot as (int)(pot_bb * POT_QUANTIZE) */

/* Action layout: 0=CHECK, 1..n=BET_0..BET_(n-1), n+1=FOLD, n+2=CALL, n+3..=RAISE_0.. */
typedef int mccfr_action_t;  /* 0..MAX_ACTIONS-1 */
#define ACT_CHECK        0
#define ACT_FOLD(sz)     ((sz) + 1)
#define ACT_CALL(sz)     ((sz) + 2)
#define ACT_RAISE(sz, i) ((sz) + 3 + (i))
#define ACT_BET(sz, i)   (1 + (i))
#define IS_CHECK(a, nsz) ((a) == 0)
#define IS_BET(a, nsz)   ((a) >= 1 && (a) < 1 + (nsz))
#define IS_FOLD(a, nsz)  ((a) == 1 + (nsz))
#define IS_CALL(a, nsz)  ((a) == 2 + (nsz))
#define IS_RAISE(a, nsz) ((a) >= 3 + (nsz) && (a) < 3 + 2 * (nsz))
#define BET_INDEX(a, nsz) ((a) - 1)
#define RAISE_INDEX(a, nsz) ((a) - 3 - (nsz))

typedef enum mccfr_street {
	STREET_FLOP = 0,
	STREET_TURN,
	STREET_RIVER
} mccfr_street_t;

typedef struct mccfr_infoset {
	uint64_t board;
	mccfr_street_t street;
	int player;
	int num_actions;
	mccfr_action_t actions[MAX_HISTORY];
	double pot_bb;        /* current pot in big blinds */
	double current_bet_bb;/* bet to call (0 if no bet facing) */
	double p0_put_bb;     /* P0 total contributed this deal */
	double p1_put_bb;     /* P1 total contributed this deal */
} mccfr_infoset_t;

typedef struct mccfr_data {
	double regrets[MAX_ACTIONS];
	double strategy_sum[MAX_ACTIONS];
	uint64_t visits;
} mccfr_data_t;

typedef struct mccfr_entry {
	uint64_t key_hash;
	mccfr_infoset_t iset;
	mccfr_data_t data;
} mccfr_entry_t;

typedef struct mccfr_solver {
	uint64_t hand_p0;
	uint64_t hand_p1;
	uint64_t board;
	double big_blind;           /* 1.0 = one BB unit */
	double starting_pot_bb;    /* e.g. 1.5 for SB+BB */
	double bet_sizes[MAX_BET_SIZES];  /* in BB: e.g. 0.5, 1.0, 2.0 */
	int num_bet_sizes;         /* 1..MAX_BET_SIZES */
	mccfr_entry_t *table;
	int table_cap;
	int table_size;
} mccfr_solver_t;

/* --- Hash --- */
static uint64_t hash_u64(uint64_t a, uint64_t b) {
	return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
}

static uint64_t mccfr_hash_infoset(const mccfr_infoset_t *iset) {
	uint64_t h = 0;
	h = hash_u64(h, iset->board);
	h = hash_u64(h, iset->board >> 32);
	h = hash_u64(h, (uint64_t)iset->street);
	h = hash_u64(h, (uint64_t)iset->player);
	h = hash_u64(h, (uint64_t)iset->num_actions);
	for (int i = 0; i < iset->num_actions && i < MAX_HISTORY; i++)
		h = hash_u64(h, (uint64_t)iset->actions[i]);
	h = hash_u64(h, (uint64_t)(int)(iset->pot_bb * POT_QUANTIZE));
	h = hash_u64(h, (uint64_t)(int)(iset->current_bet_bb * POT_QUANTIZE));
	h = hash_u64(h, (uint64_t)(int)(iset->p0_put_bb * POT_QUANTIZE));
	h = hash_u64(h, (uint64_t)(int)(iset->p1_put_bb * POT_QUANTIZE));
	return h;
}

/* --- Legal actions: no bet facing -> CHECK, BET_0..BET_n; facing bet -> FOLD, CALL, RAISE_0..RAISE_n --- */
static int facing_bet(const mccfr_solver_t *solver, const mccfr_infoset_t *iset) {
	if (iset->num_actions == 0) return 0;
	mccfr_action_t last = iset->actions[iset->num_actions - 1];
	int nsz = solver->num_bet_sizes;
	return (IS_BET(last, nsz) || IS_RAISE(last, nsz));
}

static int raise_count(const mccfr_solver_t *solver, const mccfr_infoset_t *iset) {
	int n = 0, nsz = solver->num_bet_sizes;
	for (int i = 0; i < iset->num_actions; i++)
		if (IS_RAISE(iset->actions[i], nsz)) n++;
	return n;
}

static int legal_actions(const mccfr_solver_t *solver, const mccfr_infoset_t *iset, int *out_actions) {
	int n = 0, nsz = solver->num_bet_sizes;
	if (!facing_bet(solver, iset)) {
		out_actions[n++] = ACT_CHECK;
		for (int i = 0; i < nsz; i++) out_actions[n++] = ACT_BET(nsz, i);
	} else {
		out_actions[n++] = ACT_FOLD(nsz);
		out_actions[n++] = ACT_CALL(nsz);
		if (raise_count(solver, iset) < MAX_RAISES)
			for (int i = 0; i < nsz; i++) out_actions[n++] = ACT_RAISE(nsz, i);
	}
	return n;
}

/* --- Terminal: FOLD -> done; CALL -> showdown; CHECK after CHECK -> advance street or showdown --- */
static int is_terminal(const mccfr_solver_t *solver, const mccfr_infoset_t *iset, mccfr_action_t last_act) {
	int nsz = solver->num_bet_sizes;
	if (IS_FOLD(last_act, nsz)) return 1;
	if (IS_CALL(last_act, nsz)) return 1;
	if (IS_CHECK(last_act, nsz) && iset->num_actions >= 2 &&
	    IS_CHECK(iset->actions[iset->num_actions - 2], nsz))
		return 1;
	return 0;
}

/* --- Payoff in BB from P0 perspective: P0 profit = winnings - p0_put_bb --- */
static double payoff_showdown_bb(uint64_t hand_p0, uint64_t hand_p1, uint64_t board, double pot_bb, double p0_put_bb) {
	int s0 = evaluate(hand_p0, board);
	int s1 = evaluate(hand_p1, board);
	double winnings;
	if (s0 > s1) winnings = pot_bb;
	else if (s0 < s1) winnings = 0.0;
	else winnings = pot_bb * 0.5;
	return winnings - p0_put_bb;
}

static double payoff_terminal(mccfr_solver_t *solver, const mccfr_infoset_t *iset, mccfr_action_t last_act) {
	int nsz = solver->num_bet_sizes;
	double pot = iset->pot_bb, p0 = iset->p0_put_bb;
	if (IS_FOLD(last_act, nsz)) {
		/* acting player (iset->player) folded: opponent wins pot. P0 profit from P0 view */
		return (iset->player == 0) ? -p0 : (pot - p0);
	}
	if (IS_CALL(last_act, nsz))
		return payoff_showdown_bb(solver->hand_p0, solver->hand_p1, solver->board, pot, p0);
	/* check-check: river showdown */
	if (IS_CHECK(last_act, nsz) && iset->street == STREET_RIVER)
		return payoff_showdown_bb(solver->hand_p0, solver->hand_p1, solver->board, pot, p0);
	return 0.0;
}

/* --- Regret matching over legal action set --- */
static void regret_matching(const mccfr_data_t *data, const int *legal, int n_legal, double *strategy) {
	double sum = 0.0;
	for (int i = 0; i < n_legal; i++) {
		int a = legal[i];
		strategy[a] = (data->regrets[a] > 0.0) ? data->regrets[a] : 0.0;
		sum += strategy[a];
	}
	if (sum > 0.0) {
		for (int i = 0; i < n_legal; i++)
			strategy[legal[i]] /= sum;
	} else {
		for (int i = 0; i < n_legal; i++)
			strategy[legal[i]] = 1.0 / (double)n_legal;
	}
}

/* --- Apply action to get next pot/bet/contributions --- */
static void apply_action(const mccfr_solver_t *solver, const mccfr_infoset_t *iset, mccfr_action_t a, mccfr_infoset_t *next) {
	*next = *iset;
	int nsz = solver->num_bet_sizes;
	double pot = iset->pot_bb, bet = iset->current_bet_bb;
	double p0 = iset->p0_put_bb, p1 = iset->p1_put_bb;
	int pl = iset->player;

	if (IS_CHECK(a, nsz)) {
		next->current_bet_bb = bet; next->pot_bb = pot; next->p0_put_bb = p0; next->p1_put_bb = p1;
		return;
	}
	if (IS_BET(a, nsz)) {
		double s = solver->bet_sizes[BET_INDEX(a, nsz)];
		next->pot_bb = pot + s;
		next->current_bet_bb = s;
		if (pl == 0) { next->p0_put_bb = p0 + s; next->p1_put_bb = p1; }
		else         { next->p0_put_bb = p0; next->p1_put_bb = p1 + s; }
		return;
	}
	if (IS_FOLD(a, nsz)) {
		next->pot_bb = pot; next->current_bet_bb = bet; next->p0_put_bb = p0; next->p1_put_bb = p1;
		return;
	}
	if (IS_CALL(a, nsz)) {
		next->pot_bb = pot + bet;
		next->current_bet_bb = 0.0;
		if (pl == 0) { next->p0_put_bb = p0 + bet; next->p1_put_bb = p1; }
		else         { next->p0_put_bb = p0; next->p1_put_bb = p1 + bet; }
		return;
	}
	if (IS_RAISE(a, nsz)) {
		double s = solver->bet_sizes[RAISE_INDEX(a, nsz)];
		next->pot_bb = pot + bet + s;
		next->current_bet_bb = s;
		if (pl == 0) { next->p0_put_bb = p0 + bet + s; next->p1_put_bb = p1; }
		else         { next->p0_put_bb = p0; next->p1_put_bb = p1 + bet + s; }
		return;
	}
}

static int infoset_eq(const mccfr_infoset_t *a, const mccfr_infoset_t *b) {
	if (a->board != b->board || a->street != b->street || a->player != b->player || a->num_actions != b->num_actions)
		return 0;
	for (int i = 0; i < a->num_actions; i++)
		if (a->actions[i] != b->actions[i]) return 0;
	if (fabs(a->pot_bb - b->pot_bb) > 1e-6) return 0;
	if (fabs(a->current_bet_bb - b->current_bet_bb) > 1e-6) return 0;
	if (fabs(a->p0_put_bb - b->p0_put_bb) > 1e-6) return 0;
	if (fabs(a->p1_put_bb - b->p1_put_bb) > 1e-6) return 0;
	return 1;
}

/* --- Hash table: find or create --- */
static mccfr_data_t *find_or_create(mccfr_solver_t *solver, const mccfr_infoset_t *iset) {
	uint64_t h = mccfr_hash_infoset(iset);
	int mask = solver->table_cap - 1;
	int idx = (int)(h & (uint64_t)mask);
	int start = idx;
	for (;;) {
		mccfr_entry_t *e = &solver->table[idx];
		if (e->key_hash == 0) {
			e->key_hash = h;
			e->iset = *iset;
			memset(&e->data, 0, sizeof(e->data));
			solver->table_size++;
			return &e->data;
		}
		if (e->key_hash == h && infoset_eq(&e->iset, iset)) return &e->data;
		idx = (idx + 1) & mask;
		if (idx == start) return NULL;
	}
}

/* --- CFR recursion (from P0 perspective) --- */
static double cfr_recursive(mccfr_solver_t *solver, mccfr_infoset_t *iset, double reach_p0, double reach_p1, int depth) {
	if (depth > 20) return 0.0;
	if (reach_p0 < MCCFR_EPS || reach_p1 < MCCFR_EPS) return 0.0;

	mccfr_data_t *data = find_or_create(solver, iset);
	if (!data) return 0.0;
	data->visits++;

	/* Terminal? */
	if (iset->num_actions > 0) {
		mccfr_action_t last = iset->actions[iset->num_actions - 1];
		if (is_terminal(solver, iset, last)) {
			double pay = payoff_terminal(solver, iset, last);
			/* Check-check on non-river: advance street; recurse (pot/put unchanged) */
			if (IS_CHECK(last, solver->num_bet_sizes) && iset->street != STREET_RIVER &&
			    iset->num_actions >= 2 && IS_CHECK(iset->actions[iset->num_actions - 2], solver->num_bet_sizes)) {
				mccfr_infoset_t next = *iset;
				next.street = (mccfr_street_t)((int)next.street + 1);
				next.player = 0;
				next.num_actions = 0;
				return cfr_recursive(solver, &next, reach_p0, reach_p1, depth + 1);
			}
			return (iset->player == 0) ? pay : -pay;
		}
	}

	int legal_list[MAX_ACTIONS];
	int n_legal = legal_actions(solver, iset, legal_list);
	double strategy[MAX_ACTIONS];
	memset(strategy, 0, sizeof(strategy));
	regret_matching(data, legal_list, n_legal, strategy);

	double node_util = 0.0;
	double util[MAX_ACTIONS];
	memset(util, 0, sizeof(util));

	for (int i = 0; i < n_legal; i++) {
		mccfr_action_t a = (mccfr_action_t)legal_list[i];
		int nsz = solver->num_bet_sizes;
		mccfr_infoset_t next;

		if (IS_FOLD(a, nsz)) {
			util[a] = payoff_terminal(solver, iset, a);
		} else {
			apply_action(solver, iset, a, &next);
			next.actions[next.num_actions] = a;
			next.num_actions++;
			next.player = 1 - next.player;

			if (IS_CHECK(a, nsz) && next.num_actions >= 2 && IS_CHECK(next.actions[next.num_actions - 2], nsz)) {
				if (next.street == STREET_FLOP) {
					next.street = STREET_TURN;
					next.num_actions = 0;
					next.player = 0;
				} else if (next.street == STREET_TURN) {
					next.street = STREET_RIVER;
					next.num_actions = 0;
					next.player = 0;
				}
			}

			double r0 = (iset->player == 0) ? reach_p0 * strategy[a] : reach_p0;
			double r1 = (iset->player == 1) ? reach_p1 * strategy[a] : reach_p1;
			util[a] = cfr_recursive(solver, &next, r0, r1, depth + 1);
		}
		node_util += strategy[a] * util[a];
	}

	double cf_reach = (iset->player == 0) ? reach_p1 : reach_p0;
	double reach_act = (iset->player == 0) ? reach_p0 : reach_p1;
	double player_util[MAX_ACTIONS], player_node;
	for (int i = 0; i < n_legal; i++) {
		mccfr_action_t a = (mccfr_action_t)legal_list[i];
		player_util[a] = (iset->player == 0) ? util[a] : -util[a];
	}
	player_node = (iset->player == 0) ? node_util : -node_util;

	for (int i = 0; i < n_legal; i++) {
		mccfr_action_t a = (mccfr_action_t)legal_list[i];
		double regret = player_util[a] - player_node;
		data->regrets[a] += cf_reach * regret;
		data->strategy_sum[a] += reach_act * strategy[a];
	}
	return node_util;
}

/* --- Public API --- */
mccfr_solver_t *mccfr_create(uint64_t hand_p0, uint64_t hand_p1, uint64_t board) {
	mccfr_solver_t *s = (mccfr_solver_t *)malloc(sizeof(mccfr_solver_t));
	if (!s) return NULL;
	s->hand_p0 = hand_p0;
	s->hand_p1 = hand_p1;
	s->board = board;
	s->big_blind = 1.0;
	s->starting_pot_bb = 1.5;   /* SB + BB */
	s->bet_sizes[0] = 1.0;      /* one size = 1 BB by default */
	s->num_bet_sizes = 1;
	s->table_cap = MCCFR_HASH_CAP;
	s->table_size = 0;
	s->table = (mccfr_entry_t *)calloc((size_t)s->table_cap, sizeof(mccfr_entry_t));
	if (!s->table) { free(s); return NULL; }
	return s;
}

void mccfr_set_stakes(mccfr_solver_t *solver, double big_blind, double starting_pot_bb,
	const double *bet_sizes_bb, int num_bet_sizes) {
	if (!solver || num_bet_sizes <= 0 || num_bet_sizes > MAX_BET_SIZES) return;
	solver->big_blind = big_blind > 0.0 ? big_blind : 1.0;
	solver->starting_pot_bb = starting_pot_bb > 0.0 ? starting_pot_bb : 1.5;
	solver->num_bet_sizes = num_bet_sizes;
	for (int i = 0; i < num_bet_sizes; i++)
		solver->bet_sizes[i] = bet_sizes_bb[i] > 0.0 ? bet_sizes_bb[i] : 1.0;
}

void mccfr_destroy(mccfr_solver_t *s) {
	if (!s) return;
	free(s->table);
	free(s);
}

void mccfr_solve(mccfr_solver_t *solver, int iterations) {
	mccfr_infoset_t root;
	memset(&root, 0, sizeof(root));
	root.board = solver->board;
	root.street = STREET_FLOP;
	root.player = 0;
	root.num_actions = 0;
	root.pot_bb = solver->starting_pot_bb;
	root.current_bet_bb = 0.0;
	root.p0_put_bb = solver->starting_pot_bb * 0.5;
	root.p1_put_bb = solver->starting_pot_bb * 0.5;

	for (int i = 0; i < iterations; i++)
		cfr_recursive(solver, &root, 1.0, 1.0, 0);
}

/* Normalize strategy_sum into a probability distribution; call after solve. */
void mccfr_get_strategy(const mccfr_solver_t *solver, const mccfr_infoset_t *iset, double *out_strategy) {
	mccfr_data_t *data = NULL;
	uint64_t h = mccfr_hash_infoset(iset);
	int mask = solver->table_cap - 1;
	int idx = (int)(h & (uint64_t)mask);
	int start = idx;
	for (;;) {
		mccfr_entry_t *e = &solver->table[idx];
		if (e->key_hash == 0) break;
		if (e->key_hash == h && infoset_eq(&e->iset, iset)) { data = &e->data; break; }
		idx = (idx + 1) & mask;
		if (idx == start) break;
	}
	for (int a = 0; a < MAX_ACTIONS; a++) out_strategy[a] = 0.0;
	if (!data) return;
	double sum = 0.0;
	for (int a = 0; a < MAX_ACTIONS; a++) sum += data->strategy_sum[a];
	if (sum > 0.0)
		for (int a = 0; a < MAX_ACTIONS; a++) out_strategy[a] = data->strategy_sum[a] / sum;
	else
		for (int a = 0; a < MAX_ACTIONS; a++) out_strategy[a] = 1.0 / (double)MAX_ACTIONS;
}

int mccfr_table_size(const mccfr_solver_t *solver) {
	return solver->table_size;
}

/* UI-friendly: query strategy by board, street, player, action history, and pot state (in BB).
 * actions[]: 0=CHECK, 1..n=BET_0..BET_(n-1), n+1=FOLD, n+2=CALL, n+3..=RAISE (n = solver->num_bet_sizes). */
void mccfr_get_strategy_at(const mccfr_solver_t *solver, uint64_t board, int street,
	int player, const int *actions, int num_actions,
	double pot_bb, double current_bet_bb, double p0_put_bb, double p1_put_bb,
	double *out_strategy) {
	mccfr_infoset_t iset;
	memset(&iset, 0, sizeof(iset));
	iset.board = board;
	iset.street = (mccfr_street_t)(street >= 0 && street <= 2 ? street : 0);
	iset.player = (player != 0) ? 1 : 0;
	iset.num_actions = num_actions <= MAX_HISTORY ? num_actions : MAX_HISTORY;
	for (int i = 0; i < iset.num_actions; i++)
		iset.actions[i] = (mccfr_action_t)(actions[i] >= 0 && actions[i] < MAX_ACTIONS ? actions[i] : 0);
	iset.pot_bb = pot_bb >= 0.0 ? pot_bb : solver->starting_pot_bb;
	iset.current_bet_bb = current_bet_bb >= 0.0 ? current_bet_bb : 0.0;
	iset.p0_put_bb = p0_put_bb >= 0.0 ? p0_put_bb : (solver->starting_pot_bb * 0.5);
	iset.p1_put_bb = p1_put_bb >= 0.0 ? p1_put_bb : (solver->starting_pot_bb * 0.5);
	mccfr_get_strategy(solver, &iset, out_strategy);
}
