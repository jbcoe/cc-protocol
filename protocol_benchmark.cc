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

struct ALikeToo {
  std::string_view name() const { return "ALikeToo"; }

  int count() { return 99; }
};

// Member function call benchmarks
static void Direct_Call(benchmark::State& state) {
  ALike a;
  benchmark::DoNotOptimize(a);
  for (auto _ : state) {
    benchmark::DoNotOptimize(a.name());
    benchmark::DoNotOptimize(a.count());
  }
}

BENCHMARK(Direct_Call);

static void Protocol_Virtual_Call(benchmark::State& state) {
  xyz::protocol<xyz::A_virtual> p(std::in_place_type<ALike>);
  benchmark::DoNotOptimize(p);
  for (auto _ : state) {
    benchmark::DoNotOptimize(p.name());
    benchmark::DoNotOptimize(p.count());
  }
}

BENCHMARK(Protocol_Virtual_Call);

static void Protocol_Manual_Call(benchmark::State& state) {
  xyz::protocol<xyz::A_manual> p(std::in_place_type<ALike>);
  benchmark::DoNotOptimize(p);
  for (auto _ : state) {
    benchmark::DoNotOptimize(p.name());
    benchmark::DoNotOptimize(p.count());
  }
}

BENCHMARK(Protocol_Manual_Call);

// Copy construction benchmarks
static void Direct_Copy(benchmark::State& state) {
  ALike a;
  for (auto _ : state) {
    ALike copy(a);
    benchmark::DoNotOptimize(copy);
  }
}

BENCHMARK(Direct_Copy);

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
static void Direct_Move(benchmark::State& state) {
  ALike a;
  for (auto _ : state) {
    ALike moved(std::move(a));
    benchmark::DoNotOptimize(moved);
    a = std::move(moved);
    benchmark::DoNotOptimize(a);
  }
}

BENCHMARK(Direct_Move);

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
static void Direct_Swap(benchmark::State& state) {
  ALike a1;
  ALike a2;
  for (auto _ : state) {
    std::swap(a1, a2);
    benchmark::DoNotOptimize(a1);
    benchmark::DoNotOptimize(a2);
  }
}

BENCHMARK(Direct_Swap);

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
static void Direct_CtorDtor(benchmark::State& state) {
  for (auto _ : state) {
    ALike a;
    benchmark::DoNotOptimize(a);
  }
}

BENCHMARK(Direct_CtorDtor);

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
  benchmark::DoNotOptimize(view);
  for (auto _ : state) {
    benchmark::DoNotOptimize(view.name());
    benchmark::DoNotOptimize(view.count());
  }
}

BENCHMARK(ProtocolView_Virtual_Call);

static void ProtocolView_Manual_Call(benchmark::State& state) {
  ALike alike;
  xyz::protocol_view<xyz::A_manual> view(alike);
  benchmark::DoNotOptimize(view);
  for (auto _ : state) {
    benchmark::DoNotOptimize(view.name());
    benchmark::DoNotOptimize(view.count());
  }
}

BENCHMARK(ProtocolView_Manual_Call);

static void RawPointer_Call(benchmark::State& state) {
  ALike alike;
  ALike* ptr = &alike;
  benchmark::DoNotOptimize(ptr);
  for (auto _ : state) {
    benchmark::DoNotOptimize(ptr->name());
    benchmark::DoNotOptimize(ptr->count());
  }
}

BENCHMARK(RawPointer_Call);

// Jitter benchmarks to defeat branch prediction
static void ProtocolView_Virtual_Call_Jitter(benchmark::State& state) {
  ALike a1;
  ALikeToo a2;
  xyz::protocol_view<xyz::A_virtual> views[2] = {
      xyz::protocol_view<xyz::A_virtual>(a1),
      xyz::protocol_view<xyz::A_virtual>(a2)};

  benchmark::DoNotOptimize(views);

  size_t i = 0;
  for (auto _ : state) {
    auto& view = views[i & 1];
    benchmark::DoNotOptimize(view.name());
    benchmark::DoNotOptimize(view.count());
    ++i;
  }
}

BENCHMARK(ProtocolView_Virtual_Call_Jitter);

static void ProtocolView_Manual_Call_Jitter(benchmark::State& state) {
  ALike a1;
  ALikeToo a2;
  xyz::protocol_view<xyz::A_manual> views[2] = {
      xyz::protocol_view<xyz::A_manual>(a1),
      xyz::protocol_view<xyz::A_manual>(a2)};

  benchmark::DoNotOptimize(views);

  size_t i = 0;
  for (auto _ : state) {
    auto& view = views[i & 1];
    benchmark::DoNotOptimize(view.name());
    benchmark::DoNotOptimize(view.count());
    ++i;
  }
}

BENCHMARK(ProtocolView_Manual_Call_Jitter);

static void RawPointer_Call_Jitter(benchmark::State& state) {
  ALike a1;
  ALike a2;  // Raw pointer array must be of the same type, so this is just to
             // measure the array overhead
  ALike* ptrs[2] = {&a1, &a2};

  benchmark::DoNotOptimize(ptrs);

  size_t i = 0;
  for (auto _ : state) {
    auto* ptr = ptrs[i & 1];
    benchmark::DoNotOptimize(ptr->name());
    benchmark::DoNotOptimize(ptr->count());
    ++i;
  }
}

BENCHMARK(RawPointer_Call_Jitter);

}  // namespace

BENCHMARK_MAIN();
