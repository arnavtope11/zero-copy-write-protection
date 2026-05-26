#ifndef BUFFER_ALLOCATOR_H
#define BUFFER_ALLOCATOR_H

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>

extern const std::size_t kPageSize;

class CustomBuffer;

struct PageMeta {
    void*                 base;          // page start (mmap-ed)
    std::size_t           used_bytes;    // bump pointer
    std::atomic<int>      io_refs;       // number of in-flight I/Os touching this page
    std::atomic<int>      app_refs;      // total CustomBuffer::users in this page
    bool                  is_protected;  // currently PROT_READ only
    PageMeta*             next;          // intrusive list
    
    // ctor
    explicit PageMeta(void* addr) :
        base(addr), used_bytes(0), app_refs(0), io_refs(0), is_protected(false), 
        next(nullptr) {}
};

// Simple slab allocator for page-based zero-copy buffers
class PageAllocator {
public:
    PageAllocator()  : head_(nullptr) {}
    ~PageAllocator();

    // returns a new CustomBuffer* carved out of a page
    CustomBuffer* allocate(std::size_t size);

    // called when a CustomBuffer is fully reclaimed
    void maybe_free_page(PageMeta* pg);

    bool protect(PageMeta* pg);
    bool unprotect(PageMeta* pg);

private:
    PageMeta* head_;                   // list of active pages
    PageMeta* allocate_page();         // mmap one page
};

class CustomBuffer {
    public:
        void* data; // actual memory
        size_t size; // Size of the memory buffer
        PageMeta* page;
        /*
         * This is necessary because we don't want to deallocate when something else is potentially using it I think.
         */
        std::atomic<int> users; // Reference count i.e. how many "users" have access to this buffer

        CustomBuffer(void* d, std::size_t sz, PageMeta* pg);
        ~CustomBuffer();

        void addUser(bool from_nic = false); // attach to this buffer (increase reference count)
        void removeUser(PageAllocator* alloc, bool from_nic = false); // detach from this buffer (decrease reference count)

        void markInflight();

        void ioComplete(PageAllocator* alloc);
};

namespace dk {

// global intrusive list head used by SIGSEGV handler
extern std::atomic<PageMeta*> g_page_list_head;

// install the handler once
void install_segv_handler();

// internal helper called by allocator when a new page is created
void register_page(PageMeta* pg);

}

#endif // BUFFER_ALLOCATOR_H
