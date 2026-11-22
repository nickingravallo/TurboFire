#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace turbofire {

using Card = std::uint8_t;
using SuitPermutation = std::array<Card, 4>;

Card permute_card(Card card, const SuitPermutation& suits);

struct ChanceDeal {
    Card card;
    Card representative;
    SuitPermutation to_representative;
};

class ChanceIsomorphism {
public:
    ChanceIsomorphism(std::array<Card, 3> flop, const std::vector<float>& hero,
                      const std::vector<float>& villain);

    std::vector<ChanceDeal> deals(std::optional<Card> turn = std::nullopt) const;

private:
    std::array<Card, 3> flop_;
    std::vector<SuitPermutation> symmetries_;
};

}
