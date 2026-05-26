#include "SafeWriter.h"

#include <csignal>
#include <cstring>

#include "../allocator/buffer_allocator.h"
#include <csetjmp>
#include <iostream>
#include <cstring>

// Static jump buffer for fault recovery
static sigjmp_buf jump_buffer;

void SafeWriter::setup() {
    signal(SIGSEGV, [](int signum) -> void
    {
        std::cerr << "Caught segmentation fault (SIGSEGV): Write attempt on protected page blocked.\n";
        siglongjmp(jump_buffer, 1);
    });
}

bool SafeWriter::write(CustomBuffer* buf, const std::string& msg) {
    if (sigsetjmp(jump_buffer, 1) == 0) {
        std::strcpy(static_cast<char*>(buf->data), msg.c_str());
        return true;
    }

    std::cerr << "Recovered from illegal write attempt.\n";
    return false;
}
