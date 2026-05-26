#include <csignal>
#include <cstring>

#include "../allocator/buffer_allocator.h"
#include "../user/SafeWriter.h"
#include <iostream>
#include <vector>
#include <random>

void printTestDescription(const std::string& name)
{
    std::cout << "\n\n";
    std::cout << "==================\n";
    std::cout << name << "\n";
    std::cout << "==================" << std::endl;
}

void testAllocator(PageAllocator& alloc)
{
    printTestDescription("Test Allocator Logic");

    constexpr std::size_t kBufSize = 1024;      // 4 buffers == one 4 KiB page
    std::vector<CustomBuffer*> bufs;

    // allocate four 1 KiB buffers → fills exactly one page
    for (int i = 0; i < 4; ++i) {
        auto* b = alloc.allocate(kBufSize);
        std::cout << "Allocated buffer " << i
                  << " at " << b->data
                  << "  users=" << b->users.load()
                  << "  page used=" << b->page->used_bytes << "\n";
        bufs.push_back(b);
    }
    PageMeta* page = bufs[0]->page;
    std::cout << "Page base: " << page->base << "  app_refs="
              << page->app_refs.load() << "\n\n";

    // bump application refs a few times at random
    std::mt19937 rng{1234};
    std::uniform_int_distribution<int> pick(0, bufs.size() - 1);
    for (int i = 0; i < 6; ++i) {
        auto* b = bufs[pick(rng)];
        // duplicate app reference
        b->addUser();
        std::cout << "addUser on buf@" << b->data
                  << "  users=" << b->users.load()
                  << "  page.app_refs=" << page->app_refs.load() << '\n';
    }
    std::cout << "\n";

    // randomly drop every reference
    while (!bufs.empty()) {
        int idx = pick(rng) % bufs.size();
        auto* b = bufs[idx];
        b->removeUser(&alloc);
        std::cout << "removeUser on buf@" << b->data
                  << "  users(now)=" << b->users.load()
                  << "  page.app_refs=" << page->app_refs.load() << '\n';

        // Erase buffer from vector if its users reached 0 (object deleted)
        if (b->users.load() == 0) bufs.erase(bufs.begin() + idx);
    }

    std::cout << "\nAll user refs gone.\n";
    std::cout << "Page pointer after GC = " << page->base << '\n';
}

void testProtectAndUnprotect(PageAllocator& alloc)
{
    printTestDescription("Test Protect and Unprotect");

    constexpr std::size_t kBufSize = 1024;
    std::vector<CustomBuffer*> bufs;

    // allocate 4 buffers
    for (int i = 0; i < 4; ++i) {
        auto* b = alloc.allocate(kBufSize);
        bufs.push_back(b);
    }

    PageMeta* page = bufs[0]->page;

    // Write to buffer before protection (should succeed)
    std::cout << "Writing to buffer before protect\n";
    strcpy(static_cast<char*>(bufs[0]->data), "Hello, world!");
    std::cout << "Buffer content: " << static_cast<char*>(bufs[0]->data) << '\n';

    // Protect the page
    std::cout << "Protecting page...\n";
    alloc.protect(page);

    // DEBUG: Uncomment to test seg fault.
    // std::cout << "Attempting write to protected buffer (should cause segfault)...\n";
    // strcpy(static_cast<char*>(bufs[0]->data), "This write should fail");

    // Test buffer content after failed write.
    std::cout << "Buffer content: " << static_cast<char*>(bufs[0]->data) << '\n';

    // Unprotect page
    std::cout << "Unprotecting page...\n";
    alloc.unprotect(page);

    // Write after unprotect (should succeed)
    std::cout << "Writing to buffer after unprotect\n";
    strcpy(static_cast<char*>(bufs[0]->data), "Hello, again!");
    std::cout << "Buffer content: " << static_cast<char*>(bufs[0]->data) << '\n';

    // Clean up references as before
    for (auto* b : bufs) {
        b->removeUser(&alloc);
    }
}

void testProtectedFaultHandling(PageAllocator& alloc) {
    printTestDescription("Test Segfault Handling on Protected Page");

    // Install segfault handler for this user task
    SafeWriter::setup();

    constexpr std::size_t kBufSize = 1024;
    auto* buf = alloc.allocate(kBufSize);
    PageMeta* page = buf->page;

    // Write before protection
    std::cout << "Writing to buffer before protection...\n";
    // std::strcpy(static_cast<char*>(buf->data), "Hello, world!");
    SafeWriter::write(buf, "Hello, world!");
    std::cout << "Buffer content: " << static_cast<char*>(buf->data) << "\n";

    // Protect page
    std::cout << "Protecting buffer page...\n";
    alloc.protect(page);

    // Attempt to write after protection
    std::cout << "Attempting to write after buffer is protected...\n";
    SafeWriter::write(buf, "Hello, modified!");
    std::cout << "Buffer content: " << static_cast<char*>(buf->data) << "\n";

    alloc.unprotect(page);
    std::cout << "Attempting to write after buffer is unprotected...\n";
    SafeWriter::write(buf, "Hello, modified!");
    std::cout << "Buffer content: " << static_cast<char*>(buf->data) << "\n";

    // Clean up
    buf->removeUser(&alloc);
}

int main() {
    PageAllocator alloc;

    testAllocator(alloc);
    testProtectAndUnprotect(alloc);
    testProtectedFaultHandling(alloc);

    return 0;
}
