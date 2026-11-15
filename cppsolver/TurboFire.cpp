#include <iostream>
#include "hands.hpp"
#include "solver.hpp"

/*
g++ -std=c++17 TurboFire.cpp solver.cpp hands.cpp evaluator.cpp omp/HandEvaluator.cpp -o turbofire
*/

int main() {
	auto hr = Hands::parse_range(Hands::hero);
	auto vr = Hands::parse_range(Hands::villain);

	std::cout << "Hero range:\n";
	Hands::show_range(hr);
	std::cout << "Villain range:\n";
	Hands::show_range(vr);
		
	auto p1combos = Hands::parse_range_to_combos(hr);
	auto p2combos = Hands::parse_range_to_combos(vr);

	Solver::train(p1combos, p2combos);

	return 0;
}
