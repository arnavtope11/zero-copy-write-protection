//---------------------------------------------------------------
// bench_allocator.cpp
//---------------------------------------------------------------
#include "allocator/buffer_allocator.h"
#include <x86intrin.h>          // rdtsc / __rdtsc()
#include <cstring>
#include <iostream>

static inline uint64_t cycles() { return __rdtsc(); }

template<typename F>
uint64_t avg_cycles(F fn, int iters)
{
    uint64_t start = cycles();
    for (int i = 0; i < iters; ++i) fn();
    return (cycles() - start) / iters;
}

void bench(PageAllocator& alloc)
{
    constexpr std::size_t kBuf = 1024;

    // warm page – never freed until after the benchmark
    CustomBuffer* warm = alloc.allocate(kBuf);

    // benchmark: add/drop an extra app reference
    uint64_t alloc_fast = avg_cycles([&]{
        warm->addUser();            // cheap (+users, +app_refs)
        warm->removeUser(&alloc);   // cheap (-users, -app_refs)
    }, 100000);

    std::cout << "allocate-fast   : " << alloc_fast << " cycles\n";

    // clean-up
    warm->removeUser(&alloc);

    // markInflight + ioComplete
    auto* buf = alloc.allocate(kBuf);
    uint64_t infl_cyc = avg_cycles([&]{
        buf->markInflight();
        buf->ioComplete(&alloc);
    }, 10000);                  // syscalls -> fewer iterations
    std::cout << "markInflight    : " << infl_cyc << " cycles\n";

    // first write fault
    buf->markInflight();
    uint64_t t0 = cycles();
    std::memset(buf->data, 0, 8);   // one SEGV handled
    uint64_t fault_cyc = cycles() - t0;
    std::cout << "1st write fault : " << fault_cyc << " cycles\n";
    buf->ioComplete(&alloc);
    buf->removeUser(&alloc);
}

int main()
{
    PageAllocator alloc;
    bench(alloc);
    return 0;
}
