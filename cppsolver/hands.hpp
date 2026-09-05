#pragma once
#include <iostream>
#include <unordered_map>

class Hands {
private:
	static const std::unordered_map<char, int> cards;
public:
	static const std::unordered_map<std::string, double> hero; 
	static const std::unordered_map<std::string, double> villain; 

	static std::vector<double> parse_range(const std::unordered_map<std::string, double>& range);
	static void show_range(std::vector<double> range);
};
