// Boot-time memory micro-benchmark (console 0.65: the guest's main thread ran 30-50 times slower
// than on x86 while Eden ran the same code at 22-45 fps: is the guest arena's memory slow?).
// Three 64 MB regions: malloc memory, a code-memory block aligned to 4 KB (how memory_nx.cpp maps
// the arena) and one aligned to 2 MB (large-page mapping). Each gets sequential write and read,
// memcpy, and a dependent-load chase; svcQueryMemory reports the mapping's type and attributes.
// Results go to hcu_log.txt as "[nx] membench ..." lines. About one second in total.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <thread>
#include <time.h>

#include <switch.h>

namespace {

constexpr size_t kSize = 64u << 20;
constexpr size_t kChaseSteps = 1u << 20;

double ms_since(u64 start) {
    return (double)armTicksToNs(armGetSystemTick() - start) / 1e6;
}

void bench(const char* name, uint8_t* p) {
    MemoryInfo info{};
    u32 page_info = 0;
    svcQueryMemory(&info, &page_info, (u64)p);
    // Sequential write.
    u64 t = armGetSystemTick();
    std::memset(p, 0x5A, kSize);
    const double write_ms = ms_since(t);
    // Sequential read.
    t = armGetSystemTick();
    u64 sum = 0;
    const u64* q = reinterpret_cast<const u64*>(p);
    for (size_t i = 0; i < kSize / 8; i += 4) {
        sum += q[i] + q[i + 1] + q[i + 2] + q[i + 3];
    }
    const double read_ms = ms_since(t);
    // memcpy of the first half onto the second.
    t = armGetSystemTick();
    std::memcpy(p + kSize / 2, p, kSize / 2);
    const double copy_ms = ms_since(t);
    // Dependent loads: a pointer chase with a 4 KB + 64 B stride through the whole region.
    u64* links = reinterpret_cast<u64*>(p);
    const size_t count = kSize / 8;
    const size_t stride = (4096 + 64) / 8;
    for (size_t i = 0; i < count; i++) links[i] = 0;
    size_t idx = 0;
    for (size_t n = 0; n < kChaseSteps; n++) {
        size_t next = (idx + stride) % count;
        links[idx] = (u64)next;
        idx = next;
    }
    t = armGetSystemTick();
    idx = 0;
    for (size_t n = 0; n < kChaseSteps; n++) idx = (size_t)links[idx];
    const double chase_ns = ms_since(t) * 1e6 / kChaseSteps;
    std::printf("[nx] membench %s: write %.0f MB/s, read %.0f MB/s, memcpy %.0f MB/s, dependent load %.1f ns"
                " (type 0x%X, attr 0x%X, perm 0x%X, sum %llx)\n",
                name, kSize / (write_ms / 1000.0) / 1048576.0, kSize / (read_ms / 1000.0) / 1048576.0,
                (kSize / 2) / (copy_ms / 1000.0) / 1048576.0, chase_ns, info.type, info.attr, info.perm,
                (unsigned long long)(sum + idx));
}

void bench_code_memory(const char* name, size_t alignment) {
    void* backing = aligned_alloc(alignment, kSize);
    if (!backing) {
        std::printf("[nx] membench %s: no RAM\n", name);
        return;
    }
    Handle handle = INVALID_HANDLE;
    Result rc = svcCreateCodeMemory(&handle, backing, kSize);
    if (R_FAILED(rc)) {
        std::printf("[nx] membench %s: svcCreateCodeMemory 0x%X\n", name, rc);
        std::free(backing);
        return;
    }
    // virtmemFindCodeMemory's second argument is a guard size, not an alignment: find a larger
    // range and round the start up, so virtual and physical addresses share the alignment.
    VirtmemReservation* reservation = nullptr;
    virtmemLock();
    void* raw = virtmemFindCodeMemory(kSize + alignment, 0x1000);
    void* at = nullptr;
    if (raw) {
        reservation = virtmemAddReservation(raw, kSize + alignment);
        at = reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(raw) + alignment - 1) & ~(uintptr_t)(alignment - 1));
    }
    virtmemUnlock();
    rc = at ? svcControlCodeMemory(handle, CodeMapOperation_MapOwner, at, kSize, Perm_Rw) : 1;
    if (R_FAILED(rc)) {
        std::printf("[nx] membench %s: MapOwner 0x%X\n", name, rc);
    } else {
        bench(name, static_cast<uint8_t*>(at));
        svcControlCodeMemory(handle, CodeMapOperation_UnmapOwner, at, kSize, Perm_None);
    }
    if (reservation) {
        virtmemLock();
        virtmemRemoveReservation(reservation);
        virtmemUnlock();
    }
    svcCloseHandle(handle);
    std::free(backing);
}

}  // namespace

// How long a 1 ms sleep really takes, through the three paths the runtime uses (console 0.65:
// a constant 478 ms frame smells like a sleep or wait far longer than asked).
void bench_sleeps() {
    u64 t = armGetSystemTick();
    for (int i = 0; i < 20; i++) svcSleepThread(1'000'000LL);
    const double svc_ms = ms_since(t) / 20;
    t = armGetSystemTick();
    for (int i = 0; i < 20; i++) {
        timespec ts{0, 1'000'000};
        nanosleep(&ts, nullptr);
    }
    const double nanosleep_ms = ms_since(t) / 20;
    t = armGetSystemTick();
    for (int i = 0; i < 20; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const double sleep_for_ms = ms_since(t) / 20;
    t = armGetSystemTick();
    for (int i = 0; i < 1000; i++) sched_yield();
    const double yield_us = ms_since(t);
    s32 prio = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    std::printf("[nx] sleepbench: 1 ms sleep takes svcSleepThread %.2f ms, nanosleep %.2f ms, "
                "std::sleep_for %.2f ms; sched_yield %.1f us; main thread priority 0x%X\n",
                svc_ms, nanosleep_ms, sleep_for_ms, yield_us, prio);
}

extern "C" void rex_nx_membench(void) {
    bench_sleeps();
    void* heap = aligned_alloc(0x1000, kSize);
    if (heap) {
        bench("malloc", static_cast<uint8_t*>(heap));
        std::free(heap);
    }
    bench_code_memory("code memory 4 KB aligned", 0x1000);
    bench_code_memory("code memory 2 MB aligned", 0x200000);
    std::fflush(stdout);
}
