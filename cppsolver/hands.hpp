#pragma once
#include <iostream>
#include <unordered_map>
#include <vector>

class Hands {
private:
	static const std::unordered_map<char, int> cards;
	static const std::unordered_map<char, int> suits;
public:
	static const std::unordered_map<std::string, double> hero; 
	static const std::unordered_map<std::string, double> villain; 

	static std::vector<double> parse_range(const std::unordered_map<std::string, double>& range);
	static void show_range(std::vector<double> range);
};
