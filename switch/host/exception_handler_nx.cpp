// rex::arch::ExceptionHandler for Horizon (replaces exception_handler_posix.cpp). The only user is
// the GPU SharedMemory's write watches, which need page reprotection Horizon refuses here
// (CLAUDE.md "Horizon memory probe"): the Switch build tracks writes in software instead, so no
// handler is installed. Crashes are reported by __libnx_exception_handler (switch_nx.cpp).
#include <rex/exception_handler.h>

namespace rex::arch {

void ExceptionHandler::Install(Handler fn, void* data) {
    (void)fn;
    (void)data;
}

void ExceptionHandler::Uninstall(Handler fn, void* data) {
    (void)fn;
    (void)data;
}

}  // namespace rex::arch
