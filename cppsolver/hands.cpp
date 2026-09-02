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

std::unordered_map<char, int> cards = {
	{'A', 0}, {'2', 1}, {'3', 2}, {'4', 3}, {'5', 4}, {'6', 5}, {'7', 6}, 
	{'8', 7}, {'9', 8}, {'T', 9}, {'J', 10}, {'Q', 11}, {'K', 12}
};

std::vector<double> parse_range(const std::unordered_map<std::string, double>& range) {
	bool isExtendedRange;
	bool isPair;
	bool isSuited;

	char s1;
	char s2;

	std::vector<double> out(1326, 0.0f);
	
	//(0,0) = 22, (12, 12) = AA 
	std::vector<std::vector<double>> rangemap(13, std::vector<double>(13, 0.0f));

	for (const auto& [hand, freq] : range) {
		std::cout << "Hand: " << hand << " freq: " << freq << "\n";

		isExtendedRange = false;
		isPair = false;
		isSuited = false;
		s1 = s2 = 0;

		for (char c : hand) {
			if (!s1)
				s1 = c; continue;
			if (!s2)
				s2 = c; continue
			if (c == '+')
				isExtendedRange = true;
			if (c == s1)
				isPair = true;
			if (c == 's')
				isSuited = true;
		}
	
		int index = cards.at(s1)
		int end = isExtendedRange ? cards.size() : index+1;
		if (isPair)
			for (int i = index; i < end; i++)
				rangemap[i][i] = freq;		
		//else if (isSuited)
	}

	return out;
}
