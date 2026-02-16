#include "core/hand_evaluator.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <vector>

HandValue HandEvaluator::evaluate_7cards(const Card& c1, const Card& c2, const Card& c3,
                                          const Card& c4, const Card& c5, const Card& c6,
                                          const Card& c7) {
    std::vector<Card> cards = {c1, c2, c3, c4, c5, c6, c7};
    return evaluate_best_5_from_7(cards);
}

HandValue HandEvaluator::evaluate_5cards(const Card& c1, const Card& c2, const Card& c3,
                                          const Card& c4, const Card& c5) {
    std::vector<Card> cards = {c1, c2, c3, c4, c5};
    return evaluate_5card_hand(cards);
}

HandValue HandEvaluator::evaluate_with_board(const std::vector<Card>& hole_cards,
                                               const std::vector<Card>& board) {
    std::vector<Card> all_cards = hole_cards;
    all_cards.insert(all_cards.end(), board.begin(), board.end());
    
    if (all_cards.size() == 5) {
        return evaluate_5card_hand(all_cards);
    } else if (all_cards.size() == 7) {
        return evaluate_best_5_from_7(all_cards);
    }
    
    // Fallback
    return HandValue{HandRank::HIGH_CARD, 0};
}

HandValue HandEvaluator::evaluate_best_5_from_7(const std::vector<Card>& cards) {
    HandValue best = HandValue{HandRank::HIGH_CARD, 0};
    
    // Try all combinations of 5 cards from 7
    std::vector<int> indices = {0, 1, 2, 3, 4, 5, 6};
    
    for (int i = 0; i < 7; i++) {
        for (int j = i + 1; j < 7; j++) {
            std::vector<Card> combo;
            for (int k = 0; k < 7; k++) {
                if (k != i && k != j) {
                    combo.push_back(cards[k]);
                }
            }
            HandValue val = evaluate_5card_hand(combo);
            if (val > best) {
                best = val;
            }
        }
    }
    
    return best;
}

HandValue HandEvaluator::evaluate_5card_hand(const std::vector<Card>& cards) {
    if (cards.size() != 5) {
        return HandValue{HandRank::HIGH_CARD, 0};
    }
    
    std::vector<uint8_t> ranks;
    for (const auto& card : cards) {
        ranks.push_back(card.rank);
    }
    std::sort(ranks.rbegin(), ranks.rend());
    
    // Check for flush
    bool flush = is_flush(cards);
    
    // Check for straight
    bool straight = is_straight(ranks);
    
    // Check for straight flush
    if (flush && straight) {
        uint32_t value = ranks[0];  // High card of straight
        if (ranks == std::vector<uint8_t>{12, 3, 2, 1, 0}) {  // A-2-3-4-5 (wheel)
            value = 3;
        }
        return HandValue{HandRank::STRAIGHT_FLUSH, value};
    }
    
    // Get rank counts
    std::vector<int> rank_counts = get_rank_counts(cards);
    std::sort(rank_counts.rbegin(), rank_counts.rend());
    
    // Four of a kind
    if (rank_counts[0] == 4) {
        uint32_t value = 0;
        for (const auto& card : cards) {
            if (std::count(ranks.begin(), ranks.end(), card.rank) == 4) {
                value = card.rank;
                break;
            }
        }
        return HandValue{HandRank::FOUR_OF_A_KIND, value};
    }
    
    // Full house
    if (rank_counts[0] == 3 && rank_counts[1] == 2) {
        uint32_t value = 0;
        for (const auto& card : cards) {
            if (std::count(ranks.begin(), ranks.end(), card.rank) == 3) {
                value = card.rank;
                break;
            }
        }
        return HandValue{HandRank::FULL_HOUSE, value};
    }
    
    // Flush
    if (flush) {
        uint32_t value = 0;
        for (int i = 0; i < 5; i++) {
            value = (value << 4) | ranks[i];
        }
        return HandValue{HandRank::FLUSH, value};
    }
    
    // Straight
    if (straight) {
        uint32_t value = ranks[0];
        if (ranks == std::vector<uint8_t>{12, 3, 2, 1, 0}) {  // Wheel
            value = 3;
        }
        return HandValue{HandRank::STRAIGHT, value};
    }
    
    // Three of a kind
    if (rank_counts[0] == 3) {
        uint32_t value = 0;
        for (const auto& card : cards) {
            if (std::count(ranks.begin(), ranks.end(), card.rank) == 3) {
                value = card.rank;
                break;
            }
        }
        return HandValue{HandRank::THREE_OF_A_KIND, value};
    }
    
    // Two pair
    if (rank_counts[0] == 2 && rank_counts[1] == 2) {
        uint32_t value = 0;
        std::set<uint8_t> pairs;
        for (const auto& card : cards) {
            if (std::count(ranks.begin(), ranks.end(), card.rank) == 2) {
                pairs.insert(card.rank);
            }
        }
        auto it = pairs.rbegin();
        value = *it;
        value = (value << 4) | *(++it);
        return HandValue{HandRank::TWO_PAIR, value};
    }
    
    // Pair
    if (rank_counts[0] == 2) {
        uint32_t value = 0;
        for (const auto& card : cards) {
            if (std::count(ranks.begin(), ranks.end(), card.rank) == 2) {
                value = card.rank;
                break;
            }
        }
        return HandValue{HandRank::PAIR, value};
    }
    
    // High card
    uint32_t value = 0;
    for (int i = 0; i < 5; i++) {
        value = (value << 4) | ranks[i];
    }
    return HandValue{HandRank::HIGH_CARD, value};
}

bool HandEvaluator::is_flush(const std::vector<Card>& cards) {
    if (cards.size() < 5) return false;
    
    int suit_counts[4] = {0};
    for (const auto& card : cards) {
        suit_counts[card.suit]++;
    }
    
    for (int i = 0; i < 4; i++) {
        if (suit_counts[i] >= 5) {
            return true;
        }
    }
    return false;
}

bool HandEvaluator::is_straight(const std::vector<uint8_t>& ranks) {
    if (ranks.size() < 5) return false;
    
    std::set<uint8_t> unique_ranks(ranks.begin(), ranks.end());
    std::vector<uint8_t> sorted(unique_ranks.begin(), unique_ranks.end());
    std::sort(sorted.begin(), sorted.end());
    
    // Check for regular straight
    for (size_t i = 0; i <= sorted.size() - 5; i++) {
        bool is_straight_seq = true;
        for (size_t j = 1; j < 5; j++) {
            if (sorted[i + j] != sorted[i] + j) {
                is_straight_seq = false;
                break;
            }
        }
        if (is_straight_seq) return true;
    }
    
    // Check for wheel (A-2-3-4-5)
    if (unique_ranks.count(12) && unique_ranks.count(0) && 
        unique_ranks.count(1) && unique_ranks.count(2) && unique_ranks.count(3)) {
        return true;
    }
    
    return false;
}

std::vector<int> HandEvaluator::get_rank_counts(const std::vector<Card>& cards) {
    std::map<uint8_t, int> counts;
    for (const auto& card : cards) {
        counts[card.rank]++;
    }
    
    std::vector<int> result;
    for (const auto& pair : counts) {
        result.push_back(pair.second);
    }
    return result;
}

int HandEvaluator::compare_hands(const HandValue& h1, const HandValue& h2) {
    if (h1 > h2) return 1;
    if (h2 > h1) return -1;
    return 0;
}
