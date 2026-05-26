#include "../allocator/buffer_allocator.h"
#include <iostream>
#include <cstring>
#include <random>

void banner(const char* txt)
{
    std::cout << "\n=== " << txt << " ===\n";
}

void try_write(CustomBuffer* buf, const char* tag)
{
    std::cout << tag << " write into buf@" << buf->data << " ...\n";
    std::memcpy(buf->data, "ABCDEFGH", 8);   // may fault once
    std::cout << "OK\n";
}

int main()
{
    PageAllocator alloc;

    // allocate two 1-KiB buffers (4-KiB page)
    constexpr std::size_t kBuf = 1024;
    CustomBuffer* a = alloc.allocate(kBuf);
    CustomBuffer* b = alloc.allocate(kBuf);

    PageMeta* pg = a->page;
    banner("allocated buffers");
    std::cout << "page base " << pg->base << '\n';

    // mark buffer A in-flight → page is PROT_READ
    banner("markInflight(A)");
    a->markInflight();
    std::cout << "io_refs=" << pg->io_refs.load()
              << "  is_prot=" << pg->is_protected << '\n';

    // mark buffer B in-flight → page is PROT_READ
    banner("markInflight(B)");
    b->markInflight();
    std::cout << "io_refs=" << pg->io_refs.load()
              << "  is_prot=" << pg->is_protected << '\n';

    // write while still in-flight (SIGSEGV once, handler un-protects)
    banner("write while in-flight (SIGSEGV expected)");
    try_write(a, "app");
    std::cout << "io_refs=" << pg->io_refs.load()
              << "  is_prot=" << pg->is_protected << '\n';

    // complete I/O
    banner("ioComplete(A)");
    a->ioComplete(&alloc);
    std::cout << "io_refs=" << pg->io_refs.load()
              << "  is_prot=" << pg->is_protected << '\n';

    banner("ioComplete(B)");
    b->ioComplete(&alloc);
    std::cout << "io_refs=" << pg->io_refs.load()
              << "  is_prot=" << pg->is_protected << '\n';

    // another write – should NOT fault now
    banner("second write (no fault expected)");
    try_write(b, "app");

    // clean-up: drop refs to test GC
    a->removeUser(&alloc);            // initial ref
    b->removeUser(&alloc);            // initial ref

    std::cout << "Final page app_refs=" << pg->app_refs.load() << " io_refs=" << pg->io_refs.load() << '\n';
    std::cout << "allocator munmaps when both app_refs & io_refs == 0\n";
}