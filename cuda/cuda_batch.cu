#define EXPERIMENT_LIBRARY
#include "baseline.cpp"
#include <cuda_runtime.h>
#include <memory>

namespace experiment {
void cuda_check(cudaError_t code, const char* operation) {
    if (code != cudaSuccess) throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(code));
}
struct DeviceFloats {
    float* ptr = nullptr;
    explicit DeviceFloats(size_t count) { cuda_check(cudaMalloc(reinterpret_cast<void**>(&ptr), count * sizeof(float)), "cudaMalloc floats"); }
    ~DeviceFloats() { if (ptr) cudaFree(ptr); }
    DeviceFloats(const DeviceFloats&) = delete;
};
struct PinnedFloats {
    float* ptr = nullptr;
    explicit PinnedFloats(size_t count) { cuda_check(cudaMallocHost(reinterpret_cast<void**>(&ptr), count * sizeof(float)), "cudaMallocHost"); }
    ~PinnedFloats() { if (ptr) cudaFreeHost(ptr); }
    PinnedFloats(const PinnedFloats&) = delete;
};
struct Events {
    cudaEvent_t start = nullptr, stop = nullptr;
    Events() { cuda_check(cudaEventCreate(&start), "event create"); cuda_check(cudaEventCreate(&stop), "event create"); }
    ~Events() { if (start) cudaEventDestroy(start); if (stop) cudaEventDestroy(stop); }
    template<class F> double time(F fn) {
        cuda_check(cudaEventRecord(start), "record start"); fn(); cuda_check(cudaEventRecord(stop), "record stop");
        cuda_check(cudaEventSynchronize(stop), "synchronize stop"); float ms = 0;
        cuda_check(cudaEventElapsedTime(&ms, start, stop), "elapsed time"); return ms * 1000.;
    }
};
struct GpuMetrics { double h2d_us = 0, kernel_us = 0, d2h_us = 0; size_t h2d_bytes = 0, d2h_bytes = 0, launches = 0; };
size_t action_stride(int n, int a) { return size_t(n) * (5 * a + 2); }
size_t dynamic_capacity(size_t bytes_per_slot, size_t requested) {
    size_t free = 0, total = 0; cuda_check(cudaMemGetInfo(&free, &total), "cudaMemGetInfo");
    size_t reserve = std::min(size_t(64) << 20, free / 4);
    size_t budget = std::min((free - reserve) / 4, size_t(512) << 20);
    size_t slots = std::min(requested, budget / bytes_per_slot);
    require(slots > 0, "insufficient free GPU memory for one slot");
    std::cerr << "GPU free_bytes=" << free << " total_bytes=" << total << " budget_bytes=" << budget
              << " slot_bytes=" << bytes_per_slot << " slots=" << slots << '\n';
    return slots;
}
__global__ void action_kernel(float* data, const int* slots, int count, int n, int actions, Discount d) {
    int c = blockIdx.x * blockDim.x + threadIdx.x, task = blockIdx.y;
    if (c >= n || task >= count) return;
    size_t na = size_t(n) * actions, stride = 5 * na + 2 * n;
    float* base = data + size_t(slots[task]) * stride;
    float* regret = base; float* average = base + na; float* child = base + 2 * na;
    float* strategy = base + 3 * na; float* next = base + 4 * na; float* reach = base + 5 * na; float* cf = reach + n;
    float sum = 0;
    for (int a = 0; a < actions; ++a) sum += fmaxf(0.f, regret[size_t(a) * n + c]);
    float value = 0;
    for (int a = 0; a < actions; ++a) {
        size_t j = size_t(a) * n + c; float p = sum > 0 ? fmaxf(0.f, regret[j]) / sum : 1.f / actions;
        strategy[j] = p; next[j] = reach[c] * p; value += p * child[j];
    }
    cf[c] = value;
    for (int a = 0; a < actions; ++a) {
        size_t j = size_t(a) * n + c; float r = regret[j] + (child[j] - value);
        regret[j] = r * (r > 0 ? d.positive : d.negative); average[j] = (average[j] + strategy[j]) * d.average;
    }
}
void pack_action(float* base, const Action& x) {
    size_t na = x.regret.size();
    std::copy(x.regret.begin(), x.regret.end(), base); std::copy(x.average.begin(), x.average.end(), base + na);
    std::copy(x.child.begin(), x.child.end(), base + 2 * na); std::copy(x.reach.begin(), x.reach.end(), base + 5 * na);
}
void unpack_action(const float* base, Action& x) {
    size_t na = x.regret.size();
    std::copy(base, base + na, x.regret.begin()); std::copy(base + na, base + 2 * na, x.average.begin());
    std::copy(base + 3 * na, base + 4 * na, x.strategy.begin()); std::copy(base + 4 * na, base + 5 * na, x.next_reach.begin());
    std::copy(base + 5 * na + x.n, base + 5 * na + 2 * x.n, x.cf.begin());
}
struct GpuBackend {
    int n, actions, block;
    size_t capacity, na, stride;
    DeviceFloats device;
    PinnedFloats stage;
    int* device_slots = nullptr;
    Events events;
    GpuMetrics metrics;
    GpuBackend(int count, int a, size_t requested, int threads): n(count), actions(a), block(threads),
        capacity(dynamic_capacity(action_stride(count, a) * sizeof(float) + sizeof(int), requested)),
        na(size_t(count) * a), stride(action_stride(count, a)), device(capacity * stride), stage(capacity * stride) {
        std::fill(stage.ptr, stage.ptr + capacity * stride, 0.f);
        cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_slots), capacity * sizeof(int)), "allocate slots");
    }
    ~GpuBackend() { if (device_slots) cudaFree(device_slots); }
    void upload(float* destination, const float* source, size_t count) {
        metrics.h2d_us += events.time([&] { cuda_check(cudaMemcpyAsync(destination, source, count * sizeof(float), cudaMemcpyHostToDevice), "upload"); });
        metrics.h2d_bytes += count * sizeof(float);
    }
    void download(float* destination, const float* source, size_t count) {
        metrics.d2h_us += events.time([&] { cuda_check(cudaMemcpyAsync(destination, source, count * sizeof(float), cudaMemcpyDeviceToHost), "download"); });
        metrics.d2h_bytes += count * sizeof(float);
    }
    void load(size_t slot, Action& x) {
        require(x.n == n && x.actions == actions, "GPU slot shape mismatch");
        float* s = stage.ptr + slot * stride; pack_action(s, x);
        upload(device.ptr + slot * stride, s, 3 * na); upload(device.ptr + slot * stride + 5 * na, s + 5 * na, n);
    }
    void refresh(size_t slot, Action& x) {
        require(x.n == n && x.actions == actions, "GPU refresh shape mismatch");
        float* s = stage.ptr + slot * stride;
        std::copy(x.child.begin(), x.child.end(), s + 2 * na); std::copy(x.reach.begin(), x.reach.end(), s + 5 * na);
        upload(device.ptr + slot * stride + 2 * na, s + 2 * na, na); upload(device.ptr + slot * stride + 5 * na, s + 5 * na, n);
    }
    void kernel(const std::vector<int>& ids, Discount d) {
        require(ids.size() <= capacity && ids.size() <= 65535, "GPU grid/capacity limit");
        metrics.h2d_us += events.time([&] { cuda_check(cudaMemcpyAsync(device_slots, ids.data(), ids.size() * sizeof(int), cudaMemcpyHostToDevice), "upload slots"); });
        metrics.h2d_bytes += ids.size() * sizeof(int);
        metrics.kernel_us += events.time([&] {
            action_kernel<<<dim3((n + block - 1) / block, unsigned(ids.size())), block>>>(device.ptr, device_slots, int(ids.size()), n, actions, d);
            cuda_check(cudaGetLastError(), "action launch");
        }); ++metrics.launches;
    }
    void launch(const std::vector<size_t>& slots, const std::vector<Action*>& outputs, Discount d) {
        std::vector<int> ids(slots.begin(), slots.end()); kernel(ids, d);
        for (size_t k = 0; k < slots.size(); ++k) {
            size_t slot = slots[k]; float* s = stage.ptr + slot * stride;
            download(s + 3 * na, device.ptr + slot * stride + 3 * na, 2 * na);
            download(s + 5 * na + n, device.ptr + slot * stride + 5 * na + n, n);
            auto& x = *outputs[k];
            std::copy(s + 3 * na, s + 4 * na, x.strategy.begin()); std::copy(s + 4 * na, s + 5 * na, x.next_reach.begin());
            std::copy(s + 5 * na + n, s + 5 * na + 2 * n, x.cf.begin());
        }
    }
    void writeback(size_t slot, Action& x) {
        float* s = stage.ptr + slot * stride; download(s, device.ptr + slot * stride, 2 * na);
        std::copy(s, s + na, x.regret.begin()); std::copy(s + na, s + 2 * na, x.average.begin());
    }
    void packed(std::vector<Action>& data, size_t batch, Discount d) {
        for (size_t begin = 0; begin < data.size(); begin += batch) {
            size_t count = std::min(batch, data.size() - begin); require(count <= capacity, "packed capacity");
            for (size_t i = 0; i < count; ++i) pack_action(stage.ptr + i * stride, data[begin + i]);
            upload(device.ptr, stage.ptr, count * stride);
            std::vector<int> ids(count); std::iota(ids.begin(), ids.end(), 0); kernel(ids, d);
            download(stage.ptr, device.ptr, count * stride);
            for (size_t i = 0; i < count; ++i) unpack_action(stage.ptr + i * stride, data[begin + i]);
        }
    }
};
struct DeviceHand { unsigned long long mask; int rank; };
__global__ void terminal_kernel(const DeviceHand* hands, const float* reaches, const unsigned long long* boards,
                                const float* pays, const int* folds, const signed char* cache, float* out, int n, int count, bool cached) {
    int hero = blockIdx.x * blockDim.x + threadIdx.x, task = blockIdx.y;
    if (hero >= n || task >= count) return;
    size_t base = size_t(task) * n; auto h = hands[base + hero]; float sum = 0;
    for (int villain = 0; villain < n; ++villain) {
        int sign = 0;
        if (cached) sign = cache[(base + hero) * n + villain];
        else {
            auto v = hands[base + villain];
            if (!(h.mask & v.mask) && !((h.mask | v.mask) & boards[task]))
                sign = folds[task] ? 1 : int(h.rank > v.rank) - int(h.rank < v.rank);
        }
        sum += reaches[base + villain] * (sign * pays[task]);
    }
    out[base + hero] = sum;
}
template<class T> struct DeviceArray {
    T* ptr = nullptr;
    explicit DeviceArray(size_t n) { cuda_check(cudaMalloc(reinterpret_cast<void**>(&ptr), n * sizeof(T)), "allocate device array"); }
    ~DeviceArray() { if (ptr) cudaFree(ptr); }
    DeviceArray(const DeviceArray&) = delete;
};
struct GpuTerminal {
    int n, count, block;
    DeviceArray<DeviceHand> hands;
    DeviceArray<float> reach, pays, out;
    DeviceArray<unsigned long long> boards;
    DeviceArray<int> folds;
    DeviceArray<signed char> signs;
    std::vector<TerminalCache> caches;
    Events events; GpuMetrics metrics;
    GpuTerminal(int combos, int nodes, int threads): n(combos),
        count(int(dynamic_capacity(size_t(combos) * combos + size_t(combos) * (sizeof(DeviceHand) + 2 * sizeof(float)) + 32, nodes))),
        block(threads), hands(size_t(n) * count), reach(size_t(n) * count), pays(count), out(size_t(n) * count),
        boards(count), folds(count), signs(size_t(n) * n * count), caches(count) {}
    template<class T> void upload(T* dst, const std::vector<T>& src) {
        metrics.h2d_us += events.time([&] { cuda_check(cudaMemcpyAsync(dst, src.data(), src.size() * sizeof(T), cudaMemcpyHostToDevice), "terminal upload"); });
        metrics.h2d_bytes += src.size() * sizeof(T);
    }
    void run_chunk(std::vector<Terminal*>& data, bool cached, bool force_cold) {
        require(int(data.size()) <= count, "terminal batch capacity"); int tasks = int(data.size());
        std::vector<DeviceHand> host_hands; std::vector<float> host_reach, host_pay;
        std::vector<unsigned long long> host_boards; std::vector<int> host_folds;
        for (int k = 0; k < tasks; ++k) {
            auto& x = *data[k]; require(int(x.hands.size()) == n, "terminal shape");
            for (auto h : x.hands) host_hands.push_back({h.mask, h.rank});
            host_reach.insert(host_reach.end(), x.villain().begin(), x.villain().end());
            host_pay.push_back(float(x.fold ? x.fold_payoff() : x.pot / 2)); host_boards.push_back(x.board); host_folds.push_back(x.fold);
            if (cached && (force_cold || !caches[k].matches(x))) {
                if (force_cold) caches[k].board = ~x.board;
                caches[k].ensure(x);
                metrics.h2d_us += events.time([&] {
                    cuda_check(cudaMemcpyAsync(signs.ptr + size_t(k) * n * n, caches[k].signs.data(), size_t(n) * n, cudaMemcpyHostToDevice), "upload signs");
                }); metrics.h2d_bytes += size_t(n) * n;
            }
        }
        if (!cached) { upload(hands.ptr, host_hands); upload(boards.ptr, host_boards); upload(folds.ptr, host_folds); }
        upload(reach.ptr, host_reach); upload(pays.ptr, host_pay);
        metrics.kernel_us += events.time([&] {
            terminal_kernel<<<dim3((n + block - 1) / block, tasks), block>>>(hands.ptr, reach.ptr, boards.ptr, pays.ptr, folds.ptr, signs.ptr, out.ptr, n, tasks, cached);
            cuda_check(cudaGetLastError(), "terminal launch");
        }); ++metrics.launches;
        std::vector<float> values(size_t(tasks) * n);
        metrics.d2h_us += events.time([&] { cuda_check(cudaMemcpyAsync(values.data(), out.ptr, values.size() * sizeof(float), cudaMemcpyDeviceToHost), "terminal output"); });
        metrics.d2h_bytes += values.size() * sizeof(float);
        for (int k = 0; k < tasks; ++k) std::copy(values.begin() + size_t(k) * n, values.begin() + size_t(k + 1) * n, data[k]->cf.begin());
    }
    void run(std::vector<Terminal>& data, bool cached, bool force_cold) {
        for (size_t begin = 0; begin < data.size(); begin += count) {
            std::vector<Terminal*> pending;
            for (size_t k = begin; k < std::min(data.size(), begin + count); ++k) pending.push_back(&data[k]);
            run_chunk(pending, cached, force_cold);
        }
    }
};
void test_gpu_actions() {
    for (int n : {1, 17, 1326}) for (int a : {2, 3, 5}) {
        for (size_t requested : {size_t(1), size_t(4)}) {
            GpuBackend backend(n, a, requested, 128);
            std::vector<Action> data; for (int i = 0; i < 7; ++i) data.push_back(make_action(n, a, 97 * i));
            auto expected = data; auto d = Discount::dcfr(7);
            ResidentQueue<GpuBackend> queue(backend, backend.capacity, backend.capacity, d);
            for (int k : {0, 1, 0, 0, 2, 3, 1, 4, 5, 6, 0, 6, 6}) {
                data[k].child[0] += .75f; expected[k].child[0] += .75f;
                queue.submit(data[k]); action_scalar(expected[k], d);
            }
            queue.drain(); for (size_t i = 0; i < data.size(); ++i) check_action(data[i], expected[i]);
            expected = data; for (auto& x : expected) action_scalar(x, {}); backend.packed(data, backend.capacity, {});
            for (size_t i = 0; i < data.size(); ++i) check_action(data[i], expected[i]);
        }
    }
}
void test_gpu_terminals() {
    for (int n : {1, 17, 169, 1326}) {
        GpuTerminal gpu(n, 2, 128);
        std::vector<Terminal> data; for (int i = 0; i < gpu.count * 2 + 1; ++i) data.push_back(make_terminal(n, i));
        for (int trav : {0, 1}) for (bool fold : {false, true}) for (bool cached : {false, true}) {
            for (auto& x : data) { x.traverser = trav; x.active = trav ^ 1; x.fold = fold; }
            gpu.run(data, cached, false);
            for (auto& x : data) { auto expected = x; terminal_oracle(expected); assert_vec(x.cf, expected.cf, "GPU terminal"); }
            for (auto& x : data) { x.board ^= card_mask(6); x.hands[0].rank += 3; x.p1[0] += .25f; x.p2[0] += .5f; x.pot += 4; }
            gpu.run(data, cached, false);
            for (auto& x : data) { auto expected = x; terminal_oracle(expected); assert_vec(x.cf, expected.cf, "GPU invalidation"); }
        }
    }
}
void print_metrics(const GpuMetrics& m, double invocations, const QueueStats& q = {}) {
    std::cerr << "profile per_call h2d_us=" << m.h2d_us / invocations << " kernel_us=" << m.kernel_us / invocations
              << " d2h_us=" << m.d2h_us / invocations << " h2d_bytes=" << m.h2d_bytes / invocations
              << " d2h_bytes=" << m.d2h_bytes / invocations << " launches=" << m.launches / invocations
              << " hits=" << q.hits / invocations << " misses=" << q.misses / invocations
              << " evictions=" << q.evictions / invocations << " flushes=" << q.flushes / invocations << '\n';
}
int gpu_main(int argc, char** argv) {
    Options opt(argc, argv); test_queue(); test_cache();
    int devices = 0; auto status = cudaGetDeviceCount(&devices);
    if (status != cudaSuccess || devices == 0) { std::cerr << "SKIP CUDA: " << cudaGetErrorString(status) << " devices=" << devices << '\n'; return 77; }
    cudaDeviceProp props{}; cuda_check(cudaGetDeviceProperties(&props, 0), "device properties");
    std::cerr << "GPU " << props.name << " SMs=" << props.multiProcessorCount << " memory=" << props.totalGlobalMem << '\n';
    test_gpu_actions(); test_gpu_terminals(); std::cerr << "PASS CUDA action, queue/cache, terminal, invalidation tests\n";
    if (opt.tests_only) return 0;
    std::cerr << "Timing includes instrumentation synchronization, staging and queue lookup; allocation excluded; resident final writeback reported separately; only independent ready nodes may batch\n";
    csv_header();
    for (int n : opt.quick ? std::vector<int>{169, 1326} : std::vector<int>{33, 169, 663, 1326}) for (int a : {2, 3, 5}) {
        int nodes = opt.quick ? 32 : 256;
        for (int block : {64, 128, 256}) for (size_t requested : {size_t(1), size_t(8), size_t(nodes)}) {
            GpuBackend gpu(n, a, requested, block);
            for (bool resident : {false, true}) {
                std::vector<Action> data; for (int i = 0; i < nodes; ++i) data.push_back(make_action(n, a, i * 97 + 13));
                auto expected = data; auto d = Discount::dcfr(100);
                ResidentQueue<GpuBackend> queue(gpu, gpu.capacity, gpu.capacity, d);
                double calls = 0;
                auto run = [&]() {
                    if (resident) { for (auto& x : data) queue.submit(x); queue.flush(); }
                    else gpu.packed(data, gpu.capacity, d);
                    ++calls;
                };
                auto consume = [&]() { double s = 0; for (auto& x : data) s += checksum(x.cf); return s; };
                run(); if (resident) queue.drain();
                for (int i = 0; i < nodes; ++i) { action_scalar(expected[i], d); check_action(data[i], expected[i]); }
                gpu.metrics = {}; queue.stats = {}; calls = 0;
                auto stats = measure(run, consume, opt.samples, 20);
                auto start = Clock::now(); if (resident) queue.drain();
                double drain_us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
                report("cuda_block" + std::to_string(block), resident ? "action_dcfr_resident_ids" : "action_dcfr_packed", n, a, nodes, int(gpu.capacity), 0,
                    stats, 0, consume());
                std::cerr << "profile action combos=" << n << " actions=" << a << " block=" << block << " slots=" << gpu.capacity << " resident=" << resident << " calls=" << calls << '\n';
                print_metrics(gpu.metrics, calls, queue.stats); std::cerr << "final_drain_us=" << drain_us << " persistent_state_not_uploaded_on_hits=" << resident << '\n';
            }
        }
    }
    for (int n : {169, 1326}) for (int block : {64, 128, 256}) {
        GpuTerminal gpu(n, opt.quick ? 8 : 32, block); std::vector<Terminal> data;
        for (int i = 0; i < gpu.count; ++i) data.push_back(make_terminal(n, i));
        for (std::string kind : {"direct", "cached_warm", "cached_cold"}) {
            bool cached = kind != "direct", cold = kind == "cached_cold"; double calls = 0; gpu.metrics = {};
            auto run = [&]() { gpu.run(data, cached, cold); ++calls; };
            auto consume = [&]() { double s = 0; for (auto& x : data) s += checksum(x.cf); return s; };
            auto stats = measure(run, consume, opt.samples, 20);
            report("cuda_block" + std::to_string(block), "terminal_" + kind, n, 0, gpu.count, gpu.count, 0, stats, 0, consume());
            std::cerr << "profile terminal kind=" << kind << " combos=" << n << " block=" << block << " calls=" << calls << '\n';
            print_metrics(gpu.metrics, calls);
        }
    }
    return 0;
}
}
int main(int argc, char** argv) {
    try { return experiment::gpu_main(argc, argv); }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
