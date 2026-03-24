#include <benchmark/benchmark.h>
#include <string_view>
#include <memory>

#include "interface_A.h"
#include "generated_protocol_A.h"

// Implementation for the protocol
struct ImplA {
  std::string_view name() const { return "ImplA"; }
  int count() { return ++c; }
  int c = 0;
};

// Baseline using virtual functions
struct VBase {
  virtual std::string_view name() const = 0;
  virtual int count() = 0;
  virtual ~VBase() = default;
};

struct VImpl : VBase {
  std::string_view name() const override { return "VImpl"; }
  int count() override { return ++c; }
  int c = 0;
};

static void BM_Virtual_Name(benchmark::State& state) {
  std::unique_ptr<VBase> obj = std::make_unique<VImpl>();
  for (auto _ : state) {
    benchmark::DoNotOptimize(obj->name());
  }
}
BENCHMARK(BM_Virtual_Name);

static void BM_Protocol_Name(benchmark::State& state) {
  xyz::protocol_A<> obj(ImplA{});
  for (auto _ : state) {
    benchmark::DoNotOptimize(obj.name());
  }
}
BENCHMARK(BM_Protocol_Name);

static void BM_Virtual_Count(benchmark::State& state) {
  std::unique_ptr<VBase> obj = std::make_unique<VImpl>();
  for (auto _ : state) {
    benchmark::DoNotOptimize(obj->count());
  }
}
BENCHMARK(BM_Virtual_Count);

static void BM_Protocol_Count(benchmark::State& state) {
  xyz::protocol_A<> obj(ImplA{});
  for (auto _ : state) {
    benchmark::DoNotOptimize(obj.count());
  }
}
BENCHMARK(BM_Protocol_Count);

BENCHMARK_MAIN();
