// rex::platform SEH support for Horizon (replaces seh_posix.cpp, which uses signals). Guest
// __try/__except scopes never see a host fault here: nothing can be reprotected and faults
// end in __libnx_exception_handler.
#include <cstdint>

namespace rex::platform {

static thread_local bool tls_seh_active = false;

bool& seh_active() { return tls_seh_active; }

void seh_initialize() {}

int seh_filter(uint32_t code, void* ep) {
  (void)code;
  (void)ep;
  return 0;
}

}  // namespace rex::platform
