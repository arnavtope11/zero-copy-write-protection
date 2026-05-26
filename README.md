# Zero-Copy I/O Memory Management

## Overview

Efficient I/O is essential in high-performance and latency-sensitive systems, especially within modern data centers. Traditional I/O stacks rely on kernel mediation and multiple buffer copies between user and kernel spaces, incurring substantial CPU and memory overhead. Our project introduces a lightweight, general-purpose **zero-copy memory management layer** that preserves read safety through memory protection and application-level fault handling.

This system enables direct memory access for I/O devices while maintaining application isolation and safety. It leverages:
- Memory page protection
- Custom signal (SIGSEGV) handling
- Shared memory buffer coordination

---

## Features

- **Zero-Copy Design**: Enables NICs and user-space I/O stacks to directly access application memory.
- **Read Safety**: Protects buffers using `mprotect()` and defers write access to fault-time checks.
- **SIGSEGV Handling**: Custom handler enables dynamic page unprotection at first write.
- **Application Transparency**: Compatible with typical memory access patterns (`memcpy`, `strcpy`) and signal-safe functions.
- **Fault Policy**: Protect-by-default; fallback to unprotecting on first write (with plans for CoW).

---

## Implementation
- **Signal Handling**:
  - A custom `SIGSEGV` handler intercepts illegal writes to protected memory.
  - The handler checks if the page is eligible for write access and unprotects it dynamically.

- **Memory Protection**:
  - Buffers are protected using `mprotect()` after being shared with the I/O subsystem.
  - Pages are unprotected only when necessary, minimizing unnecessary faults.

- **Limitations**:
  - No Copy-on-Write (CoW) yet: Signal handlers cannot safely allocate or copy memory.
  - Page-level granularity may lead to coarse protection for small buffers.
  - Hardware-agnostic approach misses optimizations from features like Intel MPK.

### Files

- **`buffer_allocator.h/cpp`**: Main implementation of the memory manager. Defines how buffers are allocated, protected, and tracked.
- **`test_allocator.cpp`**: Tests allocator behavior, manual protection/unprotection, and safe recovery from protection faults using reference counting and signal handling.
- **`test_in_flight.cpp`**: Verifies that protected buffers used by the NIC trigger a SIGSEGV on write, which is safely handled by dynamically unprotecting the page.
- **`Makefile`**: Simple build configuration for compiling the test and allocator.

---

### Build Instructions

```bash
git clone https://github.com/viralbhadeshiya/ECS-289D-final-project.git
cd ECS-289D-final-project
make
```

