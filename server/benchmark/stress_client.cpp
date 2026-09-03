#include "../common/types.hpp"
#include <boost/asio.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <glaze/glaze.hpp>

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <print>
#include <format>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

struct alignas(64) WorkerMetrics {
    uint64_t total_requests{0};
    uint64_t success_requests{0};
    uint64_t failed_requests{0};
    uint64_t total_bytes{0};
    std::vector<uint32_t> latency_samples; // 毫秒/微秒样本
};

class StressBenchmarkRunner {
public:
    StressBenchmarkRunner(
        std::string host,
        unsigned short port,
        int concurrency,
        int duration_seconds,
        int server_pid
    ) : host_(std::move(host)),
        port_(port),
        concurrency_(concurrency),
        duration_seconds_(duration_seconds),
        server_pid_(server_pid),
        worker_metrics_(concurrency) {}

    void run() {
#ifdef _WIN32
        DWORD_PTR client_affinity = 0xFF0; // CPU 4 ~ 11 (8个独立核心)
        if (SetProcessAffinityMask(GetCurrentProcess(), client_affinity)) {
            std::println("  [Client CPU Affinity] Pinned to CPU cores 4~11 (Mask: 0x{:X})", client_affinity);
        }
#endif

        std::println("\n=======================================================");
        std::println("   电动汽车充电桩平台 —— 超大规模阶梯极限并发压测套件   ");
        std::println("=======================================================");
        std::println("目标服务器: http://{}:{}", host_, port_);
        std::println("并发长连接数: {} | 压测时长: {} 秒 | 目标 PID: {}", concurrency_, duration_seconds_, server_pid_);
        std::println("=======================================================\n");

        init_auth_tokens();

        is_running_ = true;
        auto start_time = std::chrono::steady_clock::now();

        // 启动后台资源与 QPS 监控线程
        std::thread monitor_thread(&StressBenchmarkRunner::monitor_loop, this, start_time);

        // 启动 8 个客户端工作线程
        int num_threads = 8;
        std::vector<std::thread> worker_threads;
        net::io_context ioc(num_threads);

        for (int i = 0; i < concurrency_; ++i) {
            worker_metrics_[i].latency_samples.reserve(20000);
            net::co_spawn(ioc, worker_coroutine(ioc, i), net::detached);
        }

        for (int t = 0; t < num_threads; ++t) {
            worker_threads.emplace_back([&ioc]() { ioc.run(); });
        }

        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds_));
        is_running_ = false;

        ioc.stop();
        for (auto& th : worker_threads) {
            if (th.joinable()) th.join();
        }
        if (monitor_thread.joinable()) monitor_thread.join();

        print_final_report(start_time);
    }

