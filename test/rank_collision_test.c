#include "ranks.h"
#include <stdio.h>

extern uint16_t flush_map[];
extern uint16_t rank_map[];

#define FLUSH_TOTAL_COUNT 7099
#define  RANK_TOTAL_COUNT 49205

int main() {
	int i;
	int flush_count, rank_count;

	init_rank_map();
	init_flush_map();
	
	flush_count = 0;
	rank_count = 0;

	//flush test
	for ( i = 0; i < FLUSH_MAP_SIZE; i++)
		if (flush_map[i]) //it's populated!
			flush_count++;
	
	if (flush_count != FLUSH_TOTAL_COUNT)
		printf("[!] Flush test failed!\n");
	else
		printf("Flush test succeeded!\n");

	printf("Flush count: %d, expected: %d\n", flush_count, FLUSH_TOTAL_COUNT);

	//ranks test
	for ( i = 0; i < RANK_MAP_SIZE; i++)
		if (rank_map[i]) //it's populated!
			rank_count++;
	
	if (rank_count != RANK_TOTAL_COUNT)
		printf("[!] Rank test failed!\n");
	else
		printf("Rank test succeeded!\n");

	printf("Rank count: %d, expected: %d\n", rank_count, RANK_TOTAL_COUNT);
}

