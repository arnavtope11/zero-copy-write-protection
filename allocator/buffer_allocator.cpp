#include "buffer_allocator.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <unistd.h> // for getpagesize()
#include <sys/mman.h> // for mprotect()
#include <list>
#include <cassert>
#include <mutex>

const std::size_t kPageSize = static_cast<std::size_t>(::getpagesize());

namespace dk {

// globals 
std::atomic<PageMeta*> g_page_list_head { nullptr };
static std::once_flag  install_flag;           // for call_once
static sigjmp_buf      dummy_env;              // not used, but required

// forward decl
static void segv_handler(int, siginfo_t*, void*);

// helper
void install_segv_handler()
{
    std::call_once(install_flag, []{
        struct sigaction sa;
        sa.sa_sigaction = segv_handler;
        sa.sa_flags     = SA_SIGINFO | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGSEGV, &sa, nullptr) != 0)
            std::perror("sigaction");
    });
}

void register_page(PageMeta* pg)
{
    // intrusive lock-free push_front
    PageMeta* old_head = g_page_list_head.load(std::memory_order_relaxed);
    do {
        pg->next = old_head;
    } while (!g_page_list_head.compare_exchange_weak(
                 old_head, pg, std::memory_order_release,
                                   std::memory_order_relaxed));
}

static PageMeta* find_page(void* fault_addr)
{
    void* page_base = reinterpret_cast<void*>(
        (reinterpret_cast<std::uintptr_t>(fault_addr)) & ~(kPageSize - 1));

    for (PageMeta* pg = g_page_list_head.load(std::memory_order_acquire);
         pg; pg = pg->next)
    {
        if (pg->base == page_base) return pg;
    }
    return nullptr;
}

// SIGSEGV handler
static void segv_handler(int sig, siginfo_t* info, void*)
{
    std::cout << "In SIGSEGV handler, write fault recorded -> unprotecting page...\n";
    void* addr = info->si_addr;
    PageMeta* pg = find_page(addr);
    if (!pg || !pg->is_protected) {
        // Not one of our pages → chain to default
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
        return;
    }

    // unprotect the page on write fault
    mprotect(pg->base, kPageSize, PROT_READ | PROT_WRITE);
    pg->is_protected = false;

    // handler returns, faulting store retries
    // TODO: consider copy-on-write for referenced pages for more robustness    
}

} // namespace dk

// mmap one page & wrap it in PageMeta
PageMeta* PageAllocator::allocate_page() {
    dk::install_segv_handler();          // install once
    
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void* addr = ::mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE,
                        flags, -1, 0);
    if (addr == MAP_FAILED)
        throw std::runtime_error("mmap failed");

    auto* pg = new PageMeta(addr);

    // register in global list for SIGSEGV look-up
    dk::register_page(pg);

    // insert in allocator’s list
    pg->next = head_;
    head_ = pg;
    return pg;
}

CustomBuffer* PageAllocator::allocate(std::size_t sz) {
    // page-aligned size to 8-byte boundary for simplicity
    sz = (sz + 7) & ~static_cast<std::size_t>(7);

    PageMeta* pg = head_;
    while (pg && pg->used_bytes + sz > kPageSize)
        pg = pg->next;

    if (!pg) pg = allocate_page();

    // carve buffer
    std::size_t offset = pg->used_bytes;
    pg->used_bytes += sz;

    char* buf_addr = reinterpret_cast<char*>(pg->base) + offset;

    auto* buf = new CustomBuffer(buf_addr, sz, pg);

    // bookkeeping
    pg->app_refs.fetch_add(1, std::memory_order_relaxed);

    return buf;
}

// Called whenever a buffer's app_refs reach zero
void PageAllocator::maybe_free_page(PageMeta* pg) {
    if (pg->app_refs.load(std::memory_order_acquire) == 0 && 
        pg->io_refs.load(std::memory_order_acquire) == 0)
    {
        // unlink from list
        if (head_ == pg) head_ = pg->next;
        else {
            PageMeta* cur = head_;
            while (cur && cur->next != pg) cur = cur->next;
            if (cur) cur->next = pg->next;
        }
        ::munmap(pg->base, kPageSize);
        std::cout << "Page unmapped at " << pg->base << '\n';
        delete pg;
    }
}

// for testing simple fault handling with SafeWriter
bool PageAllocator::protect(PageMeta* pg) {
    if (pg->is_protected) return true; // already protected

    if (::mprotect(pg->base, kPageSize, PROT_READ) == 0) {
        pg->is_protected = true;
        return true;
    } else {
        perror("mprotect PROT_READ failed");
        return false;
    }
}

// for testing simple fault handling with SafeWriter
bool PageAllocator::unprotect(PageMeta* pg) {
    if (!pg->is_protected) return true; // already unprotected

    if (::mprotect(pg->base, kPageSize, PROT_READ | PROT_WRITE) == 0) {
        pg->is_protected = false;
        return true;
    } else {
        perror("mprotect PROT_READ|PROT_WRITE failed");
        return false;
    }
}


PageAllocator::~PageAllocator() {
    PageMeta* cur = head_;
    while (cur) {
        ::munmap(cur->base, kPageSize);
        PageMeta* nxt = cur->next;
        delete cur;
        cur = nxt;
    }
}

CustomBuffer::CustomBuffer(void* d, size_t sz, PageMeta* pg)
    : data(d), size(sz), page(pg), users(1) {}

CustomBuffer::~CustomBuffer() = default;

void CustomBuffer::addUser(bool from_nic) {
    users.fetch_add(1, std::memory_order_relaxed);
    if (!from_nic)
        page->app_refs.fetch_add(1, std::memory_order_relaxed);
}

// Pass in allocator so we can possibly free the page when refcount hits 0
void CustomBuffer::removeUser(PageAllocator* alloc, bool from_nic) {
    if (!from_nic)
        page->app_refs.fetch_sub(1, std::memory_order_acq_rel);

    if (users.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        alloc->maybe_free_page(page);
        delete this;                      // buffer object itself
    }
}

void CustomBuffer::markInflight() {
    PageMeta* pg = page;
    pg->io_refs.fetch_add(1, std::memory_order_relaxed);
    if (!pg->is_protected) {
        mprotect(pg->base, kPageSize, PROT_READ);
        pg->is_protected = true;
    }    
    
    // NIC now owns an extra reference
    addUser(true);
}

void CustomBuffer::ioComplete(PageAllocator* alloc) {
    PageMeta* pg = page;
    if (pg->io_refs.fetch_sub(1) == 1 && pg->is_protected) {
        mprotect(pg->base, kPageSize, PROT_READ | PROT_WRITE);
        pg->is_protected = false;
    }

    // drop NIC’s reference; may free the page
    removeUser(alloc, true);
}
