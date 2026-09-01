#include <iostream>
#include <unordered_map>
#include <vector>

std::unordered_map<std::string, double> hero = {
	{"99+", 1.0},
	{"ATs+", 1.0},
	{"KTs+", 1.0},
	{"QJs+", 1.0}
};

std::unordered_map<std::string, double> villain = {
	{"99+", 1.0},
	{"ATs+", 1.0},
	{"KTs+", 1.0},
	{"QJs+", 1.0}
};

std::vector<double> parse_range(const std::unordered_map<std::string, double>& range) {
	bool isExtendedRange;
	bool isPair;
	bool isSuited;

	char c1;

	std::vector<double> out(1326, 0.0f);

	for (const auto& [hand, freq] : range) {
		std::cout << "Hand: " << hand << " freq: " << weight << "\n";

		isExtendedRange = false;
		isPair = false;
		isSuited = false;
		c1 = 0;

		for (char c : hand) {
			if (!c1)
				c1 = c; continue;
			if (c == '+')
				isExtendedRange = true;
			if (c == c1)
				isPair = true;
			if (c == 's')
				isSuited = true;
		}
	}

	return out;
}
