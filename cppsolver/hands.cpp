#include "hands.hpp"

#define NUM_CARDS = 13;
#define NUM_SUITS = 4;

const std::unordered_map<std::string, double> Hands::hero = {
	{"99+", 1.0},
	{"KTs+", 1.0},
	{"A7+", 0.95}
};

const std::unordered_map<std::string, double> Hands::villain = {
	{"99+", 1.0},
	{"ATs+", 1.0},
	{"KTs+", 1.0},
	{"QJs+", 1.0}
};

const std::unordered_map<char, int> Hands::cards = {
	{'A', 12}, {'K', 11}, {'Q', 10}, {'J', 9}, {'T', 8}, {'9', 7}, {'8', 6}, 
	{'7', 5}, {'6', 4}, {'5', 3}, {'4', 2}, {'3', 1}, {'2', 0},
};

//in OMP -> s=0, h=1, d=2, c=3
const std::unordered_map<char, int> Hands::suits = {
	{'C', 3}, {'D', 2}, {'H', 1}, {'S', 0},
};

/*
 * suited => sum(1, 12) => 78 * 4  = 312
 * 2c2d 2c2h 2c2s
 * 2d2h 2d2s
 * 2h2s
 * pair   =>               13 * 6  = 78 
 * AcKd AcKh AcKs
 * AdKc AdKh AdKs
 * AhKc AhKd AhKs
 * AsKc AsKd AsKs 
 * suited =>               78 * 12 = 936
 */
int count_combos_for_hand(char c1, char c2, bool suited) {
	if (suited)
		return 6;
	if (c1 != c2)
		return 12;
	return 4;
}

std::vector<double> parse_range_to_combos(std::vector<double> isorange) {
	std::vector<double> out(1326, 0.0f);
	return out;
}

std::vector<double> Hands::parse_range(const std::unordered_map<std::string, double>& range) {
	bool isExtendedRange;
	bool isPair;
	bool isSuited;

	char s1;
	char s2;

	std::vector<double> out(169, 0.0f);
	
	for (const auto& [hand, freq] : range) {
		std::cout << "Hand: " << hand << " freq: " << freq << "\n";

		isExtendedRange = false;
		isPair = false;
		isSuited = false;
		s1 = s2 = 0;

		for (char c : hand) {
			if (!s1) 
				{ s1 = c; continue; }
			if (c == s1)
				isPair = true;
			if (!s2)
				{ s2 = c; continue; }
			if (c == '+')
				isExtendedRange = true;
			if (c == 's')
				isSuited = true;
		}
	
		std::cout << s1 << s2 << "<HAND\n";
		int c1i = 12 - cards.at(s1);
		int c2i = 12 - cards.at(s2);

		int end_c1 = isExtendedRange ? 0 : c1i;
		int end_c2 = isExtendedRange ? (c1i + 1) : c2i;
		
		std::cout << "HERE\n";
		if (isPair)
			for (int i = c1i; i >= end_c1; i--)
				out[(13 * i) + i] = freq;		
		else if (isSuited)
			for (int i = c2i; i >= end_c2; i--)
				out[(13 * i) + c1i] = freq;	
		else
			for (int i = c2i; i >= end_c2; i--)
				out[(13 * c1i) + i] = freq;
	}

	return out;
}

void Hands::show_range(std::vector<double> range)
{
	if (range.empty()) {
		std::cout << "Range is NULL or empty!" << "\n";
		return;
	}

	int nl = 1;
	for (auto freq : range) {
		std::cout << std::fixed << std::setprecision(2) << freq << " ";
		if (nl == 13) {
			std::cout << "\n"; 
			nl = 0;
		}
		nl++;
	}
}

