// TSan test-harness shim (LD_PRELOAD, TSan runs only). Not linked into any app/test binary.
//
// libnvcuvid (loaded by NVENC) resolves pthread_cond_* via dlvsym(..., "GLIBC_2.2.5"), i.e. the
// legacy compat versions. TSan does not intercept those; glibc's compat wrapper calls calloc()
// while TSan considers the thread inside a blocking call -> SEGV in the TSan allocator before any
// application code is reached. This shim exports dlvsym@GLIBC_2.2.5 (the version libnvcuvid binds
// to) and returns the current pthread_cond_* (i.e. the TSan interceptors) for those names.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>

typedef void* (*dlvsym_t)(void*, const char*, const char*);
static dlvsym_t real_dlvsym; // libc's dlvsym, found via dlsym(RTLD_NEXT) (dlsym is not overridden)

static int eq(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }

static void* override(const char* n)
{
    if (eq(n, "pthread_cond_init")) return (void*)&pthread_cond_init;
    if (eq(n, "pthread_cond_destroy")) return (void*)&pthread_cond_destroy;
    if (eq(n, "pthread_cond_wait")) return (void*)&pthread_cond_wait;
    if (eq(n, "pthread_cond_timedwait")) return (void*)&pthread_cond_timedwait;
    if (eq(n, "pthread_cond_signal")) return (void*)&pthread_cond_signal;
    if (eq(n, "pthread_cond_broadcast")) return (void*)&pthread_cond_broadcast;
    return 0;
}

void* shim_dlvsym(void* h, const char* n, const char* v)
{
    if (!real_dlvsym) real_dlvsym = (dlvsym_t)dlsym(RTLD_NEXT, "dlvsym");
    void* o = n ? override(n) : 0;
    return o ? o : real_dlvsym(h, n, v);
}
__asm__(".symver shim_dlvsym, dlvsym@@GLIBC_2.2.5");

// ---------------------------------------------------------------------------------------------
// Qt 5 synchronisation model for TSan (Qt itself is not TSan-instrumented).
// QMutexLocker / QBasicMutex fast paths are inline (instrumented atomics on the mutex's d_ptr,
// the mutex object's address). The contended paths and QWaitCondition::wait() live in
// libQt5Core and are invisible to TSan, producing false races between accesses that ARE locked.
// These wrappers add acquire/release edges on the mutex address around those out-of-line calls
// so that only genuinely unlocked accesses are reported.
extern void __tsan_acquire(void* addr);
extern void __tsan_release(void* addr);

#define REAL(sym) ((real_##sym) ? (real_##sym) : (real_##sym = (__typeof__(real_##sym))dlsym(RTLD_NEXT, #sym)))

typedef struct { long long a, b; } QDeadlineTimerPOD; // 16 bytes, passed in two INTEGER regs

static void (*real__ZN11QBasicMutex12lockInternalEv)(void*);
static _Bool (*real__ZN11QBasicMutex12lockInternalEi)(void*, int);
static void (*real__ZN11QBasicMutex14unlockInternalEv)(void*);
static void (*real__ZN6QMutex4lockEv)(void*);
static _Bool (*real__ZN6QMutex7tryLockEi)(void*, int);
static void (*real__ZN6QMutex6unlockEv)(void*);
static _Bool (*real__ZN14QWaitCondition4waitEP6QMutexm)(void*, void*, unsigned long);
static _Bool (*real__ZN14QWaitCondition4waitEP6QMutex14QDeadlineTimer)(void*, void*, QDeadlineTimerPOD);

void _ZN11QBasicMutex12lockInternalEv(void* m) { REAL(_ZN11QBasicMutex12lockInternalEv)(m); __tsan_acquire(m); }
_Bool _ZN11QBasicMutex12lockInternalEi(void* m, int t) { _Bool r = REAL(_ZN11QBasicMutex12lockInternalEi)(m, t); if (r) __tsan_acquire(m); return r; }
void _ZN11QBasicMutex14unlockInternalEv(void* m) { __tsan_release(m); REAL(_ZN11QBasicMutex14unlockInternalEv)(m); }
void _ZN6QMutex4lockEv(void* m) { REAL(_ZN6QMutex4lockEv)(m); __tsan_acquire(m); }
_Bool _ZN6QMutex7tryLockEi(void* m, int t) { _Bool r = REAL(_ZN6QMutex7tryLockEi)(m, t); if (r) __tsan_acquire(m); return r; }
void _ZN6QMutex6unlockEv(void* m) { __tsan_release(m); REAL(_ZN6QMutex6unlockEv)(m); }
_Bool _ZN14QWaitCondition4waitEP6QMutexm(void* c, void* m, unsigned long t)
{
    __tsan_release(m);
    _Bool r = REAL(_ZN14QWaitCondition4waitEP6QMutexm)(c, m, t);
    __tsan_acquire(m);
    return r;
}
_Bool _ZN14QWaitCondition4waitEP6QMutex14QDeadlineTimer(void* c, void* m, QDeadlineTimerPOD d)
{
    __tsan_release(m);
    _Bool r = REAL(_ZN14QWaitCondition4waitEP6QMutex14QDeadlineTimer)(c, m, d);
    __tsan_acquire(m);
    return r;
}
