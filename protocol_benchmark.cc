#include <benchmark/benchmark.h>

#include <string_view>
#include <utility>

#include "generated/protocol_A_manual.h"
#include "generated/protocol_A_virtual.h"
#include "interface_benchmark.h"

namespace {

struct ALike {
  std::string_view name() const { return "ALike"; }

  int count() { return 42; }
};

// Member function call benchmarks
static void Protocol_Virtual_Call(benchmark::State& state) {
  xyz::protocol<xyz::A_virtual> p(std::in_place_type<ALike>);
  for (auto _ : state) {
    benchmark::DoNotOptimize(p.name());
    benchmark::DoNotOptimize(p.count());
  }
}

BENCHMARK(Protocol_Virtual_Call);

static void Protocol_Manual_Call(benchmark::State& state) {
  xyz::protocol<xyz::A_manual> p(std::in_place_type<ALike>);
  for (auto _ : state) {
    benchmark::DoNotOptimize(p.name());
    benchmark::DoNotOptimize(p.count());
  }
}

BENCHMARK(Protocol_Manual_Call);

// Copy construction benchmarks
static void Protocol_Virtual_Copy(benchmark::State& state) {
  xyz::protocol<xyz::A_virtual> p(std::in_place_type<ALike>);
  for (auto _ : state) {
    xyz::protocol<xyz::A_virtual> copy(p);
    benchmark::DoNotOptimize(copy);
  }
}

BENCHMARK(Protocol_Virtual_Copy);

static void Protocol_Manual_Copy(benchmark::State& state) {
  xyz::protocol<xyz::A_manual> p(std::in_place_type<ALike>);
  for (auto _ : state) {
    xyz::protocol<xyz::A_manual> copy(p);
    benchmark::DoNotOptimize(copy);
  }
}

BENCHMARK(Protocol_Manual_Copy);

// Move construction/assignment benchmarks
static void Protocol_Virtual_Move(benchmark::State& state) {
  xyz::protocol<xyz::A_virtual> p(std::in_place_type<ALike>);
  for (auto _ : state) {
    xyz::protocol<xyz::A_virtual> moved(std::move(p));
    benchmark::DoNotOptimize(moved);
    p = std::move(moved);
    benchmark::DoNotOptimize(p);
  }
}

BENCHMARK(Protocol_Virtual_Move);

static void Protocol_Manual_Move(benchmark::State& state) {
  xyz::protocol<xyz::A_manual> p(std::in_place_type<ALike>);
  for (auto _ : state) {
    xyz::protocol<xyz::A_manual> moved(std::move(p));
    benchmark::DoNotOptimize(moved);
    p = std::move(moved);
    benchmark::DoNotOptimize(p);
  }
}

BENCHMARK(Protocol_Manual_Move);

// Swap benchmarks
static void Protocol_Virtual_Swap(benchmark::State& state) {
  xyz::protocol<xyz::A_virtual> p1(std::in_place_type<ALike>);
  xyz::protocol<xyz::A_virtual> p2(std::in_place_type<ALike>);
  for (auto _ : state) {
    p1.swap(p2);
    benchmark::DoNotOptimize(p1);
    benchmark::DoNotOptimize(p2);
  }
}

BENCHMARK(Protocol_Virtual_Swap);

static void Protocol_Manual_Swap(benchmark::State& state) {
  xyz::protocol<xyz::A_manual> p1(std::in_place_type<ALike>);
  xyz::protocol<xyz::A_manual> p2(std::in_place_type<ALike>);
  for (auto _ : state) {
    p1.swap(p2);
    benchmark::DoNotOptimize(p1);
    benchmark::DoNotOptimize(p2);
  }
}

BENCHMARK(Protocol_Manual_Swap);

// Construction and Destruction benchmarks
static void Protocol_Virtual_CtorDtor(benchmark::State& state) {
  for (auto _ : state) {
    xyz::protocol<xyz::A_virtual> p(std::in_place_type<ALike>);
    benchmark::DoNotOptimize(p);
  }
}

BENCHMARK(Protocol_Virtual_CtorDtor);

static void Protocol_Manual_CtorDtor(benchmark::State& state) {
  for (auto _ : state) {
    xyz::protocol<xyz::A_manual> p(std::in_place_type<ALike>);
    benchmark::DoNotOptimize(p);
  }
}

BENCHMARK(Protocol_Manual_CtorDtor);

// View benchmarks
static void ProtocolView_Virtual_Call(benchmark::State& state) {
  ALike alike;
  xyz::protocol_view<xyz::A_virtual> view(alike);
  for (auto _ : state) {
    benchmark::DoNotOptimize(view.name());
    benchmark::DoNotOptimize(view.count());
  }
}

BENCHMARK(ProtocolView_Virtual_Call);

static void ProtocolView_Manual_Call(benchmark::State& state) {
  ALike alike;
  xyz::protocol_view<xyz::A_manual> view(alike);
  for (auto _ : state) {
    benchmark::DoNotOptimize(view.name());
    benchmark::DoNotOptimize(view.count());
  }
}

BENCHMARK(ProtocolView_Manual_Call);

}  // namespace

BENCHMARK_MAIN();
