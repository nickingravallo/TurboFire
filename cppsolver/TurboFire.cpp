#include <iostream>
#include "hands.hpp"

//g++ -std=c++17 TurboFire.cpp hands.cpp -o turbofire

int main() {
	auto hr = Hands::parse_range(Hands::hero);
	auto vr = Hands::parse_range(Hands::villain);
	
	std::cout << "Hero range:\n";
	Hands::show_range(hr);
	std::cout << "Villain range:\n";
	Hands::show_range(vr);
	return 0;
}
