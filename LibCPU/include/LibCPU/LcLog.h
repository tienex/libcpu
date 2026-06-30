#ifndef __LibCPU_LcLog_h
#define __LibCPU_LcLog_h

/*
 * LibCPU logging API (CoreFoundation-style LC names, matching the rest of the public C API:
 * LCBackendRef, LCRetain, ...).
 *
 * A small, dependency-free severity-gated logger. It originated as libnix's nix_log (replacing the
 * old xec-compat logging) and was promoted to LibCPU's shared logging API: libcpu and libnix both log
 * through it. Portable C -- usable from the LibCPU framework and from the standalone libnix host layer
 * alike (the implementation lives in core/LcLog.c).
 */

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _LCLogSeverity {
    LCLogFatal,
    LCLogError,
    LCLogWarning,
    LCLogInfo,
    LCLogDebug
} LCLogSeverity;

#define LCLogNone     0
#define LCLogCore     1
#define LCLogDontExit 2
#define LCLogNewline  4
#define LCLogErrExit  8
#define LCLogCleanExit 16

/* Register a logging tag; the returned cookie is passed to LCLog (printed as a prefix). */
void *LCLogRegister (char const *tag);

void  LCLogImpl (void *cookie, int severity, unsigned flags,
                 char const *func, char const *file, unsigned line, char const *fmt, ...);

#ifdef NDEBUG
/* In release builds only a FATAL message is emitted. */
#define LCLog(cookie, sev, flags, ...) \
    ((sev) == LCLogFatal \
        ? LCLogImpl ((cookie), (sev), (flags), __func__, __FILE__, __LINE__, __VA_ARGS__) \
        : (void) 0)
#else
#define LCLog(cookie, sev, flags, ...) \
    LCLogImpl ((cookie), (sev), (flags), __func__, __FILE__, __LINE__, __VA_ARGS__)
#endif

#define LCAssert(cookie, cond) \
    do { if (!(cond)) { LCLog ((cookie), LCLogFatal, LCLogCore, "Assertion failed: %s", #cond); } } while (0)

#define LCAssert0(cond)              LCAssert (NULL, (cond))
#define LCAssert2(cookie, cond, why) LCAssert ((cookie), (cond))

#define LCBugCheck(cookie, id) \
    LCLogImpl ((cookie), LCLogFatal, LCLogCore, __func__, __FILE__, __LINE__, "BUG CHECK %s", #id)

#ifdef __cplusplus
}
#endif

#endif /* !__LibCPU_LcLog_h */
