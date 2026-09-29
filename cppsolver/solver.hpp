#pragma once

#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cmath>

#define FLOP  0
#define TURN  1
#define RIVER 2

#define P1 0
#define P2 1

struct GameState {
	int street;
	int pot;
	int p1commit;
	int p2commit;
	int p1stack;
	int p2stack;
};
