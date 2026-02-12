/**
 * strategy.h - Regret and Strategy Storage
 * 
 * This module implements sparse storage for MCCFR regrets and strategies.
 * Uses a hash table with linear probing for efficient lookups.
 * 
 * Each information set stores:
 * - Regret sums for each action (can be negative)
 * - Strategy sums for computing average strategy (always positive)
 * - Visit count for normalization
 */

#ifndef STRATEGY_H
#define STRATEGY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "actions.h"

/* Initial capacity of hash table (will grow as needed) */
#define INITIAL_TABLE_CAPACITY (1 << 20)  /* ~1 million entries */
#define LOAD_FACTOR_THRESHOLD 0.75

/* Maximum number of actions per info set */
#define MAX_INFO_SET_ACTIONS 8

/*
 * Info set data stored in hash table
 */
typedef struct {
    uint64_t key;                    /* Hash key */
    int num_actions;                 /* Number of valid actions */
    Action actions[MAX_INFO_SET_ACTIONS];  /* Which actions are valid */
    double regrets[MAX_INFO_SET_ACTIONS];  /* Cumulative regrets */
    double strategy_sums[MAX_INFO_SET_ACTIONS]; /* Cumulative strategy */
    uint64_t visit_count;            /* How many times visited */
    bool occupied;                   /* Is this slot used? */
} InfoSetData;

/*
 * Strategy table (hash map)
 */
typedef struct {
    InfoSetData* entries;            /* Array of entries */
    size_t capacity;                 /* Total slots */
    size_t count;                    /* Occupied slots */
    size_t collisions;               /* For stats */
} StrategyTable;

/*
 * Initialize and cleanup
 */

/* Create new strategy table */
StrategyTable* strategy_table_create(void);

/* Free strategy table */
void strategy_table_free(StrategyTable* table);

/* Clear all entries (reset to zero) */
void strategy_table_clear(StrategyTable* table);

/*
 * Data access
 */

/* Get or create info set data */
InfoSetData* strategy_get(StrategyTable* table, uint64_t key, 
                          const Action* actions, int num_actions);

/* Get existing info set (returns NULL if not found) */
InfoSetData* strategy_get_existing(StrategyTable* table, uint64_t key);

/*
 * Regret matching
 */

/* 
 * Compute current strategy from regrets using regret matching
 * strategy[i] = max(0, regret[i]) / sum(max(0, regret[j]))
 * If all regrets <= 0, use uniform distribution
 */
void regret_matching(const double* regrets, int num_actions, double* strategy);

/*
 * Strategy utilities
 */

/* Get average strategy (strategy_sum / visit_count) */
void get_average_strategy(const InfoSetData* data, double* avg_strategy);

/* Sample action from strategy (for external sampling MCCFR) */
Action sample_action(const InfoSetData* data, const double* strategy, double random_val);

/* Get best action (highest regret) for debug/display */
Action get_best_action(const InfoSetData* data);

/*
 * Table statistics
 */

/* Get number of entries */
size_t strategy_table_count(const StrategyTable* table);

/* Get memory usage in bytes */
size_t strategy_table_memory(const StrategyTable* table);

/* Print statistics */
void strategy_table_stats(const StrategyTable* table);

#endif /* STRATEGY_H */
