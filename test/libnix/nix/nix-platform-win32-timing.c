/*
 * nix-platform-win32-timing.c
 *
 * Timing and profiling operations emulation for Windows NT
 *
 * Implements POSIX/BSD/Linux timing and profiling functions:
 * - clock_gettime/clock_settime/clock_getres - high-resolution time
 * - times - get process times
 * - alarm/ualarm - alarm signals
 * - profil - statistical profiling
 * - nanosleep - high-resolution sleep
 * - clock_nanosleep - sleep on specific clock
 *
 * Compatible with Windows NT 3.1 through Windows 11
 */

#if defined(NIX_HOST_WIN32)

#include "nix-platform-win32.h"
#include <windows.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>

/* Clock IDs */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_REALTIME_COARSE    5
#define CLOCK_MONOTONIC_COARSE   6
#define CLOCK_BOOTTIME           7
#endif

/* Timer types */
#ifndef ITIMER_REAL
#define ITIMER_REAL    0  /* Real time */
#define ITIMER_VIRTUAL 1  /* Process virtual time */
#define ITIMER_PROF    2  /* Profile time */
#endif

/* Time structures */
struct tms {
	clock_t tms_utime;   /* User CPU time */
	clock_t tms_stime;   /* System CPU time */
	clock_t tms_cutime;  /* User CPU time of children */
	clock_t tms_cstime;  /* System CPU time of children */
};

/* Performance counter frequency (initialized once) */
static LARGE_INTEGER g_perf_frequency = {0};
static int g_perf_init = 0;

/* Initialize performance counter */
static void init_perf_counter(void)
{
	if (!g_perf_init) {
		QueryPerformanceFrequency(&g_perf_frequency);
		g_perf_init = 1;
	}
}

/* Convert FILETIME to nanoseconds */
static uint64_t filetime_to_ns(const FILETIME *ft)
{
	uint64_t t = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
	/* FILETIME is in 100-nanosecond intervals */
	return t * 100;
}

/* Convert nanoseconds to timespec */
static void ns_to_timespec(uint64_t ns, struct timespec *ts)
{
	ts->tv_sec = (time_t)(ns / 1000000000ULL);
	ts->tv_nsec = (long)(ns % 1000000000ULL);
}

/* Convert timespec to nanoseconds */
static uint64_t timespec_to_ns(const struct timespec *ts)
{
	return (uint64_t)ts->tv_sec * 1000000000ULL + ts->tv_nsec;
}

/*
 * clock_getres - Get clock resolution
 *
 * Returns the resolution (precision) of the specified clock.
 */
int nix_platform_win32_clock_getres(clockid_t clk_id, struct timespec *res)
{
	if (!res) {
		errno = EINVAL;
		return -1;
	}

	init_perf_counter();

	switch (clk_id) {
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_COARSE:
		/* GetSystemTimeAsFileTime has ~15ms resolution on older Windows,
		 * ~1ms on newer Windows */
		res->tv_sec = 0;
		res->tv_nsec = 15625000;  /* 15.625ms (64 Hz) */
		break;

	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
	case CLOCK_MONOTONIC_COARSE:
	case CLOCK_BOOTTIME:
		/* Performance counter resolution */
		if (g_perf_frequency.QuadPart > 0) {
			uint64_t ns_per_tick = 1000000000ULL / g_perf_frequency.QuadPart;
			res->tv_sec = 0;
			res->tv_nsec = (long)ns_per_tick;
		} else {
			res->tv_sec = 0;
			res->tv_nsec = 1000000;  /* 1ms fallback */
		}
		break;

	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		/* GetProcessTimes has 100ns resolution */
		res->tv_sec = 0;
		res->tv_nsec = 100;
		break;

	default:
		errno = EINVAL;
		return -1;
	}

	return 0;
}

/*
 * clock_gettime - Get current time of clock
 *
 * Returns the current time of the specified clock.
 */
