// Standalone latency benchmark for the matching engine.
//
// Fires N synthetic bid/ask pairs at OrderBook::matchNewOrder on an
// in-memory book and reports fill latency percentiles. This is deliberately
// separate from the GoogleTest suite: it measures *how fast*, not
// *pass/fail*, so it doesn't belong in `ctest`.
//
// Usage: ./latency_bench [num_orders]   (default 10000)

#include "OrderBook.h"
#include "Database.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

int main(int argc, char* argv[])
{
    const int numOrders = (argc > 1) ? std::atoi(argv[1]) : 10000;

    Database db(":memory:");

    // Seed the book with one throwaway resting order so OrderBook's
    // constructor (which requires a non-empty CSV) has something to load;
    // the benchmark itself only exercises matchNewOrder on synthetic data.
    const char* seedPath = "/tmp/merkelrex_bench_seed.csv";
    {
        FILE* f = std::fopen(seedPath, "w");
        std::fprintf(f, "2020-01-01 00:00:00.000,BTC/USDT,ask,100000,0.0001\n");
        std::fclose(f);
    }
    OrderBook book(seedPath, db);

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> priceJitter(-5.0, 5.0);
    std::uniform_real_distribution<double> amountDist(0.01, 2.0);

    std::vector<double> latenciesUs;
    latenciesUs.reserve(numOrders);

    // Pre-seed resting liquidity on both sides so incoming orders have
    // something to cross against, similar to a live book under load.
    for (int i = 0; i < numOrders / 2; ++i)
    {
        OrderBookEntry restingAsk{10000.0 + priceJitter(rng), amountDist(rng),
                                   "2020-01-02 00:00:00.000", "BTC/USDT",
                                   OrderBookType::ask, "seed_ask_" + std::to_string(i)};
        book.insertOrder(restingAsk);
    }

    for (int i = 0; i < numOrders; ++i)
    {
        OrderBookEntry incoming{10000.0 + priceJitter(rng), amountDist(rng),
                                  "2020-01-03 00:00:00.000", "BTC/USDT",
                                  OrderBookType::bid, "bench_user_" + std::to_string(i)};

        auto start = std::chrono::high_resolution_clock::now();
        book.matchNewOrder(incoming);
        auto end = std::chrono::high_resolution_clock::now();

        latenciesUs.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }

    std::sort(latenciesUs.begin(), latenciesUs.end());
    auto percentile = [&](double p) {
        size_t idx = static_cast<size_t>(p * (latenciesUs.size() - 1));
        return latenciesUs[idx];
    };

    double total = 0;
    for (double v : latenciesUs) total += v;

    std::printf("orders matched: %d\n", numOrders);
    std::printf("mean:  %.2f us\n", total / latenciesUs.size());
    std::printf("p50:   %.2f us\n", percentile(0.50));
    std::printf("p95:   %.2f us\n", percentile(0.95));
    std::printf("p99:   %.2f us\n", percentile(0.99));
    std::printf("max:   %.2f us\n", latenciesUs.back());

    return 0;
}