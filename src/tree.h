#ifndef TREE_H
#define TREE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdalign.h>

typedef enum {
	NODE_ACTION,
	NODE_CHANCE,
	NODE_TERMINAL
} NodeType;

typedef struct PublicNode {
	NodeType type;
	uint8_t active_player;
	uint8_t num_children;

	struct PublicNode** children;

	//for action nodes only
	alignas(32) float* regret_sum;
	alignas(32) float* strategy_sum;

	//for chance nodes
	int* dealt_cards;
	float* chance_weights;
} PublicNode;

typedef struct {
	uint8_t* memory;
	size_t capacity;
	size_t offset;
} Arena;

void arena_init(Arena* a, size_t size);

void* arena_alloc(Arena* a, size_t size);

#endif //TREE_H
