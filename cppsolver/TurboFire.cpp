#include <iostream>
#include "hands.hpp"
#include "solver.cpp"

//g++ -std=c++17 TurboFire.cpp hands.cpp -o turbofire

int main() {
	auto hr = Hands::parse_range(Hands::hero);
	auto vr = Hands::parse_range(Hands::villain);

	std::cout << "Hero range:\n";
	Hands::show_range(hr);
	std::cout << "Villain range:\n";
	Hands::show_range(vr);
		
	std::vector<float> p1combos(1326, 0.0f);
	std::vector<float> p2combos(1326, 0.0f);
	//the uint64 (52bit in reality) value for each idx 
	for (int i = 0; i <= 1326; i++) {
		p1combos[i] = 1;
		p2combos[i] = 1;
	}

	train(p1combos, p2combos);

	return 0;
}
