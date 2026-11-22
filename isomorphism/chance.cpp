#include "chance.hpp"

#include <algorithm>
#include <stdexcept>

namespace turbofire {

Card permute_card(Card card, const SuitPermutation& suits) {
    return suits.at(card / 13) * 13 + card % 13;
}

namespace {

bool preserves_ranges(const SuitPermutation& suits, const std::vector<float>& hero,
                      const std::vector<float>& villain) {
    int combo = 0;
    for (Card first = 0; first < 52; ++first) {
        for (Card second = first + 1; second < 52; ++second, ++combo) {
            int a = permute_card(first, suits);
            int b = permute_card(second, suits);
            if (a > b)
                std::swap(a, b);
            int mapped = a * (103 - a) / 2 + b - a - 1;
            if (hero[combo] != hero[mapped] || villain[combo] != villain[mapped])
                return false;
        }
    }
    return true;
}

}

ChanceIsomorphism::ChanceIsomorphism(std::array<Card, 3> flop, const std::vector<float>& hero,
                                     const std::vector<float>& villain) : flop_(flop) {
    std::sort(flop_.begin(), flop_.end());
    if (flop_.back() >= 52 || std::adjacent_find(flop_.begin(), flop_.end()) != flop_.end())
        throw std::invalid_argument("Expected three distinct flop cards in the deck");
    if (hero.size() != 1326 || villain.size() != 1326)
        throw std::invalid_argument("Expected 1326 combo weights per player");

    SuitPermutation suits{0, 1, 2, 3};
    do {
        auto mapped = flop_;
        for (Card& card : mapped)
            card = permute_card(card, suits);
        std::sort(mapped.begin(), mapped.end());
        if (mapped == flop_ && preserves_ranges(suits, hero, villain))
            symmetries_.push_back(suits);
    } while (std::next_permutation(suits.begin(), suits.end()));
    if (symmetries_.empty())
        throw std::invalid_argument("Range weights must be comparable");
}

std::vector<ChanceDeal> ChanceIsomorphism::deals(std::optional<Card> turn) const {
    auto on_flop = [&](Card card) { return std::binary_search(flop_.begin(), flop_.end(), card); };
    if (turn && (*turn >= 52 || on_flop(*turn)))
        throw std::invalid_argument("Expected a legal turn card");

    std::vector<ChanceDeal> result;
    result.reserve(turn ? 48 : 49);
    for (Card card = 0; card < 52; ++card) {
        if (on_flop(card) || turn == card)
            continue;
        ChanceDeal deal{card, card, {0, 1, 2, 3}};
        for (const auto& suits : symmetries_) {
            if (turn && permute_card(*turn, suits) != *turn)
                continue;
            Card mapped = permute_card(card, suits);
            if (mapped < deal.representative) {
                deal.representative = mapped;
                deal.to_representative = suits;
            }
        }
        result.push_back(deal);
    }
    return result;
}

}
