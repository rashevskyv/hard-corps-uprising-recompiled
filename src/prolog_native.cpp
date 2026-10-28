#include <cstddef>
#include <cstdint>

#include <rex/hook.h>
#include <rex/system/xmemory.h>

// The compiler's prologue/epilogue helpers (__savegprlr_N / __restgprlr_N: r_N..r31 and the link register
// through r1). The generated versions run the address fold and the dirty-page test for every register, and
// they were 16% of the main thread's samples. Below 0x7F000000 the fold is zero and nothing is tracked, so
// one pointer serves the whole frame; any other stack takes the generated path.
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(PPCContext, r31) - offsetof(PPCContext, r14) == 17 * sizeof(PPCRegister));

namespace {

inline uint8_t* StackFrame(uint8_t* base, const PPCContext& ctx) {
  const uint32_t sp = ctx.r1.u32;
  return (sp >= 0x1000u && sp < 0x7F000000u) ? base + sp : nullptr;
}

template <int N>
inline bool Save(PPCContext& ctx, uint8_t* base) {
  uint8_t* sp = StackFrame(base, ctx);
  if (!sp) return false;
  const PPCRegister* r = &ctx.r14;
  for (int i = N; i <= 31; ++i) {
    const uint64_t v = __builtin_bswap64(r[i - 14].u64);
    __builtin_memcpy(sp - 0x10 - 8 * (31 - i), &v, 8);
  }
  const uint32_t lr = __builtin_bswap32(ctx.r12.u32);
  __builtin_memcpy(sp - 8, &lr, 4);
  return true;
}

template <int N>
inline bool Restore(PPCContext& ctx, uint8_t* base) {
  uint8_t* sp = StackFrame(base, ctx);
  if (!sp) return false;
  PPCRegister* r = &ctx.r14;
  for (int i = N; i <= 31; ++i) {
    uint64_t v;
    __builtin_memcpy(&v, sp - 0x10 - 8 * (31 - i), 8);
    r[i - 14].u64 = __builtin_bswap64(v);
  }
  uint32_t lr;
  __builtin_memcpy(&lr, sp - 8, 4);
  ctx.r12.u64 = ctx.lr = __builtin_bswap32(lr);
  return true;
}

}  // namespace

#define HCU_PROLOG(N)                                           \
  REX_EXTERN(__imp____savegprlr_##N);                           \
  REX_HOOK_RAW(__savegprlr_##N) {                               \
    if (!Save<N>(ctx, base)) __imp____savegprlr_##N(ctx, base); \
  }                                                             \
  REX_EXTERN(__imp____restgprlr_##N);                           \
  REX_HOOK_RAW(__restgprlr_##N) {                               \
    if (!Restore<N>(ctx, base)) __imp____restgprlr_##N(ctx, base); \
  }

HCU_PROLOG(14) HCU_PROLOG(15) HCU_PROLOG(16) HCU_PROLOG(17) HCU_PROLOG(18) HCU_PROLOG(19)
HCU_PROLOG(20) HCU_PROLOG(21) HCU_PROLOG(22) HCU_PROLOG(23) HCU_PROLOG(24) HCU_PROLOG(25)
HCU_PROLOG(26) HCU_PROLOG(27) HCU_PROLOG(28) HCU_PROLOG(29) HCU_PROLOG(30) HCU_PROLOG(31)
