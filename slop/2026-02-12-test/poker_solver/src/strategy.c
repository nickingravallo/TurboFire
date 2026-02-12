/**
 * strategy.c - Strategy Table Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "strategy.h"

/* FNV-1a hash for 64-bit keys */
static uint64_t hash_key(uint64_t key) {
    /* Use the key directly since it should already be well-distributed */
    /* But apply some mixing for better distribution */
    key = (~key) + (key << 21);
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return key;
}

/* Create new strategy table */
StrategyTable* strategy_table_create(void) {
    StrategyTable* table = (StrategyTable*)malloc(sizeof(StrategyTable));
    if (!table) return NULL;
    
    table->capacity = INITIAL_TABLE_CAPACITY;
    table->count = 0;
    table->collisions = 0;
    
    table->entries = (InfoSetData*)calloc(table->capacity, sizeof(InfoSetData));
    if (!table->entries) {
        free(table);
        return NULL;
    }
    
    return table;
}

/* Free strategy table */
void strategy_table_free(StrategyTable* table) {
    if (table) {
        if (table->entries) {
            free(table->entries);
        }
        free(table);
    }
}

/* Clear all entries */
void strategy_table_clear(StrategyTable* table) {
    if (table && table->entries) {
        memset(table->entries, 0, table->capacity * sizeof(InfoSetData));
        table->count = 0;
        table->collisions = 0;
    }
}

/* Grow table when load factor exceeded */
static int strategy_table_grow(StrategyTable* table) {
    size_t old_capacity = table->capacity;
    InfoSetData* old_entries = table->entries;
    
    /* Double capacity */
    table->capacity *= 2;
    table->entries = (InfoSetData*)calloc(table->capacity, sizeof(InfoSetData));
    if (!table->entries) {
        table->entries = old_entries;
        table->capacity = old_capacity;
        return -1;
    }
    
    /* Rehash all entries */
    table->count = 0;
    table->collisions = 0;
    
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].occupied) {
            /* Find new slot */
            uint64_t h = hash_key(old_entries[i].key) % table->capacity;
            while (table->entries[h].occupied) {
                h = (h + 1) % table->capacity;
                table->collisions++;
            }
            
            memcpy(&table->entries[h], &old_entries[i], sizeof(InfoSetData));
            table->count++;
        }
    }
    
    free(old_entries);
    return 0;
}

/* Get or create info set */
InfoSetData* strategy_get(StrategyTable* table, uint64_t key,
                          const Action* actions, int num_actions) {
    /* Check load factor and grow if needed */
    if ((double)table->count / table->capacity > LOAD_FACTOR_THRESHOLD) {
        if (strategy_table_grow(table) != 0) {
            return NULL;
        }
    }
    
    /* Find slot */
    uint64_t h = hash_key(key) % table->capacity;
    size_t first_tombstone = table->capacity;
    
    while (table->entries[h].occupied) {
        if (table->entries[h].key == key) {
            /* Found existing entry */
            return &table->entries[h];
        }
        
        h = (h + 1) % table->capacity;
        table->collisions++;
        
        /* Prevent infinite loop */
        if (h == hash_key(key) % table->capacity) {
            return NULL;
        }
    }
    
    /* Create new entry */
    InfoSetData* entry = &table->entries[h];
    entry->key = key;
    entry->occupied = true;
    entry->num_actions = num_actions;
    entry->visit_count = 0;
    
    /* Copy valid actions */
    for (int i = 0; i < num_actions && i < MAX_INFO_SET_ACTIONS; i++) {
        entry->actions[i] = actions[i];
        entry->regrets[i] = 0.0;
        entry->strategy_sums[i] = 0.0;
    }
    
    table->count++;
    return entry;
}

/* Get existing info set */
InfoSetData* strategy_get_existing(StrategyTable* table, uint64_t key) {
    uint64_t h = hash_key(key) % table->capacity;
    
    while (table->entries[h].occupied) {
        if (table->entries[h].key == key) {
            return &table->entries[h];
        }
        h = (h + 1) % table->capacity;
        
        /* Prevent infinite loop */
        if (h == hash_key(key) % table->capacity) {
            break;
        }
    }
    
    return NULL;
}

/* Regret matching - compute strategy from regrets */
void regret_matching(const double* regrets, int num_actions, double* strategy) {
    double total_positive_regret = 0.0;
    
    /* Sum positive regrets */
    for (int i = 0; i < num_actions; i++) {
        if (regrets[i] > 0) {
            total_positive_regret += regrets[i];
        }
    }
    
    if (total_positive_regret > 0) {
        /* Normalize positive regrets */
        for (int i = 0; i < num_actions; i++) {
            strategy[i] = (regrets[i] > 0) ? regrets[i] / total_positive_regret : 0.0;
        }
    } else {
        /* Uniform distribution if no positive regrets */
        double uniform = 1.0 / num_actions;
        for (int i = 0; i < num_actions; i++) {
            strategy[i] = uniform;
        }
    }
}

/* Get average strategy */
void get_average_strategy(const InfoSetData* data, double* avg_strategy) {
    if (data->visit_count == 0) {
        /* Uniform if never visited */
        double uniform = 1.0 / data->num_actions;
        for (int i = 0; i < data->num_actions; i++) {
            avg_strategy[i] = uniform;
        }
        return;
    }
    
    for (int i = 0; i < data->num_actions; i++) {
        avg_strategy[i] = data->strategy_sums[i] / data->visit_count;
    }
}

/* Sample action from strategy */
Action sample_action(const InfoSetData* data, const double* strategy, double random_val) {
    double cum_prob = 0.0;
    
    for (int i = 0; i < data->num_actions; i++) {
        cum_prob += strategy[i];
        if (random_val <= cum_prob) {
            return data->actions[i];
        }
    }
    
    /* Fallback to last action due to floating point */
    return data->actions[data->num_actions - 1];
}

/* Get best action (highest regret) */
Action get_best_action(const InfoSetData* data) {
    double best_regret = -1e300;
    int best_idx = 0;
    
    for (int i = 0; i < data->num_actions; i++) {
        if (data->regrets[i] > best_regret) {
            best_regret = data->regrets[i];
            best_idx = i;
        }
    }
    
    return data->actions[best_idx];
}

/* Get entry count */
size_t strategy_table_count(const StrategyTable* table) {
    return table ? table->count : 0;
}

/* Get memory usage */
size_t strategy_table_memory(const StrategyTable* table) {
    if (!table) return 0;
    return sizeof(StrategyTable) + table->capacity * sizeof(InfoSetData);
}

/* Print statistics */
void strategy_table_stats(const StrategyTable* table) {
    if (!table) return;
    
    printf("Strategy Table Statistics:\n");
    printf("  Entries: %zu\n", table->count);
    printf("  Capacity: %zu\n", table->capacity);
    printf("  Load factor: %.2f%%\n", 
           100.0 * table->count / table->capacity);
    printf("  Collisions: %zu\n", table->collisions);
    printf("  Memory: %.2f MB\n", 
           strategy_table_memory(table) / (1024.0 * 1024.0));
}
