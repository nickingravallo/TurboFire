#pragma once
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cmath>

class Hands {
	private:
		static const std::unordered_map<char, int> cards;
		static const std::unordered_map<char, int> suits;
	public:
		static const std::unordered_map<std::string, double> hero; 
		static const std::unordered_map<std::string, double> villain; 

		static std::vector<double> parse_range(const std::unordered_map<std::string, double>& range);
		static void show_range(std::vector<double> range);
		static __attribute__((always_inline)) std::uint64_t card_bit(int card) {
			int rank = card % 13;
			int suit = card / 13;
			return 1ULL << (rank + suit * 16);
		}

		static inline __attribute__((always_inline)) std::uint64_t get_mask_for_combo(int combo_idx) {
			if (combo_idx < 0 || combo_idx >= 1326) {
				return 0;
			}
			int c1 = (int)std::floor((103.0 - std::sqrt(10609.0 - 8.0 * combo_idx)) / 2.0);
			int row_start = c1 * (103 - c1) / 2;
			int offset = combo_idx - row_start;
			int c2 = c1 + 1 + offset;
			return card_bit(c1) | card_bit(c2);
		}
};
