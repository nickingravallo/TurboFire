#define EXPERIMENT_LIBRARY
#include "baseline.cpp"
#include <immintrin.h>
#include <omp.h>

namespace experiment {
template<int KnownActions>
__attribute__((target("avx2"), noinline))
void action_avx2_impl(Action& x, Discount d = {}) {
    const int actions = KnownActions == 0 ? x.actions : KnownActions;
    int c = 0;
    auto zero = _mm256_setzero_ps();
    auto uniform = _mm256_set1_ps(1.f / actions);
    auto positive = _mm256_set1_ps(d.positive), negative = _mm256_set1_ps(d.negative), average = _mm256_set1_ps(d.average);
    for (; c + 8 <= x.n; c += 8) {
        auto sum = zero;
        for (int a = 0; a < actions; ++a) sum = _mm256_add_ps(sum, _mm256_max_ps(zero, _mm256_loadu_ps(x.regret.data() + size_t(a) * x.n + c)));
        auto valid = _mm256_cmp_ps(sum, zero, _CMP_GT_OQ);
        auto denom = _mm256_blendv_ps(_mm256_set1_ps(1.f), sum, valid);
        auto reach = _mm256_loadu_ps(x.reach.data() + c);
        auto value = zero;
        for (int a = 0; a < actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            auto r = _mm256_max_ps(zero, _mm256_loadu_ps(x.regret.data() + j));
            auto ratio = _mm256_div_ps(r, denom);
            auto prob = _mm256_blendv_ps(uniform, ratio, valid);
            _mm256_storeu_ps(x.strategy.data() + j, prob);
            _mm256_storeu_ps(x.next_reach.data() + j, _mm256_mul_ps(reach, prob));
            value = _mm256_add_ps(value, _mm256_mul_ps(prob, _mm256_loadu_ps(x.child.data() + j)));
        }
        _mm256_storeu_ps(x.cf.data() + c, value);
        for (int a = 0; a < actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            auto r = _mm256_add_ps(_mm256_loadu_ps(x.regret.data() + j), _mm256_sub_ps(_mm256_loadu_ps(x.child.data() + j), value));
            auto is_positive = _mm256_cmp_ps(r, zero, _CMP_GT_OQ);
            auto factor = _mm256_blendv_ps(negative, positive, is_positive);
            _mm256_storeu_ps(x.regret.data() + j, _mm256_mul_ps(r, factor));
            auto accum = _mm256_add_ps(_mm256_loadu_ps(x.average.data() + j), _mm256_loadu_ps(x.strategy.data() + j));
            _mm256_storeu_ps(x.average.data() + j, _mm256_mul_ps(accum, average));
        }
    }
    for (; c < x.n; ++c) {
        float sum = 0;
        for (int a = 0; a < actions; ++a) sum += std::max(0.f, x.regret[size_t(a) * x.n + c]);
        float value = 0;
        for (int a = 0; a < actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            float prob = sum > 0 ? std::max(0.f, x.regret[j]) / sum : 1.f / actions;
            x.strategy[j] = prob; x.next_reach[j] = x.reach[c] * prob; value += prob * x.child[j];
        }
        x.cf[c] = value;
        for (int a = 0; a < actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            float r = x.regret[j] + (x.child[j] - value);
            x.regret[j] = r * (r > 0 ? d.positive : d.negative);
            x.average[j] = (x.average[j] + x.strategy[j]) * d.average;
        }
    }
}
void action_avx2(Action& x, Discount d) { action_avx2_impl<0>(x, d); }
void action_avx2_specialized(Action& x, Discount d) {
    switch (x.actions) {
        case 2: action_avx2_impl<2>(x, d); break;
        case 3: action_avx2_impl<3>(x, d); break;
        case 5: action_avx2_impl<5>(x, d); break;
        default: action_avx2_impl<0>(x, d); break;
    }
}
__attribute__((target("avx2"), noinline))
void terminal_avx2(Terminal& x, TerminalCache& cache) {
    cache.ensure(x);
    size_t n = x.hands.size(); const auto& reach = x.villain();
    float payoff = x.fold ? x.fold_payoff() : x.pot / 2;
    auto pay = _mm256_set1_ps(payoff);
    for (size_t h = 0; h < n; ++h) {
        auto sum = _mm256_setzero_ps(); size_t v = 0;
        for (; v + 8 <= n; v += 8) {
            auto signs = _mm256_cvtepi32_ps(_mm256_cvtepi8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(cache.signs.data() + h * n + v))));
            sum = _mm256_add_ps(sum, _mm256_mul_ps(_mm256_loadu_ps(reach.data() + v), _mm256_mul_ps(signs, pay)));
        }
        alignas(64) float temp[8]; _mm256_storeu_ps(temp, sum);
        float value = 0; for (float item : temp) value += item;
        for (; v < n; ++v) value += reach[v] * (cache.signs[h * n + v] * payoff);
        x.cf[h] = value;
    }
}
template<int KnownActions>
__attribute__((target("avx512f,avx512bw"), noinline))
void action_avx512_impl(Action& x, Discount d = {}) {
    const int actions = KnownActions == 0 ? x.actions : KnownActions;
    int c = 0;
    auto zero = _mm512_setzero_ps();
    auto uniform = _mm512_set1_ps(1.f / actions);
    auto positive = _mm512_set1_ps(d.positive), negative = _mm512_set1_ps(d.negative), average = _mm512_set1_ps(d.average);
    for (; c + 16 <= x.n; c += 16) {
        auto sum = zero;
        for (int a = 0; a < actions; ++a) sum = _mm512_add_ps(sum, _mm512_max_ps(zero, _mm512_loadu_ps(x.regret.data() + size_t(a) * x.n + c)));
        auto valid = _mm512_cmp_ps_mask(sum, zero, _CMP_GT_OQ);
        auto denom = _mm512_mask_blend_ps(valid, _mm512_set1_ps(1.f), sum);
        auto reach = _mm512_loadu_ps(x.reach.data() + c);
        auto value = zero;
        for (int a = 0; a < actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            auto r = _mm512_max_ps(zero, _mm512_loadu_ps(x.regret.data() + j));
            auto ratio = _mm512_div_ps(r, denom);
            auto prob = _mm512_mask_blend_ps(valid, uniform, ratio);
            _mm512_storeu_ps(x.strategy.data() + j, prob);
            _mm512_storeu_ps(x.next_reach.data() + j, _mm512_mul_ps(reach, prob));
            value = _mm512_add_ps(value, _mm512_mul_ps(prob, _mm512_loadu_ps(x.child.data() + j)));
        }
        _mm512_storeu_ps(x.cf.data() + c, value);
        for (int a = 0; a < actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            auto r = _mm512_add_ps(_mm512_loadu_ps(x.regret.data() + j), _mm512_sub_ps(_mm512_loadu_ps(x.child.data() + j), value));
            auto is_positive = _mm512_cmp_ps_mask(r, zero, _CMP_GT_OQ);
            auto factor = _mm512_mask_blend_ps(is_positive, negative, positive);
            _mm512_storeu_ps(x.regret.data() + j, _mm512_mul_ps(r, factor));
            auto accum = _mm512_add_ps(_mm512_loadu_ps(x.average.data() + j), _mm512_loadu_ps(x.strategy.data() + j));
            _mm512_storeu_ps(x.average.data() + j, _mm512_mul_ps(accum, average));
        }
    }
    for (; c < x.n; ++c) {
        float sum = 0;
        for (int a = 0; a < actions; ++a) sum += std::max(0.f, x.regret[size_t(a) * x.n + c]);
        float value = 0;
        for (int a = 0; a < actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            float prob = sum > 0 ? std::max(0.f, x.regret[j]) / sum : 1.f / actions;
            x.strategy[j] = prob; x.next_reach[j] = x.reach[c] * prob; value += prob * x.child[j];
        }
        x.cf[c] = value;
        for (int a = 0; a < actions; ++a) {
            size_t j = size_t(a) * x.n + c;
            float r = x.regret[j] + (x.child[j] - value);
            x.regret[j] = r * (r > 0 ? d.positive : d.negative);
            x.average[j] = (x.average[j] + x.strategy[j]) * d.average;
        }
    }
}
void action_avx512(Action& x, Discount d) { action_avx512_impl<0>(x, d); }
void action_avx512_specialized(Action& x, Discount d) {
    switch (x.actions) {
        case 2: action_avx512_impl<2>(x, d); break;
        case 3: action_avx512_impl<3>(x, d); break;
        case 5: action_avx512_impl<5>(x, d); break;
        default: action_avx512_impl<0>(x, d); break;
    }
}
__attribute__((target("avx512f,avx512bw"), noinline))
void terminal_avx512(Terminal& x, TerminalCache& cache) {
    cache.ensure(x);
    size_t n = x.hands.size(); const auto& reach = x.villain();
    float payoff = x.fold ? x.fold_payoff() : x.pot / 2;
    auto pay = _mm512_set1_ps(payoff);
    for (size_t h = 0; h < n; ++h) {
        auto sum = _mm512_setzero_ps(); size_t v = 0;
        for (; v + 16 <= n; v += 16) {
            auto signs = _mm512_cvtepi32_ps(_mm512_cvtepi8_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(cache.signs.data() + h * n + v))));
            sum = _mm512_add_ps(sum, _mm512_mul_ps(_mm512_loadu_ps(reach.data() + v), _mm512_mul_ps(signs, pay)));
        }
        alignas(64) float temp[16]; _mm512_storeu_ps(temp, sum);
        float value = 0; for (float item : temp) value += item;
        for (; v < n; ++v) value += reach[v] * (cache.signs[h * n + v] * payoff);
        x.cf[h] = value;
    }
}
using ActionFn = void (*)(Action&, Discount);
struct CpuVariant { std::string name; ActionFn fn; };
int cpu_main(int argc, char** argv) {
    Options opt(argc, argv);
    __builtin_cpu_init();
    bool avx2 = __builtin_cpu_supports("avx2");
    bool avx512 = __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw");
    std::cerr << "CPU avx2=" << avx2 << " avx512f+bw=" << avx512 << " max_threads=" << omp_get_max_threads() << '\n';
    std::vector<CpuVariant> variants{{"scalar", action_scalar}};
    if (avx2) { variants.push_back({"avx2", action_avx2}); variants.push_back({"avx2_specialized", action_avx2_specialized}); }
    if (avx512) { variants.push_back({"avx512", action_avx512}); variants.push_back({"avx512_specialized", action_avx512_specialized}); }
    else std::cerr << "SKIP AVX-512 execution: CPU/OS support absent; functions still compiled\n";
    for (auto& variant : variants) { test_action(variant.fn); std::cerr << "PASS " << variant.name << " action\n"; }
    if (avx2) { TerminalCache c; test_terminal([&](Terminal& x) { terminal_avx2(x, c); }); std::cerr << "PASS avx2 terminal\n"; }
    if (avx512) { TerminalCache c; test_terminal([&](Terminal& x) { terminal_avx512(x, c); }); std::cerr << "PASS avx512 terminal\n"; }
    if (opt.tests_only) return 0;
    omp_set_dynamic(0);
    std::cerr << "Storage=float32; action timings are steady-state repeated DCFR updates; logical bandwidth is an estimate\n";
    csv_header();
    int nodes = opt.quick ? 32 : 256;
    auto counts = opt.focused ? std::vector<int>{1326} : opt.quick ? std::vector<int>{169, 1326} : std::vector<int>{33, 169, 663, 1326};
    for (int n : counts) for (int a : opt.focused ? std::vector<int>{3} : std::vector<int>{2, 3, 5}) {
        std::vector<Action> originals;
        for (int i = 0; i < nodes; ++i) originals.push_back(make_action(n, a, i * 97 + 13));
        std::vector<int> order(nodes); std::iota(order.begin(), order.end(), 0); std::mt19937 rng(778); std::shuffle(order.begin(), order.end(), rng);
        for (auto& variant : variants) for (int threads : opt.focused ? std::vector<int>{1, 4} : std::vector<int>{1, 2, 4}) {
            if (threads > omp_get_num_procs()) continue;
            for (int batch : opt.focused ? std::vector<int>{1, 16} : opt.quick ? std::vector<int>{1, 16, nodes} : std::vector<int>{1, 4, 16, 64, nodes}) {
                if (batch > nodes) continue;
                for (bool ids : {false, true}) {
                    auto data = originals;
                    std::unordered_map<uint64_t, size_t> lookup;
                    for (size_t i = 0; i < data.size(); ++i) lookup.emplace(data[i].id, i);
                    auto run = [&]() {
                        int chunks = (nodes + batch - 1) / batch;
                        #pragma omp parallel for num_threads(threads) schedule(static) if(threads > 1)
                        for (int chunk = 0; chunk < chunks; ++chunk) {
                            int end = std::min(nodes, (chunk + 1) * batch);
                            for (int k = chunk * batch; k < end; ++k) {
                                size_t index = ids ? lookup.at(originals[order[k]].id) : size_t(order[k]);
                                variant.fn(data[index], Discount::dcfr(100));
                            }
                        }
                    };
                    auto consume = [&]() { double s = 0; for (auto& x : data) s += checksum(x.regret) + checksum(x.cf); return s; };
                    run();
                    for (int i = 0; i < nodes; ++i) { auto expected = originals[i]; action_scalar(expected, Discount::dcfr(100)); check_action(data[i], expected); }
                    auto stats = measure(run, consume, opt.samples);
                    report(variant.name, ids ? "action_dcfr_ids" : "action_dcfr_direct", n, a, nodes, batch, threads, stats,
                        double(nodes) * n * (a * 10 + 2) * sizeof(float), consume());
                }
            }
        }
    }
    for (int n : counts) {
        int count = opt.quick ? 8 : 32;
        std::vector<Terminal> data; for (int i = 0; i < count; ++i) data.push_back(make_terminal(n, i));
        std::vector<TerminalCache> caches(count);
        std::vector<TerminalOrder> orders(count);
        for (auto& variant : variants) for (int threads : {1, 2, 4}) for (int terminal_mode : {0, 1, 2}) {
            if (variant.name.find("specialized") != std::string::npos) continue;
            auto run = [&]() {
                #pragma omp parallel for num_threads(threads) schedule(static) if(threads > 1)
                for (int i = 0; i < count; ++i) {
                    if (terminal_mode == 1) terminal_prefix(data[i]);
                    else if (terminal_mode == 2) orders[i].apply(data[i]);
                    else if (variant.name == "avx2") terminal_avx2(data[i], caches[i]);
                    else if (variant.name == "avx512") terminal_avx512(data[i], caches[i]);
                    else caches[i].apply(data[i]);
                }
            };
            run();
            for (int i = 0; i < count; ++i) { auto expected = data[i]; terminal_oracle(expected); assert_vec(data[i].cf, expected.cf, "parallel terminal"); }
            auto consume = [&]() { double s = 0; for (auto& x : data) s += checksum(x.cf); return s; };
            auto stats = measure(run, consume, opt.samples);
            report(variant.name, terminal_mode == 2 ? "terminal_rank_sweep_warm" : terminal_mode == 1 ? "terminal_rank_prefix" : "terminal_cached_warm", n, 0, count, count, threads,
                stats, terminal_mode != 0 ? 0 : double(count) * n * n * 5, consume());
        }
    }
    return 0;
}
}
int main(int argc, char** argv) {
    try { return experiment::cpu_main(argc, argv); }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