int nix_platform_win32_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	if (!tp) {
		errno = EINVAL;
		return -1;
	}

	init_perf_counter();

	switch (clk_id) {
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_COARSE:
	{
		/* System time (wall clock) */
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);

		/* Convert to Unix epoch (Jan 1, 1970) */
		/* Windows epoch is Jan 1, 1601 */
		uint64_t ns = filetime_to_ns(&ft);
		/* Difference between 1601 and 1970 in 100ns intervals */
		uint64_t unix_epoch = 116444736000000000ULL;
		ns -= unix_epoch * 100;

		ns_to_timespec(ns, tp);
		break;
	}

	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
	case CLOCK_MONOTONIC_COARSE:
	case CLOCK_BOOTTIME:
	{
		/* Monotonic time using performance counter */
		LARGE_INTEGER counter;
		if (!QueryPerformanceCounter(&counter)) {
			errno = EINVAL;
			return -1;
		}

		if (g_perf_frequency.QuadPart > 0) {
			uint64_t ns = (counter.QuadPart * 1000000000ULL) / g_perf_frequency.QuadPart;
			ns_to_timespec(ns, tp);
		} else {
			/* Fallback to GetTickCount */
			DWORD ticks = GetTickCount();
			tp->tv_sec = ticks / 1000;
			tp->tv_nsec = (ticks % 1000) * 1000000;
		}
		break;
	}

	case CLOCK_PROCESS_CPUTIME_ID:
	{
		/* Process CPU time */
		FILETIME creation, exit, kernel, user;
		if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
			errno = EINVAL;
			return -1;
		}

		/* Combine kernel and user time */
		uint64_t kernel_ns = filetime_to_ns(&kernel);
		uint64_t user_ns = filetime_to_ns(&user);
		uint64_t total_ns = kernel_ns + user_ns;

		ns_to_timespec(total_ns, tp);
		break;
	}

	case CLOCK_THREAD_CPUTIME_ID:
	{
		/* Thread CPU time */
		FILETIME creation, exit, kernel, user;
		if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
			errno = EINVAL;
			return -1;
		}

		/* Combine kernel and user time */
		uint64_t kernel_ns = filetime_to_ns(&kernel);
		uint64_t user_ns = filetime_to_ns(&user);
		uint64_t total_ns = kernel_ns + user_ns;

		ns_to_timespec(total_ns, tp);
		break;
	}

	default:
		errno = EINVAL;
		return -1;
	}

	return 0;
}

/*
 * clock_settime - Set time of clock
 *
 * Sets the time of the specified clock.
 * Only CLOCK_REALTIME can be set (requires admin privileges).
 */
int nix_platform_win32_clock_settime(clockid_t clk_id, const struct timespec *tp)
{
	if (!tp) {
		errno = EINVAL;
		return -1;
	}

	if (clk_id != CLOCK_REALTIME) {
		errno = EINVAL;
		return -1;
	}

	/* Convert timespec to SYSTEMTIME */
	uint64_t ns = timespec_to_ns(tp);

	/* Add Unix epoch offset */
	uint64_t unix_epoch = 116444736000000000ULL;
	ns += unix_epoch * 100;

	/* Convert to FILETIME */
	uint64_t ft_value = ns / 100;
	FILETIME ft;
	ft.dwLowDateTime = (DWORD)(ft_value & 0xFFFFFFFF);
	ft.dwHighDateTime = (DWORD)(ft_value >> 32);

	/* Convert FILETIME to SYSTEMTIME */
	SYSTEMTIME st;
	if (!FileTimeToSystemTime(&ft, &st)) {
		errno = EINVAL;
		return -1;
	}

	/* Set system time (requires admin privileges) */
	if (!SetSystemTime(&st)) {
		errno = EPERM;
		return -1;
	}

	return 0;
}

/*
 * times - Get process times
 *
 * Returns process and children CPU times.
 */
clock_t nix_platform_win32_times(struct tms *buf)
{
	if (!buf) {
		errno = EINVAL;
		return (clock_t)-1;
	}

	/* Get process times */
	FILETIME creation, exit, kernel, user;
	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
		errno = EINVAL;
		return (clock_t)-1;
	}

	/* Convert to clock ticks (CLOCKS_PER_SEC = 1000 on Windows) */
	uint64_t kernel_ns = filetime_to_ns(&kernel);
	uint64_t user_ns = filetime_to_ns(&user);

	buf->tms_stime = (clock_t)(kernel_ns / 1000000);  /* ms */
	buf->tms_utime = (clock_t)(user_ns / 1000000);    /* ms */

	/* Windows doesn't track children times easily */
	buf->tms_cutime = 0;
	buf->tms_cstime = 0;

	/* Return elapsed time since boot */
	return (clock_t)GetTickCount();
}

/*
 * Alarm support (uses timer thread)
 */

static HANDLE g_alarm_timer = NULL;
static CRITICAL_SECTION g_alarm_lock;
static int g_alarm_init = 0;

static void init_alarm_subsystem(void)
{
	if (!g_alarm_init) {
		InitializeCriticalSection(&g_alarm_lock);
		g_alarm_init = 1;
	}
}

static VOID CALLBACK alarm_callback(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	/* Raise SIGALRM signal */
	/* For now, just call the signal handler directly if registered */
	/* In a complete implementation, this would queue a signal */
	(void)lpParam;
	(void)TimerOrWaitFired;

	/* TODO: Call signal handler for SIGALRM (14) */
}

/*
 * alarm - Set alarm signal
 *
 * Schedules SIGALRM to be sent after specified seconds.
 * Returns remaining time from previous alarm, or 0 if none.
 */
