#ifndef USER_FAULT_HANDLING_H
#define USER_FAULT_HANDLING_H

#include "../allocator/buffer_allocator.h"
#include <iostream>

class SafeWriter
{
    public:
        static void setup();
        static bool write(CustomBuffer* buf, const std::string& msg);
};

#endif //USER_FAULT_HANDLING_H
