/* What newlib and libnx lack (switch_compat.h). */

#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <switch.h>

#include "include/switch_compat.h"

/* Horizon has no time zones: the C library's local time is UTC, so mktime is timegm. */
time_t timegm(struct tm* tm) {
    return mktime(tm);
}

/* newlib declares it but doesn't provide it. Weak: mesa-switch's NVK brings its own. */
__attribute__((weak)) int posix_memalign(void** memptr, size_t alignment, size_t size) {
    void* memory = memalign(alignment, size);
    if (memory == NULL) {
        return ENOMEM;
    }
    *memptr = memory;
    return 0;
}

/* pthread_detach calls this when it exists and fails with ENOSYS otherwise, which makes
   std::thread::detach() throw. The thread is simply never joined (the N64 port does the same). */
struct __pthread_t;
int __syscall_thread_detach(struct __pthread_t* thread) {
    (void)thread;
    return 0;
}

/* Priorities and affinity: only for the calling thread for now (libnx exposes the handle of the
   current thread only; others keep libnx's defaults). The SDK's priorities are SCHED_FIFO 1..99;
   Horizon's are 0 (highest) .. 63, applications use 0x1C-0x3F. ponytail: a thread registry keyed
   by pthread_t if the game's priorities matter on hardware. */
static int to_horizon_priority(int sched_priority) {
    int p = 0x2C - sched_priority / 4;
    if (p < 0x1C) p = 0x1C;
    if (p > 0x3B) p = 0x3B;
    return p;
}

int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize, const cpu_set_t* cpuset) {
    (void)cpusetsize;
    if (thread != pthread_self() || cpuset->bits == 0) {
        return 0;
    }
    /* Never core 3: the system's. */
    u64 mask = cpuset->bits & 0x7;
    if (mask == 0) {
        return 0;
    }
    int preferred = __builtin_ctzl(mask);
    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, preferred, mask);
    return R_FAILED(rc) ? EINVAL : 0;
}

int pthread_getaffinity_np(pthread_t thread, size_t cpusetsize, cpu_set_t* cpuset) {
    (void)thread;
    (void)cpusetsize;
    cpuset->bits = 0x7;
    return 0;
}

int pthread_getschedparam(pthread_t thread, int* policy, struct sched_param* param) {
    (void)thread;
    if (policy) *policy = SCHED_FIFO;
    if (param) param->sched_priority = 0;
    return 0;
}

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param* param) {
    (void)policy;
    if (thread != pthread_self() || !param) {
        return 0;
    }
    Result rc = svcSetThreadPriority(CUR_THREAD_HANDLE, to_horizon_priority(param->sched_priority));
    return R_FAILED(rc) ? EINVAL : 0;
}

int pthread_sigqueue(pthread_t thread, int sig, const union sigval value) {
    (void)thread;
    (void)sig;
    (void)value;
    return ENOSYS;
}

int pthread_attr_setschedpolicy(pthread_attr_t* attr, int policy) {
    (void)attr;
    (void)policy;
    return 0;
}

/* libc calls the SDK's POSIX sources make and libnx lacks. */
#include <fcntl.h>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

void rex_nx_thread_name(void* pthread, const char* name);  /* switch_nx.cpp: the CPU profile line */

int pthread_setname_np(pthread_t thread, const char* name) {
    rex_nx_thread_name(thread, name);
    return 0;
}

int pthread_kill(pthread_t thread, int sig) {
    (void)thread;
    (void)sig;
    return ENOSYS;
}

pid_t waitpid(pid_t pid, int* status, int options) {
    (void)pid;
    (void)status;
    (void)options;
    errno = ECHILD;
    return -1;
}

int execvp(const char* file, char* const argv[]) {
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    off_t here = lseek(fd, 0, SEEK_CUR);
    if (here < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = read(fd, buf, count);
    lseek(fd, here, SEEK_SET);
    return n;
}

ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
    off_t here = lseek(fd, 0, SEEK_CUR);
    if (here < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = write(fd, buf, count);
    lseek(fd, here, SEEK_SET);
    return n;
}

uid_t getuid(void) {
    return 0;
}

int getpwuid_r(uid_t uid, struct passwd* pwd, char* buf, size_t buflen, struct passwd** result) {
    (void)uid;
    (void)pwd;
    (void)buf;
    (void)buflen;
    *result = NULL;
    return ENOENT;
}

int creat(const char* path, mode_t mode) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}