private:
    void init_auth_tokens() {
        int64_t exp = ev::current_time_ms() + 86400000LL;
        for (int i = 1; i <= 20; ++i) {
            user_tokens_.push_back(std::format("EV_TOKEN.{}.user.{}.SIG_BENCH", i, exp));
        }
        admin_token_ = std::format("EV_TOKEN.1.admin.{}.SIG_BENCH", exp);
    }

    net::awaitable<void> worker_coroutine(net::io_context& ioc, int worker_id) {
        std::mt19937 rng(1337 + worker_id);
        std::uniform_int_distribution<int> type_dist(1, 100);
        std::uniform_real_distribution<double> lat_dist(30.0000, 32.5000);
        std::uniform_real_distribution<double> lon_dist(120.0000, 122.5000);
        std::uniform_int_distribution<int> station_dist(1, 10000); // 10,000 座电站

        tcp::resolver resolver(ioc);
        auto& wm = worker_metrics_[worker_id];

        while (is_running_) {
            try {
                auto const results = co_await resolver.async_resolve(host_, std::to_string(port_), net::use_awaitable);
                beast::tcp_stream stream(ioc);
                stream.expires_after(std::chrono::seconds(10));
                co_await stream.async_connect(results, net::use_awaitable);

                beast::flat_buffer buffer;
                http::request<http::string_body> req;
                http::response<http::string_body> resp;

                while (is_running_) {
                    int r = type_dist(rng);
                    req = {};
                    req.version(11);
                    req.keep_alive(true);
                    req.set(http::field::host, host_);
                    req.set(http::field::user_agent, "EV-Stress-Client/2.0");

                    if (r <= 30) {
                        // 1. 空间 R-Tree 搜桩 (30%) - 跨 10,000 座电站快速检索
                        double lat = lat_dist(rng);
                        double lon = lon_dist(rng);
                        req.method(http::verb::get);
                        req.target(std::format("/api/v1/stations/nearby?latitude={:.4f}&longitude={:.4f}&radius_km=15&limit=5", lat, lon));
                    } else if (r <= 55) {
                        // 2. 电站详情与实时枪位状态 (25%) - 跨 100,000 根电桩状态池直读
                        int sid = station_dist(rng);
                        req.method(http::verb::get);
                        req.target(std::format("/api/v1/stations/{}", sid));
                    } else if (r <= 75) {
                        // 3. 用户资料与钱包余额 (20%)
                        req.method(http::verb::get);
                        req.target("/api/v1/wallet/balance");
                        req.set(http::field::authorization, "Bearer " + user_tokens_[worker_id % user_tokens_.size()]);
                    } else if (r <= 90) {
                        // 4. 充电业务状态与订单 (15%)
                        req.method(http::verb::get);
                        req.target("/api/v1/charging/active-order");
                        req.set(http::field::authorization, "Bearer " + user_tokens_[worker_id % user_tokens_.size()]);
                    } else {
                        // 5. 管理大盘看板与单站销售报表 (10%)
                        req.method(http::verb::get);
                        req.target("/api/v1/admin/dashboard/summary");
                        req.set(http::field::authorization, "Bearer " + admin_token_);
                    }

                    req.prepare_payload();

                    auto req_start = std::chrono::steady_clock::now();
                    stream.expires_after(std::chrono::seconds(5));
                    co_await http::async_write(stream, req, net::use_awaitable);

                    buffer.clear();
                    resp = {};
                    co_await http::async_read(stream, buffer, resp, net::use_awaitable);

                    auto req_end = std::chrono::steady_clock::now();
                    uint32_t lat_us = static_cast<uint32_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(req_end - req_start).count()
                    );

                    wm.total_requests++;
                    if (resp.result() == http::status::ok) {
                        wm.success_requests++;
                        wm.total_bytes += resp.body().size();
                    } else {
                        wm.failed_requests++;
                    }

                    // 无锁 Thread-Local 采样
                    if (wm.latency_samples.size() < 20000) {
                        wm.latency_samples.push_back(lat_us);
                    }

                    if (!resp.keep_alive()) {
                        break;
                    }
                }
            } catch (...) {
                // 连接异常
            }

            if (!is_running_) break;
            boost::asio::steady_timer sleep_timer(ioc);
            sleep_timer.expires_after(std::chrono::milliseconds(20));
            co_await sleep_timer.async_wait(net::use_awaitable);
        }
    }

    void monitor_loop(std::chrono::steady_clock::time_point start_time) {
        uint64_t last_requests = 0;

        std::println("{:>6} | {:>12} | {:>14} | {:>10} | {:>12} | {:>10}",
                     "Time", "Interval QPS", "Total Reqs", "Success %", "Server CPU %", "Server RAM");
        std::println("-----------------------------------------------------------------------------");

        while (is_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!is_running_) break;

            auto now = std::chrono::steady_clock::now();
            int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count());

            uint64_t current_reqs = 0;
            uint64_t current_succ = 0;
            for (const auto& wm : worker_metrics_) {
                current_reqs += wm.total_requests;
                current_succ += wm.success_requests;
            }

            uint64_t delta_reqs = current_reqs - last_requests;
            double interval_qps = static_cast<double>(delta_reqs) / 2.0;
            last_requests = current_reqs;

            double succ_rate = current_reqs > 0 ? (static_cast<double>(current_succ) / current_reqs * 100.0) : 100.0;

            auto [cpu_pct, ram_mb] = sample_server_resources();
            cpu_samples_.push_back(cpu_pct);
            ram_samples_.push_back(ram_mb);
            qps_samples_.push_back(interval_qps);

            std::println("{:>5}s | {:>12.1f} | {:>14} | {:>9.2f}% | {:>10.1f}% | {:>9.1f} MB",
                         elapsed, interval_qps, current_reqs, succ_rate, cpu_pct, ram_mb);
        }
    }

    std::pair<double, double> sample_server_resources() {
#ifdef _WIN32
        if (server_pid_ > 0) {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, server_pid_);
            if (hProcess) {
                PROCESS_MEMORY_COUNTERS pmc;
                double ram_mb = 0.0;
                if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                    ram_mb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
                }

                FILETIME ftCreation, ftExit, ftKernel, ftUser;
                double cpu_pct = 0.0;
                if (GetProcessTimes(hProcess, &ftCreation, &ftExit, &ftKernel, &ftUser)) {
                    ULARGE_INTEGER kTime, uTime;
                    kTime.LowPart = ftKernel.dwLowDateTime;
                    kTime.HighPart = ftKernel.dwHighDateTime;
                    uTime.LowPart = ftUser.dwLowDateTime;
                    uTime.HighPart = ftUser.dwHighDateTime;
                    uint64_t total_proc_time = kTime.QuadPart + uTime.QuadPart;

                    auto now_time = std::chrono::steady_clock::now();
                    if (last_proc_time_ > 0 && last_sample_time_.time_since_epoch().count() > 0) {
                        double wall_time_sec = std::chrono::duration_cast<std::chrono::duration<double>>(now_time - last_sample_time_).count();
                        double proc_time_sec = static_cast<double>(total_proc_time - last_proc_time_) / 10000000.0;
                        cpu_pct = (proc_time_sec / (wall_time_sec * 2.0)) * 100.0;
                        if (cpu_pct > 100.0) cpu_pct = 100.0;
                    }
                    last_proc_time_ = total_proc_time;
                    last_sample_time_ = now_time;
                }

                CloseHandle(hProcess);
                return {cpu_pct, ram_mb};
            }
        }
