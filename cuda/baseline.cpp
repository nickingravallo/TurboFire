#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace experiment {
using Clock = std::chrono::steady_clock;
using Vec = std::vector<float>;
volatile double sink = 0;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void close(double got, double expected, const std::string& label, double tolerance = 4e-4) {
    require(std::isfinite(got) && std::abs(got - expected) <= tolerance * (1 + std::abs(expected)),
            label + ": got=" + std::to_string(got) + " expected=" + std::to_string(expected));
}
void assert_vec(const Vec& a, const Vec& b, const std::string& label, double tolerance = 4e-4) {
    require(a.size() == b.size(), label + " size");
    for (size_t i = 0; i < a.size(); ++i) close(a[i], b[i], label + "[" + std::to_string(i) + "]", tolerance);
}
double checksum(const Vec& a) { double s = 0; for (size_t i = 0; i < a.size(); ++i) s += a[i] * (1 + i % 13); return s; }
struct Discount {
    float positive = 1, negative = 1, average = 1;
    static Discount dcfr(int t) {
        require(t > 0, "DCFR iteration must be positive");
        double p = std::pow(double(t), 1.5);
        return {float(p / (p + 1)), 0.5f, float(std::pow(double(t) / (t + 1), 2))};
    }
};
struct Action {
    uint64_t id;
    int n, actions;
    Vec regret, average, child, reach, strategy, next_reach, cf;
    Action(int count, int a, uint64_t identity = 0): id(identity), n(count), actions(a),
        regret(size_t(count) * a), average(regret.size()), child(regret.size()), reach(count),
        strategy(regret.size()), next_reach(regret.size()), cf(count) {}
};
Action make_action(int n, int a, uint64_t id) {
    Action x(n, a, id);
    std::mt19937 rng(7301 + unsigned(id));
    std::uniform_real_distribution<float> u(0, 1);
    for (int c = 0; c < n; ++c) {
        x.reach[c] = c % 17 == 0 ? 0 : u(rng);
        for (int k = 0; k < a; ++k) {
            size_t j = size_t(k) * n + c;
            x.regret[j] = c % 7 == 0 ? -u(rng) * 10 : (u(rng) - .45f) * 20;
            x.average[j] = u(rng) * 2;
            x.child[j] = (u(rng) - .5f) * 100;
        }
    }
    return x;
}
#if defined(__GNUC__) && !defined(__CUDACC__)
__attribute__((optimize("no-tree-vectorize"), noinline))
#endif
void action_scalar(Action& x, Discount d = {}) {
    for (int c = 0; c < x.n; ++c) {
        float sum = 0;
        for (int a = 0; a < x.actions; ++a) sum += std::max(0.f, x.regret[size_t(a) * x.n + c]);
        float value = 0;
        for (int a = 0; a < x.actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            float p = sum > 0 ? std::max(0.f, x.regret[j]) / sum : 1.f / x.actions;
            x.strategy[j] = p;
            x.next_reach[j] = x.reach[c] * p;
            value += p * x.child[j];
        }
        x.cf[c] = value;
        for (int a = 0; a < x.actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            float r = x.regret[j] + (x.child[j] - value);
            x.regret[j] = r * (r > 0 ? d.positive : d.negative);
            x.average[j] = (x.average[j] + x.strategy[j]) * d.average;
        }
    }
}
void action_oracle(Action& x, Discount d = {}) {
    std::vector<double> probs(x.actions);
    for (int c = 0; c < x.n; ++c) {
        double total = 0;
        for (int a = 0; a < x.actions; ++a) total += std::max(0., double(x.regret[size_t(a) * x.n + c]));
        double value = 0;
        for (int a = 0; a < x.actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            probs[a] = total == 0 ? 1. / x.actions : std::max(0., double(x.regret[j])) / total;
            value += probs[a] * x.child[j];
            x.strategy[j] = float(probs[a]);
            x.next_reach[j] = float(probs[a] * x.reach[c]);
        }
        x.cf[c] = float(value);
        for (int a = 0; a < x.actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            double r = double(x.regret[j]) + x.child[j] - value;
            x.regret[j] = float(r * (r > 0 ? d.positive : d.negative));
            x.average[j] = float((double(x.average[j]) + probs[a]) * d.average);
        }
    }
}
void check_action(const Action& x, const Action& y) {
    assert_vec(x.strategy, y.strategy, "strategy"); assert_vec(x.regret, y.regret, "regret");
    assert_vec(x.average, y.average, "average"); assert_vec(x.next_reach, y.next_reach, "reach"); assert_vec(x.cf, y.cf, "action CF");
    for (int c = 0; c < x.n; ++c) {
        double sum = 0;
        for (int a = 0; a < x.actions; ++a) {
            float p = x.strategy[size_t(a) * x.n + c]; require(p >= 0 && p <= 1, "strategy bounds"); sum += p;
        }
        close(sum, 1, "strategy normalization", 2e-6);
    }
}
uint64_t card_mask(int card) { return uint64_t(1) << ((card / 13) * 16 + card % 13); }
struct Hand { uint64_t mask; int c0, c1, rank; };
struct Terminal {
    uint64_t id, board;
    int pot = 81, p1commit = 20, p2commit = 30, active = 0, traverser = 0;
    bool fold = false;
    std::vector<Hand> hands;
    Vec p1, p2, cf;
    const Vec& villain() const { return traverser == 0 ? p2 : p1; }
    int fold_payoff() const { return active == traverser ? (active == 0 ? p2commit : p1commit) : -(traverser == 0 ? p1commit : p2commit); }
};
Terminal make_terminal(int n, uint64_t id) {
    require(n > 0 && n <= 1326, "terminal combo count");
    Terminal x; x.id = id; x.board = card_mask(2) | card_mask(17) | card_mask(29) | card_mask(43) | card_mask(9);
    std::vector<Hand> all;
    for (int i = 0; i < 52; ++i) for (int j = i + 1; j < 52; ++j)
        all.push_back({card_mask(i) | card_mask(j), i, j, (i * 977 + j * 127 + int(id % 11) * 19) % 7462});
    std::mt19937 rng(1827 + unsigned(id)); std::shuffle(all.begin(), all.end(), rng);
    x.hands.assign(all.begin(), all.begin() + n); x.p1.resize(n); x.p2.resize(n); x.cf.resize(n);
    std::uniform_real_distribution<float> u(0, 1);
    for (int i = 0; i < n; ++i) { x.p1[i] = i % 11 == 0 ? 0 : u(rng); x.p2[i] = i % 13 == 0 ? 0 : u(rng); }
    return x;
}
int outcome(const Terminal& x, size_t h, size_t v) {
    auto a = x.hands[h], b = x.hands[v];
    if ((a.mask & b.mask) || ((a.mask | b.mask) & x.board)) return 0;
    return x.fold ? 1 : int(a.rank > b.rank) - int(a.rank < b.rank);
}
#if defined(__GNUC__) && !defined(__CUDACC__)
__attribute__((optimize("no-tree-vectorize"), noinline))
#endif
void terminal_scalar(Terminal& x) {
    float payoff = x.fold ? x.fold_payoff() : x.pot / 2;
    const auto& reach = x.villain();
    for (size_t h = 0; h < x.hands.size(); ++h) {
        float s = 0;
        for (size_t v = 0; v < x.hands.size(); ++v) s += reach[v] * (outcome(x, h, v) * payoff);
        x.cf[h] = s;
    }
}
void terminal_oracle(Terminal& x) {
    std::vector<double> values(x.hands.size());
    for (size_t p1 = 0; p1 < x.hands.size(); ++p1) for (size_t p2 = 0; p2 < x.hands.size(); ++p2) {
        const auto& a = x.hands[p1]; const auto& b = x.hands[p2];
        if ((a.mask & b.mask) != 0 || (a.mask & x.board) != 0 || (b.mask & x.board) != 0) continue;
        double pay = x.fold ? x.fold_payoff() : (x.pot / 2) * (int(a.rank > b.rank) - int(a.rank < b.rank)) * (x.traverser == 0 ? 1 : -1);
        if (x.traverser == 0) values[p1] += double(x.p2[p2]) * pay;
        else values[p2] += double(x.p1[p1]) * pay;
    }
    for (size_t i = 0; i < values.size(); ++i) x.cf[i] = float(values[i]);
}
struct TerminalCache {
    uint64_t board = ~uint64_t(0);
    bool fold = false;
    std::vector<Hand> hands;
    std::vector<int8_t> signs;
    size_t hits = 0, misses = 0;
    bool matches(const Terminal& x) const {
        if (board != x.board || fold != x.fold || hands.size() != x.hands.size()) return false;
        for (size_t i = 0; i < hands.size(); ++i) if (hands[i].mask != x.hands[i].mask || hands[i].rank != x.hands[i].rank) return false;
        return true;
    }
    void ensure(const Terminal& x) {
        if (matches(x)) { ++hits; return; }
        ++misses; board = x.board; fold = x.fold; hands = x.hands;
        signs.resize(hands.size() * hands.size());
        for (size_t h = 0; h < hands.size(); ++h) for (size_t v = 0; v < hands.size(); ++v) signs[h * hands.size() + v] = int8_t(outcome(x, h, v));
    }
    void apply(Terminal& x) {
        ensure(x); float pay = x.fold ? x.fold_payoff() : x.pot / 2;
        const auto& reach = x.villain(); size_t n = hands.size();
        for (size_t h = 0; h < n; ++h) {
            float sum = 0; for (size_t v = 0; v < n; ++v) sum += reach[v] * (signs[h * n + v] * pay); x.cf[h] = sum;
        }
    }
};
void terminal_prefix(Terminal& x) {
    size_t n = x.hands.size();
    std::vector<int> order(n); std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return x.hands[a].rank < x.hands[b].rank; });
    std::vector<std::array<double, 53>> prefix(n + 1);
    std::vector<int> ranks(n);
    const auto& reach = x.villain();
    for (size_t k = 0; k < n; ++k) {
        int v = order[k]; const auto& h = x.hands[v]; prefix[k + 1] = prefix[k]; ranks[k] = h.rank;
        if (h.mask & x.board) continue;
        prefix[k + 1][0] += reach[v]; prefix[k + 1][h.c0 + 1] += reach[v]; prefix[k + 1][h.c1 + 1] += reach[v];
    }
    for (size_t h = 0; h < n; ++h) {
        const auto& hand = x.hands[h]; if (hand.mask & x.board) { x.cf[h] = 0; continue; }
        auto compatible = [&](size_t k) { return prefix[k][0] - prefix[k][hand.c0 + 1] - prefix[k][hand.c1 + 1]; };
        if (x.fold) x.cf[h] = float((compatible(n) + reach[h]) * x.fold_payoff());
        else {
            size_t lo = std::lower_bound(ranks.begin(), ranks.end(), hand.rank) - ranks.begin();
            size_t hi = std::upper_bound(ranks.begin(), ranks.end(), hand.rank) - ranks.begin();
            x.cf[h] = float((compatible(lo) - (compatible(n) - compatible(hi))) * (x.pot / 2));
        }
    }
}
struct TerminalOrder {
    TerminalCache key;
    std::vector<int> order;
    std::vector<double> lower;
    void ensure(const Terminal& x) {
        if (key.matches(x)) { ++key.hits; return; }
        ++key.misses; key.board = x.board; key.fold = x.fold; key.hands = x.hands;
        order.resize(x.hands.size()); lower.resize(x.hands.size()); std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return x.hands[a].rank < x.hands[b].rank; });
    }
    void apply(Terminal& x) {
        ensure(x); const auto& reach = x.villain(); size_t n = x.hands.size();
        std::array<double, 53> total{}, prefix{};
        for (size_t v = 0; v < n; ++v) {
            const auto& h = x.hands[v]; if (h.mask & x.board) continue;
            total[0] += reach[v]; total[h.c0 + 1] += reach[v]; total[h.c1 + 1] += reach[v];
        }
        if (x.fold) {
            for (size_t h = 0; h < n; ++h) {
                const auto& hand = x.hands[h];
                x.cf[h] = hand.mask & x.board ? 0 : float((total[0] - total[hand.c0 + 1] - total[hand.c1 + 1] + reach[h]) * x.fold_payoff());
            }
            return;
        }
        for (size_t begin = 0; begin < n;) {
            size_t end = begin + 1;
            while (end < n && x.hands[order[end]].rank == x.hands[order[begin]].rank) ++end;
            for (size_t k = begin; k < end; ++k) {
                int h = order[k]; const auto& hand = x.hands[h];
                lower[h] = prefix[0] - prefix[hand.c0 + 1] - prefix[hand.c1 + 1];
            }
            for (size_t k = begin; k < end; ++k) {
                int v = order[k]; const auto& h = x.hands[v]; if (h.mask & x.board) continue;
                prefix[0] += reach[v]; prefix[h.c0 + 1] += reach[v]; prefix[h.c1 + 1] += reach[v];
            }
            for (size_t k = begin; k < end; ++k) {
                int h = order[k]; const auto& hand = x.hands[h]; if (hand.mask & x.board) { x.cf[h] = 0; continue; }
                double greater = (total[0] - prefix[0]) - (total[hand.c0 + 1] - prefix[hand.c0 + 1]) - (total[hand.c1 + 1] - prefix[hand.c1 + 1]);
                x.cf[h] = float((lower[h] - greater) * (x.pot / 2));
            }
            begin = end;
        }
    }
};
void terminal_sweep(Terminal& x) { TerminalOrder order; order.apply(x); }
struct QueueStats { size_t hits = 0, misses = 0, evictions = 0, flushes = 0, peak_pending = 0; };
template<class Backend> class ResidentQueue {
    struct Entry { Action* host; size_t slot, age; };
    Backend& backend;
    size_t capacity, batch, tick = 0;
    Discount discount;
    std::unordered_map<uint64_t, Entry> resident;
    std::vector<size_t> pending;
    std::vector<Action*> outputs;
public:
    QueueStats stats;
    ResidentQueue(Backend& b, size_t slots, size_t batch_size, Discount d): backend(b), capacity(slots), batch(batch_size), discount(d) {
        require(slots > 0 && batch_size > 0 && batch_size <= slots, "queue capacity");
        resident.reserve(slots); pending.reserve(batch); outputs.reserve(batch);
    }
    void flush() {
        if (pending.empty()) return;
        backend.launch(pending, outputs, discount); ++stats.flushes; pending.clear(); outputs.clear();
    }
    void submit(Action& node) {
        for (auto* p : outputs) if (p->id == node.id) { flush(); break; }
        auto it = resident.find(node.id);
        if (it == resident.end()) {
            size_t slot = resident.size();
            if (resident.size() == capacity) {
                flush();
                auto victim = std::min_element(resident.begin(), resident.end(), [](const auto& a, const auto& b) { return a.second.age < b.second.age; });
                slot = victim->second.slot; backend.writeback(slot, *victim->second.host); resident.erase(victim); ++stats.evictions;
            }
            backend.load(slot, node); it = resident.emplace(node.id, Entry{&node, slot, ++tick}).first; ++stats.misses;
        } else {
            require(it->second.host == &node, "duplicate ID refers to another node");
            backend.refresh(it->second.slot, node); ++stats.hits; it->second.age = ++tick;
        }
        pending.push_back(it->second.slot); outputs.push_back(&node);
        stats.peak_pending = std::max(stats.peak_pending, pending.size());
        if (pending.size() == batch) flush();
    }
    void drain() {
        flush();
        for (auto& item : resident) backend.writeback(item.second.slot, *item.second.host);
        resident.clear();
    }
};
struct HostBackend {
    std::vector<Action> slots;
    explicit HostBackend(size_t capacity): slots(capacity, Action(1, 1)) {}
    void load(size_t slot, Action& x) { slots.at(slot) = x; }
    void refresh(size_t slot, Action& x) { slots.at(slot).child = x.child; slots.at(slot).reach = x.reach; }
    void launch(const std::vector<size_t>& ids, const std::vector<Action*>& out, Discount d) {
        for (size_t k = 0; k < ids.size(); ++k) {
            auto& x = slots.at(ids[k]); action_scalar(x, d);
            out[k]->strategy = x.strategy; out[k]->next_reach = x.next_reach; out[k]->cf = x.cf;
        }
    }
    void writeback(size_t slot, Action& x) { x.regret = slots.at(slot).regret; x.average = slots.at(slot).average; }
};
void test_queue() {
    for (size_t capacity : {size_t(1), size_t(2), size_t(5)}) for (size_t batch : {size_t(1), capacity}) {
        std::vector<Action> actual, expected;
        for (int i = 0; i < 7; ++i) actual.push_back(make_action(17, 3, i * 97 + 13));
        expected = actual; HostBackend backend(capacity); auto d = Discount::dcfr(7);
        ResidentQueue<HostBackend> queue(backend, capacity, batch, d);
        for (int id : {0, 1, 0, 0, 2, 3, 1, 4, 5, 6, 0, 6, 6}) {
            actual[id].child[0] += .75f; actual[id].reach[0] += .01f;
            expected[id].child[0] += .75f; expected[id].reach[0] += .01f;
            queue.submit(actual[id]); queue.flush(); action_scalar(expected[id], d);
            assert_vec(actual[id].cf, expected[id].cf, "queue dependency flush");
        }
        queue.drain(); for (size_t i = 0; i < actual.size(); ++i) check_action(actual[i], expected[i]);
        for (int i : {0, 1, 2, 3, 0, 0, 4, 6, 5, 6}) { queue.submit(actual[i]); action_scalar(expected[i], d); }
        queue.drain(); for (size_t i = 0; i < actual.size(); ++i) check_action(actual[i], expected[i]);
        require(queue.stats.misses > capacity && queue.stats.evictions > 0 && queue.stats.peak_pending <= batch, "queue pressure coverage");
    }
    HostBackend backend(2); ResidentQueue<HostBackend> queue(backend, 2, 2, {});
    auto first = make_action(7, 2, 11), collision = make_action(7, 2, 11); queue.submit(first);
    bool rejected = false; try { queue.submit(collision); } catch (const std::exception&) { rejected = true; }
    require(rejected, "duplicate node ID must fail"); queue.drain();
}
struct Stats { double median, p10, p90, minimum; int repeats; };
Stats measure(const std::function<void()>& fn, const std::function<double()>& consume, int samples, double target_ms = 12) {
    fn(); sink = consume();
    int repeats = 1;
    for (;;) {
        auto start = Clock::now(); for (int i = 0; i < repeats; ++i) fn();
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count(); sink = consume();
        if (ms >= target_ms || repeats >= 65536) break;
        repeats *= 2;
    }
    std::vector<double> times;
    for (int s = 0; s < samples; ++s) {
        auto start = Clock::now(); for (int i = 0; i < repeats; ++i) fn();
        double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / repeats;
        sink = consume(); require(std::isfinite(sink), "benchmark checksum is nonfinite"); times.push_back(us);
    }
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], times[(times.size() - 1) / 10], times[(times.size() - 1) * 9 / 10], times.front(), repeats};
}
void report(const std::string& backend, const std::string& work, int n, int a, int nodes, int batch, int threads, Stats s, double bytes, double check) {
    std::cout << backend << ',' << work << ',' << n << ',' << a << ',' << nodes << ',' << batch << ',' << threads << ','
        << s.median << ',' << s.p10 << ',' << s.p90 << ',' << s.minimum << ',' << nodes * 1e6 / s.median << ','
        << bytes / (s.median * 1000) << ',' << s.repeats << ',' << check << '\n';
}
void csv_header() {
    std::cout << "backend,work,combos,actions,nodes,batch,threads,median_us,p10_us,p90_us,min_us,nodes_per_s,logical_GB_per_s,repeats,checksum\n";
}
struct Options {
    int samples = 9;
    bool quick = false, tests_only = false, focused = false;
    Options(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--quick") quick = true;
            else if (a == "--focused") focused = true;
            else if (a == "--test-only") tests_only = true;
            else if (a == "--samples" && i + 1 < argc) { samples = std::stoi(argv[++i]); require(samples >= 3, "at least 3 samples"); }
            else throw std::runtime_error("options: --quick --focused --test-only --samples N");
        }
    }
};
void test_action(const std::function<void(Action&, Discount)>& fn) {
    for (int n : {1, 7, 15, 16, 17, 31, 33, 1326}) for (int a : {1, 2, 3, 5}) {
        auto original = make_action(n, a, n * 17 + a);
        for (bool dcfr : {false, true}) {
            auto got = original, expected = original;
            for (int iteration = 1; iteration <= 9; ++iteration) {
                auto d = dcfr ? Discount::dcfr(iteration) : Discount{};
                fn(got, d); action_oracle(expected, d); check_action(got, expected);
            }
        }
    }
    Action hand(1, 2); hand.regret = {2, -1}; hand.child = {10, -6}; hand.reach = {.25f};
    fn(hand, {}); assert_vec(hand.strategy, {1, 0}, "hand strategy"); assert_vec(hand.regret, {2, -17}, "hand regrets");
    assert_vec(hand.next_reach, {.25f, 0}, "hand reach"); assert_vec(hand.cf, {10}, "hand CF");
    Action zero(1, 2); zero.regret = {-2, 0}; zero.child = {4, 8}; zero.reach = {1}; fn(zero, {});
    assert_vec(zero.strategy, {.5f, .5f}, "uniform"); assert_vec(zero.cf, {6}, "uniform CF");
    auto reach_a = make_action(17, 3, 56), reach_b = reach_a;
    std::fill(reach_b.reach.begin(), reach_b.reach.end(), 0.f);
    fn(reach_a, {}); fn(reach_b, {}); assert_vec(reach_a.cf, reach_b.cf, "hero reach excluded from CF");
    assert_vec(reach_a.regret, reach_b.regret, "regret excludes hero reach");
    Action sign(1, 2); sign.regret = {1, -1}; sign.child = {0, 10}; sign.reach = {1};
    fn(sign, Discount::dcfr(1)); assert_vec(sign.regret, {.5f, 4.5f}, "discount follows updated sign");
}
void test_terminal(const std::function<void(Terminal&)>& fn) {
    for (int n : {1, 7, 17, 33, 169, 1326}) for (int trav : {0, 1}) for (int active : {0, 1}) for (bool fold : {false, true}) {
        auto got = make_terminal(n, n + trav); got.traverser = trav; got.active = active; got.fold = fold;
        auto expected = got; fn(got); terminal_oracle(expected); assert_vec(got.cf, expected.cf, "terminal oracle");
    }
    Terminal hand; hand.board = 0; hand.pot = 21;
    hand.hands = {{card_mask(0)|card_mask(1),0,1,3}, {card_mask(2)|card_mask(3),2,3,1}, {card_mask(4)|card_mask(5),4,5,3}};
    hand.p1 = {1, 2, 3}; hand.p2 = {.2f, .3f, .5f}; hand.cf.resize(3);
    fn(hand); assert_vec(hand.cf, {3, -7, 3}, "ties blockers integer pot");
    hand.fold = true; hand.traverser = 1; hand.active = 0; fn(hand); assert_vec(hand.cf, {-150, -120, -90}, "P2 fold indexing");
    hand.board = hand.hands[0].mask; fn(hand); assert_vec(hand.cf, {0, -90, -60}, "board blockers");
    auto all = make_terminal(33, 4); all.fold = false; std::fill(all.p1.begin(), all.p1.end(), 0); std::fill(all.p2.begin(), all.p2.end(), 0);
    fn(all); for (float v : all.cf) close(v, 0, "zero reach");
}
void test_cache() {
    TerminalCache cache; auto x = make_terminal(33, 11);
    auto verify = [&]() { auto expected = x; cache.apply(x); terminal_oracle(expected); assert_vec(x.cf, expected.cf, "cache"); };
    verify(); require(cache.misses == 1, "cold cache"); verify(); require(cache.hits == 1, "warm cache");
    x.p2[2] += .3f; x.pot += 4; verify(); require(cache.misses == 1, "reach and pot must not invalidate");
    x.traverser = 1; x.active = 1; verify(); require(cache.misses == 1, "perspective must not invalidate");
    x.board ^= card_mask(6); verify(); require(cache.misses == 2, "board invalidation");
    x.hands[0].rank += 2; verify(); require(cache.misses == 3, "rank invalidation");
    std::swap(x.hands[0], x.hands[1]); verify(); require(cache.misses == 4, "combo order invalidation");
    x.fold = true; verify(); require(cache.misses == 5, "terminal type invalidation");
}
int baseline_main(int argc, char** argv) {
    Options opt(argc, argv); test_action(action_scalar); test_terminal(terminal_scalar); test_terminal(terminal_prefix); test_terminal(terminal_sweep); TerminalOrder order; test_terminal([&](Terminal& x) { order.apply(x); }); test_cache(); test_queue();
    std::cerr << "PASS scalar/double oracle, action updates, DCFR, terminal perspectives, folds, ties, blockers, cache invalidation, bounded queue eviction/duplicates/writeback\n";
    if (opt.tests_only) return 0;
    std::cerr << "Math: r'=(r+child-CF)*sign_discount; sum'=(sum+strategy)*average_discount; source uses unweighted strategy accumulation; no traversal or evaluator included\n";
    std::cerr << "Storage=float32; logical bytes are estimates, not hardware counters; timings include cache-key validation and queueing where used\n";
    csv_header();
    for (int n : opt.quick ? std::vector<int>{169, 1326} : std::vector<int>{33, 169, 663, 1326}) {
        int nodes = opt.quick ? 32 : 256;
        for (int a : {2, 3, 5}) {
            std::vector<Action> data; for (int i = 0; i < nodes; ++i) data.push_back(make_action(n, a, i * 97 + 13));
            auto run = [&]() { for (auto& x : data) action_scalar(x, Discount::dcfr(100)); };
            auto consume = [&]() { double s = 0; for (const auto& x : data) s += checksum(x.regret) + checksum(x.cf); return s; };
            auto stats = measure(run, consume, opt.samples);
            report("scalar", "action_dcfr", n, a, nodes, nodes, 1, stats, double(nodes) * n * (a * 10 + 2) * sizeof(float), consume());
        }
        auto original = make_terminal(n, 9);
        for (std::string variant : {"direct", "cached_warm", "cached_cold", "rank_prefix", "rank_sweep_cold", "rank_sweep_warm"}) {
            auto x = original; TerminalCache cache; TerminalOrder order;
            auto run = [&]() {
                if (variant == "direct") terminal_scalar(x);
                else if (variant == "rank_prefix") terminal_prefix(x);
                else if (variant == "rank_sweep_cold") terminal_sweep(x);
                else if (variant == "rank_sweep_warm") order.apply(x);
                else { if (variant == "cached_cold") cache.board = ~x.board; cache.apply(x); }
            };
            auto consume = [&]() { return checksum(x.cf); };
            auto stats = measure(run, consume, opt.samples);
            report("scalar", "terminal_" + variant, n, 0, 1, 1, 1, stats,
                variant.find("rank_") == 0 ? 0 : double(n) * n * (variant == "direct" ? 0 : 5), consume());
        }
    }
    return 0;
}
}
#ifndef EXPERIMENT_LIBRARY
int main(int argc, char** argv) {
    try { return experiment::baseline_main(argc, argv); }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
#endif
