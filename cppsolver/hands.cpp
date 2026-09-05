#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>

#include "hands.hpp"

const std::unordered_map<std::string, double> Hands::hero = {
	{"99+", 1.0},
	{"ATs+", 1.0},
	{"KTs+", 1.0},
	{"QJs+", 1.0}
};

const std::unordered_map<std::string, double> Hands::villain = {
	{"99+", 1.0},
	{"ATs+", 1.0},
	{"KTs+", 1.0},
	{"QJs+", 1.0}
};

const std::unordered_map<char, int> Hands::cards = {
	{'A', 0}, {'K', 1}, {'Q', 2}, {'J', 3}, {'T', 4}, {'9', 5}, {'8', 6}, 
	{'7', 7}, {'6', 8}, {'5', 9}, {'4', 10}, {'3', 11}, {'2', 12}
};

std::vector<double> Hands::parse_range(const std::unordered_map<std::string, double>& range) {
	bool isExtendedRange;
	bool isPair;
	bool isSuited;

	char s1;
	char s2;

	std::vector<double> out(169, 0.0f);
	
	//(0,0) = AA, (12, 12) = 22 
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
		std::cout << "HERE\n";
		if (isPair)
			for (int i = c1i; i >= end_c1; i--)
				out[(12*i)+i] = freq;		
		else if (isSuited)
			for (int i = c2i; i >= end_c2; i--)
				out[(12*c1i)-i] = freq;	
		else
			for (int i = c1i; i >= end_c1; i--)
				out[(12*i)+c1i] = freq;
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
