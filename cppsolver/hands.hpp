#pragma once
#include <iostream>
#include <unordered_map>

extern std::unordered_map<std::string, double> hero; 
extern std::unordered_map<std::string, double> villain; 

std::vector<double> parse_range(const std::unordered_map<std::string, double>& range);
