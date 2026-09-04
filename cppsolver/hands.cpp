#include <iostream>
#include <iomanip>
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

	std::vector<double> out(169, 0.0f);
	
	//(0,0) = AA, (12, 12) = 22 
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
				s2 = c; continue;
			if (c == '+')
				isExtendedRange = true;
			if (c == s1)
				isPair = true;
			if (c == 's')
				isSuited = true;
		}
	
		int c1i = cards.at(s1);
		int c2i = cards.at(s2);
		/*  0  1  2  3  4
		 *0 AA AK AQ AJ AT A9...
		 *1 AK KK
		 *2 AQ   QQ
		 *3 AJ      JJ
		 */
		int end_c1 = isExtendedRange ? 0 : c1i;
		int end_c2 = isExtendedRange ? 0 : c2i;
		if (isPair)
			for (int i = c1i; i >= end_c1; i--)
				rangemap[i][i] = freq;		
		else if (isSuited)
			for (int i = c2i; i >= end_c2; i--)
				rangemap[c1i][i] = freq;	
		else
			for (int i = c1i; i >= end_c1; i--)
				rangemap[i][c2i] = freq;
	}

	return out;
}

void show_range(std::vector<double> range)
{
	if (range.empty()) {
		std::cout << "Range is NULL or empty!" << "\n";
		return;
	}

	int nl = 0;
	for (auto freq : range) {
		std::cout << std::fixed << std::setprecision(2) << freq << " ";
		if (nl == 12) {
			std::cout << "\n"; 
			nl = 0;
		}
		nl++;
	}
}
