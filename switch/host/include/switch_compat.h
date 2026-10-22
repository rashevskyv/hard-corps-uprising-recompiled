/* Forced into every file of the Switch build (CMakeLists.txt, -include): what newlib and libnx lack
   and the SDK's POSIX sources use. Implemented in switch_compat.c. */
#ifndef HCU_SWITCH_COMPAT_H
#define HCU_SWITCH_COMPAT_H

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

time_t timegm(struct tm* tm);

/* threading_posix.cpp: affinity and priorities. Horizon has cores 0-2 for applications (3 is the
   system's); the implementations map onto svcSetThreadCoreMask / svcSetThreadPriority when the
   thread is the caller's, and are no-ops otherwise (switch_compat.c). */
typedef struct {
    unsigned long bits;
} cpu_set_t;
#define CPU_ZERO(set) ((set)->bits = 0UL)
#define CPU_SET(cpu, set) ((set)->bits |= 1UL << (cpu))
#define CPU_CLR(cpu, set) ((set)->bits &= ~(1UL << (cpu)))
#define CPU_ISSET(cpu, set) (((set)->bits >> (cpu)) & 1UL)
#define CPU_COUNT(set) __builtin_popcountl((set)->bits)
#define CPU_SETSIZE 64

int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize, const cpu_set_t* cpuset);
int pthread_getaffinity_np(pthread_t thread, size_t cpusetsize, cpu_set_t* cpuset);
int pthread_getschedparam(pthread_t thread, int* policy, struct sched_param* param);
int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param* param);
int pthread_sigqueue(pthread_t thread, int sig, const union sigval value);
int pthread_attr_setschedpolicy(pthread_attr_t* attr, int policy);

#ifdef __cplusplus
}
#endif

#endif
