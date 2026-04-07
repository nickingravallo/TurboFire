#include "parse.h"
#include "evaluator.h"

#include <stdint.h>

int main(int argc, char **argv) {
	init_evaluator();
	uint64_t board = parse_board_string("AsKd4h");	
}