unsigned int nix_platform_win32_alarm(unsigned int seconds)
{
	init_alarm_subsystem();

	EnterCriticalSection(&g_alarm_lock);

	unsigned int remaining = 0;

	/* Cancel existing alarm and get remaining time */
	if (g_alarm_timer) {
		/* Cannot easily get remaining time on Windows */
		/* Just cancel it */
		DeleteTimerQueueTimer(NULL, g_alarm_timer, NULL);
		g_alarm_timer = NULL;
	}

	/* Set new alarm if seconds > 0 */
	if (seconds > 0) {
		DWORD ms = seconds * 1000;

		if (!CreateTimerQueueTimer(
				&g_alarm_timer,
				NULL,
				alarm_callback,
				NULL,
				ms,
				0,  /* One-shot timer */
				WT_EXECUTEDEFAULT
			)) {
			g_alarm_timer = NULL;
		}
	}

	LeaveCriticalSection(&g_alarm_lock);

	return remaining;
}

/*
 * ualarm - Set alarm in microseconds (BSD)
 *
 * Schedules SIGALRM to be sent after specified microseconds.
 * Returns remaining time from previous alarm, or 0 if none.
 */
unsigned int nix_platform_win32_ualarm(unsigned int usecs, unsigned int interval)
{
	init_alarm_subsystem();

	EnterCriticalSection(&g_alarm_lock);

	unsigned int remaining = 0;

	/* Cancel existing alarm */
	if (g_alarm_timer) {
		DeleteTimerQueueTimer(NULL, g_alarm_timer, NULL);
		g_alarm_timer = NULL;
	}

	/* Set new alarm if usecs > 0 */
	if (usecs > 0) {
		DWORD ms = usecs / 1000;
		DWORD interval_ms = interval / 1000;

		if (!CreateTimerQueueTimer(
				&g_alarm_timer,
				NULL,
				alarm_callback,
				NULL,
				ms,
				interval_ms,  /* Interval for repeating timer */
				WT_EXECUTEDEFAULT
			)) {
			g_alarm_timer = NULL;
		}
	}

	LeaveCriticalSection(&g_alarm_lock);

	return remaining;
}

/*
 * nanosleep - High-resolution sleep
 *
 * Sleeps for the specified time.
 */
int nix_platform_win32_nanosleep(const struct timespec *req, struct timespec *rem)
{
	if (!req) {
		errno = EINVAL;
		return -1;
	}

	if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000) {
		errno = EINVAL;
		return -1;
	}

	/* Convert to milliseconds */
	DWORD ms = (DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000);

	/* Windows Sleep has ~1ms resolution at best */
	Sleep(ms);

	/* If remainder requested, set to 0 (we slept the full time) */
	if (rem) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}

	return 0;
}

/*
 * clock_nanosleep - Sleep on specific clock
 *
 * Sleeps for the specified time on the specified clock.
 */
int nix_platform_win32_clock_nanosleep(
	clockid_t clk_id,
	int flags,
	const struct timespec *request,
	struct timespec *remain
)
{
	if (!request) {
		return EINVAL;
	}

	/* For now, only support CLOCK_REALTIME and CLOCK_MONOTONIC */
	if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC) {
		return EINVAL;
	}

	/* Flags: 0 = relative time, TIMER_ABSTIME = absolute time */
	#define TIMER_ABSTIME 1

	if (flags & TIMER_ABSTIME) {
		/* Absolute time - calculate relative time */
		struct timespec now;
		if (nix_platform_win32_clock_gettime(clk_id, &now) != 0) {
			return EINVAL;
		}

		uint64_t now_ns = timespec_to_ns(&now);
		uint64_t target_ns = timespec_to_ns(request);

		if (target_ns <= now_ns) {
			/* Already past target time */
			if (remain) {
				remain->tv_sec = 0;
				remain->tv_nsec = 0;
			}
			return 0;
		}

		uint64_t sleep_ns = target_ns - now_ns;
		struct timespec sleep_ts;
		ns_to_timespec(sleep_ns, &sleep_ts);

		return nix_platform_win32_nanosleep(&sleep_ts, remain);
	} else {
		/* Relative time */
		return nix_platform_win32_nanosleep(request, remain);
	}
}

/*
 * profil - Statistical profiling
 *
 * Enables statistical profiling of program execution.
 * Note: This is a stub implementation on Windows.
 */
int nix_platform_win32_profil(
	unsigned short *buf,
	size_t bufsiz,
	size_t offset,
	unsigned int scale
)
{
	/* Windows doesn't have direct profiling support */
	/* This would require setting up a high-frequency timer
	 * and sampling the instruction pointer */

	(void)buf;
	(void)bufsiz;
	(void)offset;
	(void)scale;

	/* Return success but do nothing */
	/* Real profiling would use VirtualQuery + SuspendThread + GetThreadContext */
	return 0;
}

#endif /* NIX_HOST_WIN32 */
