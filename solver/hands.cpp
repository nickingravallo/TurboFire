#include "hands.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

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

const std::unordered_map<char, int> Hands::suits = {
	{'C', 3}, {'D', 2}, {'H', 1}, {'S', 0},
};

std::vector<float> Hands::load_range(const std::filesystem::path& path, std::uint64_t dead_cards) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open range file " + path.string());

    std::unordered_map<std::string, double> weights;
    std::string line;
    std::size_t row = 0;
    bool positive = false;
    while (std::getline(file, line)) {
        ++row;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto error = [&](const char* message) {
            return std::runtime_error(path.string() + ":" + std::to_string(row) + ": " + message);
        };
        if (row == 1) {
            if (line != "hand,weight")
                throw error("Expected hand,weight header");
            continue;
        }
        if (line.empty())
            continue;
        auto comma = line.find(',');
        if (comma == std::string::npos)
            throw error("Expected hand,weight");
        auto hand = line.substr(0, comma);
        if (hand.size() < 2 || !cards.count(hand[0]) || !cards.count(hand[1]))
            throw error("Invalid hand class");
        bool pair = hand.size() == 2 && hand[0] == hand[1];
        bool nonpair = hand.size() == 3 && cards.at(hand[0]) > cards.at(hand[1])
                       && (hand[2] == 's' || hand[2] == 'o');
        if (!pair && !nonpair)
            throw error("Expected a class such as AA, AKs or AKo");
        std::istringstream field(line.substr(comma + 1));
        double weight;
        if (!(field >> weight) || !(field >> std::ws).eof() ||
            !std::isfinite(weight) || weight < 0 || weight > 1)
            throw error("Expected a weight between 0 and 1");
        if (!weights.emplace(hand, weight).second)
            throw error("Duplicate hand class");
        positive |= weight > 0;
    }
    if (file.bad())
        throw std::runtime_error("Cannot read range file " + path.string());
    if (!positive)
        throw std::runtime_error("No positive weights in " + path.string());
    return parse_range_to_combos(parse_range(weights), dead_cards);
}

std::vector<float> Hands::parse_range_to_combos(const std::vector<double>& range, std::uint64_t dead_cards) {
	if (range.size() != 169)
		throw std::invalid_argument("Expected 169 range entries");
	std::vector<float> out(1326, 0.0f);
	int combo = 0;
	for (int c1 = 0; c1 < 52; c1++) {
		for (int c2 = c1 + 1; c2 < 52; c2++, combo++) {
			if ((card_bit(c1) | card_bit(c2)) & dead_cards)
				continue;
			const int high = 12 - std::max(c1 % 13, c2 % 13);
			const int low = 12 - std::min(c1 % 13, c2 % 13);
			const int index = c1 / 13 == c2 / 13 ? 13 * low + high : 13 * high + low;
			out[combo] = range[index];
		}
	}
	return out;
}

std::vector<uint8_t> Hands::available_cards(std::uint64_t dead_cards) {
	std::vector<uint8_t> deck;
	for (int card = 0; card < 52; card++)
		if (!(card_bit(card) & dead_cards))
			deck.push_back(card);
	return deck;
}

std::uint64_t Hands::deal_cards(int count, std::uint64_t dead_cards, std::mt19937& rng) {
	auto deck = available_cards(dead_cards);
	if (count < 0 || count > static_cast<int>(deck.size()))
		throw std::invalid_argument("Not enough cards to deal");
	std::shuffle(deck.begin(), deck.end(), rng);
	std::uint64_t dealt = 0;
	for (int i = 0; i < count; i++)
		dealt |= card_bit(deck[i]);
	return dealt;
}

std::vector<double> Hands::parse_range(const std::unordered_map<std::string, double>& range) {
	bool isExtendedRange;
	bool isPair;
	bool isSuited;

	char s1;
	char s2;

	std::vector<double> out(169, 0.0f);

	for (const auto& [hand, freq] : range) {

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

		int c1i = 12 - cards.at(s1);
		int c2i = 12 - cards.at(s2);

		int end_c1 = isExtendedRange ? 0 : c1i;
		int end_c2 = isExtendedRange ? (c1i + 1) : c2i;

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
