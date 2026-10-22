// rex::thread::Fiber for Horizon (replaces fiber_posix.cpp): newlib has no ucontext. A context is
// the AArch64 callee-saved state: sp, lr, x19-x29, d8-d15 (24 words, fiber.h).
#include <cassert>
#include <cstdint>

#include <rex/thread/fiber.h>

extern "C" void hcu_fiber_swap(uint64_t* from, const uint64_t* to);

// Layout: [0] sp, [1] lr, [2..12] x19-x29, [13..20] d8-d15, [21..23] spare.
__asm__(
    ".text\n"
    ".global hcu_fiber_swap\n"
    ".type hcu_fiber_swap, %function\n"
    "hcu_fiber_swap:\n"
    "  mov x9, sp\n"
    "  stp x9, x30, [x0, #0]\n"
    "  stp x19, x20, [x0, #16]\n"
    "  stp x21, x22, [x0, #32]\n"
    "  stp x23, x24, [x0, #48]\n"
    "  stp x25, x26, [x0, #64]\n"
    "  stp x27, x28, [x0, #80]\n"
    "  str x29, [x0, #96]\n"
    "  stp d8, d9, [x0, #104]\n"
    "  stp d10, d11, [x0, #120]\n"
    "  stp d12, d13, [x0, #136]\n"
    "  stp d14, d15, [x0, #152]\n"
    "  ldp x9, x30, [x1, #0]\n"
    "  ldp x19, x20, [x1, #16]\n"
    "  ldp x21, x22, [x1, #32]\n"
    "  ldp x23, x24, [x1, #48]\n"
    "  ldp x25, x26, [x1, #64]\n"
    "  ldp x27, x28, [x1, #80]\n"
    "  ldr x29, [x1, #96]\n"
    "  ldp d8, d9, [x1, #104]\n"
    "  ldp d10, d11, [x1, #120]\n"
    "  ldp d12, d13, [x1, #136]\n"
    "  ldp d14, d15, [x1, #152]\n"
    "  mov sp, x9\n"
    "  ret\n"
    ".size hcu_fiber_swap, .-hcu_fiber_swap\n");

namespace rex::thread {

thread_local Fiber* Fiber::tls_current_ = nullptr;

Fiber* Fiber::ConvertCurrentThread() {
    if (tls_current_) {
        return tls_current_;
    }
    Fiber* fiber = new Fiber();
    fiber->is_thread_fiber_ = true;
    tls_current_ = fiber;
    return fiber;
}

void Fiber::Trampoline() {
    Fiber* self = tls_current_;
    self->entry_(self->arg_);
    // A fiber's entry never returns (the SDK switches away before). Stop loudly if it does.
    assert(false && "fiber entry returned");
    for (;;) {
    }
}

Fiber* Fiber::Create(size_t stack_size, void (*entry)(void*), void* arg) {
    Fiber* fiber = new Fiber();
    fiber->stack_.resize(stack_size + 16);
    fiber->entry_ = entry;
    fiber->arg_ = arg;
    uintptr_t top = reinterpret_cast<uintptr_t>(fiber->stack_.data() + fiber->stack_.size()) & ~uintptr_t(15);
    fiber->context_[0] = top;
    fiber->context_[1] = reinterpret_cast<uint64_t>(&Fiber::Trampoline);
    return fiber;
}

void Fiber::SwitchTo(Fiber* target) {
    Fiber* current = tls_current_;
    assert(current && "SwitchTo before ConvertCurrentThread");
    if (current == target) {
        return;
    }
    tls_current_ = target;
    hcu_fiber_swap(current->context_, target->context_);
}

void Fiber::Destroy() {
    if (is_thread_fiber_) {
        tls_current_ = nullptr;
    } else {
        assert(this != tls_current_ && "Destroy called on the currently running fiber");
    }
    delete this;
}

}  // namespace rex::thread
