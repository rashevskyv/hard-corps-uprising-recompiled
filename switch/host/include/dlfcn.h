/* Horizon has no dynamic libraries. dynlib_posix.cpp (SPIRV-Tools, the Vulkan loader) compiles
   against this and every load fails; the Switch build links everything statically. */
#ifndef HCU_SWITCH_DLFCN_H
#define HCU_SWITCH_DLFCN_H

#include <stddef.h>

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 4
#define RTLD_NOLOAD 8  /* HCU patch 0010: DynamicLibrary::LoadExisting */

static inline void* dlopen(const char* filename, int flags) {
    (void)filename;
    (void)flags;
    return NULL;
}

static inline void* dlsym(void* handle, const char* symbol) {
    (void)handle;
    (void)symbol;
    return NULL;
}

static inline int dlclose(void* handle) {
    (void)handle;
    return 0;
}

static inline const char* dlerror(void) {
    return "no dynamic libraries on the Switch";
}

#endif