#endif
        return {0.0, 0.0};
    }

    void print_final_report(std::chrono::steady_clock::time_point start_time) {
        auto end_time = std::chrono::steady_clock::now();
        double total_duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time).count();

        uint64_t total_reqs = 0, total_succ = 0, total_fail = 0, total_bytes = 0;
        std::vector<uint32_t> all_lats;

        for (const auto& wm : worker_metrics_) {
            total_reqs += wm.total_requests;
            total_succ += wm.success_requests;
            total_fail += wm.failed_requests;
            total_bytes += wm.total_bytes;
            all_lats.insert(all_lats.end(), wm.latency_samples.begin(), wm.latency_samples.end());
        }

        double overall_qps = total_duration_sec > 0 ? (static_cast<double>(total_reqs) / total_duration_sec) : 0.0;
        double mb_received = static_cast<double>(total_bytes) / (1024.0 * 1024.0);

        std::sort(all_lats.begin(), all_lats.end());

        auto get_percentile = [](const std::vector<uint32_t>& sorted, double p) -> double {
            if (sorted.empty()) return 0.0;
            size_t idx = static_cast<size_t>(std::ceil(p * sorted.size())) - 1;
            idx = std::min(idx, sorted.size() - 1);
            return static_cast<double>(sorted[idx]) / 1000.0;
        };

        double avg_lat = all_lats.empty() ? 0.0 : (std::accumulate(all_lats.begin(), all_lats.end(), 0.0) / all_lats.size() / 1000.0);
        double max_ram = ram_samples_.empty() ? 0.0 : *std::max_element(ram_samples_.begin(), ram_samples_.end());
        double avg_cpu = cpu_samples_.empty() ? 0.0 : (std::accumulate(cpu_samples_.begin(), cpu_samples_.end(), 0.0) / cpu_samples_.size());

        std::println("\n=======================================================");
        std::println("   >>> 压测结果汇总 (并发长连接: {}) <<<", concurrency_);
        std::println("=======================================================");
        std::println("  - 测试总时长: {:.2f} 秒", total_duration_sec);
        std::println("  - 累计请求总数: {} 次", total_reqs);
        std::println("  - 成功请求数: {} 次 (成功率: {:.4f}%)", total_succ, (total_succ * 100.0 / std::max(uint64_t(1), total_reqs)));
        std::println("  - 平均吞吐量 (QPS): {:.2f} req/s", overall_qps);
        std::println("  - 延迟指标: p50={:.2f}ms | p95={:.2f}ms | p99={:.2f}ms | Avg={:.2f}ms",
                     get_percentile(all_lats, 0.50), get_percentile(all_lats, 0.95), get_percentile(all_lats, 0.99), avg_lat);
        std::println("  - 资源消耗: 2核CPU={:.1f}% | 峰值RAM={:.1f} MB (4GB利用率: {:.2f}%)",
                     avg_cpu, max_ram, (max_ram / 4096.0 * 100.0));
        std::println("=======================================================\n");
    }

    std::string host_;
    unsigned short port_;
    int concurrency_;
    int duration_seconds_;
    int server_pid_{0};

    std::atomic<bool> is_running_{false};
    std::vector<WorkerMetrics> worker_metrics_;

    std::vector<std::string> user_tokens_;
    std::string admin_token_;

    std::vector<double> cpu_samples_;
    std::vector<double> qps_samples_;
    std::vector<double> ram_samples_;

    uint64_t last_proc_time_{0};
    std::chrono::steady_clock::time_point last_sample_time_;
};

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    unsigned short port = 8080;
    int concurrency = 128;
    int duration_sec = 30; // 阶梯测试默认单轮 30~60 秒
    int server_pid = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--concurrency" && i + 1 < argc) concurrency = std::stoi(argv[++i]);
        else if (arg == "--duration" && i + 1 < argc) duration_sec = std::stoi(argv[++i]);
        else if (arg == "--server-pid" && i + 1 < argc) server_pid = std::stoi(argv[++i]);
    }

    StressBenchmarkRunner runner(host, port, concurrency, duration_sec, server_pid);
    runner.run();

    return 0;
}
