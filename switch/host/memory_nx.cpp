// rex::memory for Horizon (replaces memory_posix.cpp). The guest arena is a 4.5 GB address-space
// reservation (libnx virtmem) with at most 8 code-memory blocks placed at base + guest address,
// each backed by RAM from libnx's heap. Only one R/W view of the 512 MB physical memory exists
// (the 0xA0000000 block): the 0xC0000000, 0xE0000000, raw-physical and 0x90000000 views are folded
// onto their blocks in software (xmemory.h TranslateVirtual, the codegen pch's
// REX_PHYS_HOST_OFFSET). Reasons: CLAUDE.md "Horizon memory probe on hardware".
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <unistd.h>

#include <switch.h>

extern "C" char* fake_heap_start;  // libnx: newlib's sbrk works inside [start, end)
extern "C" char* fake_heap_end;

#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>
#include <rex/thread/mutex.h>

namespace rex::memory {

namespace {

struct Arena {
    uint8_t* base = nullptr;
    size_t size = 0;
    VirtmemReservation* reservation = nullptr;
};
Arena arena;

struct Block {
    uint64_t guest_start;
    size_t size;
    Handle handle;
    bool mapped;
};
// The blocks, keyed by the guest address of the view that asks for them. The sizes are the RAM
// budget (~1 GB of the 3.2 GB title takeover gives); the SDK heaps allocate bottom-up on the
// Switch, so a range stays inside its block until the game really uses more.
Block blocks[] = {
    {0x00000000, 256u << 20, INVALID_HANDLE, false},  // 4 KB virtual heap, bottom
    {0x30000000, 64u << 20, INVALID_HANDLE, false},   // 4 KB heap top 256 MB: the kernel's system heap
    {0x40000000, 128u << 20, INVALID_HANDLE, false},  // 64 KB virtual heap, bottom
    {0x70000000, 64u << 20, INVALID_HANDLE, false},   // guest thread stacks (xthread.h 0x70000000-0x7EFFFFFF)
    {0x80000000, 64u << 20, INVALID_HANDLE, false},   // XEX image + kernel modules
    {0xA0000000, 512u << 20, INVALID_HANDLE, false},  // the physical arena (one view)
};

// The views that are folded onto a block (xmemory.h TranslateVirtual, the codegen pch): the same
// fold for the commit checks below.
uint64_t fold_guest(uint64_t guest) {
    if (guest >= 0xE0000000ull) return 0xA0000000ull + (guest - 0xE0000000ull) + 0x1000;
    if (guest >= 0xC0000000ull) return guest - 0x20000000ull;
    if (guest >= 0x90000000ull && guest < 0xA0000000ull) return guest - 0x10000000ull;
    if (guest >= 0x7F000000ull && guest < 0x80000000ull) return guest + 0x21000000ull;
    return guest;
}

bool in_mapped_block(uint64_t guest, size_t length) {
    guest = fold_guest(guest);
    for (const Block& block : blocks) {
        if (block.mapped && guest >= block.guest_start && guest + length <= block.guest_start + block.size) {
            return true;
        }
    }
    return false;
}

// libnx's heap (newlib malloc) and the process memory, for the log: what a failed block allocation
// had to work with.
void print_heap_state(const char* when) {
    const struct mallinfo mi = mallinfo();
    u64 heap_size = 0, total = 0, used = 0;
    svcGetInfo(&heap_size, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    std::printf("[nx_memory] heap %s: region %llu MB, malloc arena %zu MB, in use %zu MB, free %zu MB, "
                "top %zu MB; process %llu of %llu MB\n",
                when, (unsigned long long)(heap_size >> 20), mi.arena >> 20, mi.uordblks >> 20,
                mi.fordblks >> 20, mi.keepcost >> 20, (unsigned long long)(used >> 20),
                (unsigned long long)(total >> 20));
}

// The RAM behind every block, taken from the heap once, as the program's first allocation
// (rex_nx_reserve_pool from userAppInit). Separate aligned_alloc calls per block failed twice on the
// console (0.63, 0.79) whenever other heap activity came first: newlib's allocator grew the arena
// by 256 MB and still returned null. The first allocation on a fresh heap never did.
uint8_t* pool = nullptr;
size_t pool_size = 0, pool_used = 0;

bool map_block(Block& block) {
    uint8_t* at = arena.base + block.guest_start;
    void* backing = nullptr;
    if (pool != nullptr && pool_used + block.size <= pool_size) {
        backing = pool + pool_used;
        pool_used += block.size;
    } else {
        backing = aligned_alloc(0x200000, block.size);
    }
    if (!backing) {
        std::printf("[nx_memory] no RAM for the 0x%llX block (%zu MB)\n", (unsigned long long)block.guest_start,
                    block.size >> 20);
        print_heap_state("at the failure");
        // The SDK's failure path tears the presenter down from this thread and crashes in free();
        // a readable log line and a clean exit are worth more than a crash report (console 0.63).
        std::printf("[nx] fatal: not enough RAM for the guest memory; exiting\n");
        std::fflush(stdout);
        fsync(fileno(stdout));
        svcExitProcess();
    }
    Result rc = svcCreateCodeMemory(&block.handle, backing, block.size);
    if (R_FAILED(rc)) {
        std::printf("[nx_memory] svcCreateCodeMemory(0x%llX, %zu MB) failed: 0x%X\n",
                    (unsigned long long)block.guest_start, block.size >> 20, rc);
        return false;
    }
    rc = svcControlCodeMemory(block.handle, CodeMapOperation_MapOwner, at, block.size, Perm_Rw);
    if (R_FAILED(rc)) {
        std::printf("[nx_memory] MapOwner(0x%llX -> %p, %zu MB) failed: 0x%X\n", (unsigned long long)block.guest_start,
                    at, block.size >> 20, rc);
        return false;
    }
    // The owner view does not show the source's contents (the probe): zero it here.
    std::memset(at, 0, block.size);
    block.mapped = true;
    std::printf("[nx_memory] mapped guest 0x%llX, %zu MB at %p\n", (unsigned long long)block.guest_start,
                block.size >> 20, at);
    return true;
}

}  // namespace

// GPU write tracking without page faults (CLAUDE.md "Switch memory design"): the codegen pch's
// store macros set one byte per 4 KB page of the physical arena; SharedMemory flushes them into
// the SDK's own invalidation bookkeeping before it uploads anything.
extern "C" volatile uint8_t rex_nx_dirty[0x20000];
extern "C" volatile uint8_t rex_nx_dirty[0x20000] = {};
// One byte per 1 MB, set with the fine one: the flush scans this first (512 bytes per draw).
extern "C" volatile uint8_t rex_nx_dirty_coarse[0x200];
extern "C" volatile uint8_t rex_nx_dirty_coarse[0x200] = {};

// Host writes into guest memory that bypass the store macros (the SDK's file reads, xfile.cpp on the
// Switch) mark their pages here, by guest virtual address (any view of the physical arena).
extern "C" void rex_nx_mark_range(uint32_t guest, uint32_t length) {
    if (!length) return;
    const uint64_t f = fold_guest(guest);
    if (f < 0xA0000000ull) return;  // not the physical arena: not GPU-visible
    const uint64_t first = (f - 0xA0000000ull) >> 12, last = (f - 0xA0000000ull + length - 1) >> 12;
    for (uint64_t p = first; p <= last && p < 0x20000; p++) {
        rex_nx_dirty[p] = 1;
        rex_nx_dirty_coarse[p >> 8] = 1;
    }
}

namespace nx {
uint8_t* arena_base() { return arena.base; }

// How often the command processor asks for the dirty pages and how many it gets (the 2-s line).
extern "C" volatile uint64_t rex_nx_flush_calls = 0, rex_nx_flush_pages = 0;

void FlushDirtyPages(rex::memory::Memory& memory) {
    rex_nx_flush_calls++;
    const uint64_t* words = reinterpret_cast<const uint64_t*>(const_cast<const uint8_t*>(rex_nx_dirty));
    for (uint32_t c = 0; c < 0x200; c++) {
        if (!rex_nx_dirty_coarse[c]) {
            continue;
        }
        rex_nx_dirty_coarse[c] = 0;
        for (uint32_t w = c * 32; w < c * 32 + 32; w++) {
            if (words[w] == 0) {
                continue;
            }
            for (uint32_t i = w * 8; i < w * 8 + 8; i++) {
                if (!rex_nx_dirty[i]) {
                    continue;
                }
                rex_nx_dirty[i] = 0;
                rex_nx_flush_pages++;
                memory.TriggerPhysicalMemoryCallbacks(rex::thread::global_critical_region::AcquireDirect(),
                                                      0xA0000000u + (i << 12), 0x1000, true, false, false);
            }
        }
    }
}
}  // namespace nx

size_t page_size() { return 0x1000; }
size_t allocation_granularity() { return 0x1000; }

bool IsWritableExecutableMemorySupported() { return false; }

void* AllocFixed(void* base_address, size_t length, AllocationType allocation_type, PageAccess access) {
    (void)allocation_type;
    (void)access;
    if (base_address == nullptr) {
        // Host-side memory with no fixed address.
        length = (length + 0xFFF) & ~size_t(0xFFF);
        void* memory = aligned_alloc(0x1000, length);
        if (memory) {
            std::memset(memory, 0, length);
        }
        return memory;
    }
    // Inside the arena: only the blocks are backed by RAM; a commit outside them must fail loudly.
    uint8_t* p = static_cast<uint8_t*>(base_address);
    if (arena.base && p >= arena.base && p < arena.base + arena.size) {
        const uint64_t guest = p - arena.base;
        if (in_mapped_block(guest, length)) {
            return base_address;
        }
        std::printf("[nx_memory] commit of guest 0x%llX + 0x%zX is outside the mapped blocks\n",
                    (unsigned long long)guest, length);
        return nullptr;
    }
    return base_address;
}

bool DeallocFixed(void* base_address, size_t length, DeallocationType deallocation_type) {
    (void)length;
    uint8_t* p = static_cast<uint8_t*>(base_address);
    if (arena.base && p >= arena.base && p < arena.base + arena.size) {
        return true;
    }
    if (deallocation_type == DeallocationType::kRelease) {
        free(base_address);
    }
    return true;
}

bool Protect(void* base_address, size_t length, PageAccess access, PageAccess* out_old_access) {
    (void)base_address;
    (void)length;
    (void)access;
    // Horizon refuses permission changes on these mappings (the probe): nothing is ever protected.
    if (out_old_access) {
        *out_old_access = PageAccess::kReadWrite;
    }
    return true;
}

bool QueryProtect(void* base_address, size_t& length, PageAccess& access_out) {
    (void)base_address;
    (void)length;
    access_out = PageAccess::kReadWrite;
    return true;
}

FileMappingHandle CreateFileMappingHandle(const std::filesystem::path& path, size_t length, PageAccess access,
                                          bool commit) {
    (void)path;
    (void)access;
    (void)commit;
    if (arena.base) {
        return 1;
    }
    virtmemLock();
    void* at = virtmemFindAslr(length, 0x10000);
    if (at) {
        arena.reservation = virtmemAddReservation(at, length);
    }
    virtmemUnlock();
    if (!at || !arena.reservation) {
        std::printf("[nx_memory] no address space for the %zu MB arena\n", length >> 20);
        return kFileMappingHandleInvalid;
    }
    arena.base = static_cast<uint8_t*>(at);
    arena.size = length;
    std::printf("[nx_memory] reserved %zu MB at %p\n", length >> 20, at);
    print_heap_state("before the blocks");
    return 1;
}

void CloseFileMappingHandle(FileMappingHandle handle, const std::filesystem::path& path) {
    (void)handle;
    (void)path;
}

void* MapFileView(FileMappingHandle handle, void* base_address, size_t length, PageAccess access,
                  size_t file_offset) {
    (void)handle;
    (void)length;
    (void)access;
    (void)file_offset;
    uint8_t* at = static_cast<uint8_t*>(base_address);
    if (!arena.base || at < arena.base || at >= arena.base + arena.size) {
        return nullptr;
    }
    const uint64_t guest = at - arena.base;
    // Every block inside the requested view (the SDK asks for 0x00000000 + 1 GB, which holds the
    // 0x00000000 and 0x30000000 blocks; 0x40000000 + 1008 MB holds 0x40000000 and 0x70000000).
    bool any = false;
    size_t mapped_bytes = 0;
    for (Block& block : blocks) {
        if (block.guest_start >= guest && block.guest_start + block.size <= guest + length) {
            if (!block.mapped && !map_block(block)) {
                return nullptr;
            }
            any = true;
            mapped_bytes += block.size;
        }
    }
    if (any) {
        if (mapped_bytes < length) {
            std::printf("[nx_memory] view 0x%llX asks for %zu MB, its blocks hold %zu MB: allocations outside them fail\n",
                        (unsigned long long)guest, length >> 20, mapped_bytes >> 20);
        }
        return at;
    }
    // A view with no block of its own: an alias folded onto another block (0x90000000, 0xC0000000,
    // 0xE0000000, the raw physical view). The address is valid for arithmetic only.
    return at;
}

bool UnmapFileView(FileMappingHandle handle, void* base_address, size_t length) {
    (void)handle;
    (void)base_address;
    (void)length;
    return true;
}

}  // namespace rex::memory

// Called first thing from userAppInit (switch_nx.cpp): the RAM behind every block in one piece.
extern "C" void rex_nx_reserve_pool(void) {
    using namespace rex::memory;
    size_t total = 0;
    for (const Block& block : blocks) total += block.size;
    print_heap_state("before the arena pool");
    // Carved from the top of libnx's heap by lowering fake_heap_end (newlib's sbrk stops there):
    // malloc never sees the 1088 MB. Console 0.82: one 1088 MB aligned_alloc succeeded, then
    // every allocation NVK made failed ("out of host memory") while the arena grew by 1.2 GB:
    // newlib's allocator does not cope with a chunk that size.
    const uintptr_t end = reinterpret_cast<uintptr_t>(fake_heap_end) & ~uintptr_t(0x1FFFFF);
    const uintptr_t start = end - total;
    if (fake_heap_end != nullptr && start > reinterpret_cast<uintptr_t>(fake_heap_start) + (256u << 20)) {
        fake_heap_end = reinterpret_cast<char*>(start);
        pool = reinterpret_cast<uint8_t*>(start);
    }
    if (pool == nullptr) {
        std::printf("[nx_memory] no RAM for the arena pool (%zu MB)\n", total >> 20);
        print_heap_state("at the failure");
        std::printf("[nx] fatal: not enough RAM for the guest memory (start with title takeover: hold R on a "
                    "game); exiting\n");
        std::fflush(stdout);
        fsync(fileno(stdout));
        svcExitProcess();
    }
    pool_size = total;
    std::printf("[nx_memory] arena pool: %zu MB at %p (heap end moved down to %p)\n", total >> 20,
                static_cast<void*>(pool), static_cast<void*>(fake_heap_end));
    // Is malloc sane after that? A big and a small request, freed again.
    void* big = std::malloc(256u << 20);
    void* small = std::malloc(1u << 20);
    std::printf("[nx_memory] malloc self-test: 256 MB %s, 1 MB %s\n", big ? "ok" : "FAILED", small ? "ok" : "FAILED");
    std::free(small);
    std::free(big);
    print_heap_state("after the arena pool");
}

// The host address of guest physical memory (the 0xA0000000 block), or null before it is mapped.
extern "C" const uint8_t* rex_nx_physical_base(void) {
    using namespace rex::memory;
    for (const Block& block : blocks) {
        if (block.guest_start == 0xA0000000ull && block.mapped) return arena.base + block.guest_start;
    }
    return nullptr;
}
