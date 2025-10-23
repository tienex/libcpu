# Windows NT Timing and Profiling Emulation

High-resolution time and profiling operations for Windows NT 3.1 through Windows 11.

## Overview

Complete implementation of POSIX/BSD/Linux timing functions using Windows high-resolution performance counters, GetSystemTimeAsFileTime, and GetProcessTimes/GetThreadTimes APIs.

**File:** `nix-platform-win32-timing.c` (600+ lines)

---

## Implemented Functions

| Function | Status | Windows API | Accuracy |
|----------|--------|-------------|----------|
| clock_getres | ✅ | QueryPerformanceFrequency | Varies by clock |
| clock_gettime | ✅ | Multiple APIs | ~100ns - 15ms |
| clock_settime | ✅ | SetSystemTime | Admin required |
| times | ✅ | GetProcessTimes | 100ns |
| alarm | ✅ | CreateTimerQueueTimer | 1ms |
| ualarm | ✅ | CreateTimerQueueTimer | 1ms |
| nanosleep | ✅ | Sleep | ~1ms |
| clock_nanosleep | ✅ | Sleep + time calc | ~1ms |
| profil | ⚠️ | Stub (not implemented) | N/A |

---

## Supported Clock Types

### CLOCK_REALTIME (0)
**System wall clock time**
- API: GetSystemTimeAsFileTime
- Resolution: ~15ms (NT 3.1-XP), ~1ms (Vista+)
- Can be set with clock_settime (admin privileges)
- Affected by system time changes
- Accuracy: ±15.625ms

### CLOCK_MONOTONIC (1)
**Monotonic time (never decreases)**
- API: QueryPerformanceCounter
- Resolution: Hardware-dependent (~100ns-1µs)
- Cannot be set
- Not affected by system time changes
- Best for measuring intervals

### CLOCK_PROCESS_CPUTIME_ID (2)
**Process CPU time (user + kernel)**
- API: GetProcessTimes
- Resolution: 100ns
- Includes all threads in process
- User time + kernel time

### CLOCK_THREAD_CPUTIME_ID (3)
**Thread CPU time**
- API: GetThreadTimes
- Resolution: 100ns
- Current thread only

### Other Clocks
- CLOCK_MONOTONIC_RAW (4): Same as CLOCK_MONOTONIC
- CLOCK_REALTIME_COARSE (5): Same as CLOCK_REALTIME
- CLOCK_MONOTONIC_COARSE (6): Same as CLOCK_MONOTONIC
- CLOCK_BOOTTIME (7): Same as CLOCK_MONOTONIC

---

## API Reference

### clock_gettime

Get current time of specified clock.

```c
int nix_platform_win32_clock_gettime(clockid_t clk_id, struct timespec *tp);
```

**Example:**
```c
struct timespec ts;

// Wall clock time
clock_gettime(CLOCK_REALTIME, &ts);
printf("Time: %ld.%09ld\n", ts.tv_sec, ts.tv_nsec);

// Monotonic time for measuring intervals
struct timespec start, end;
clock_gettime(CLOCK_MONOTONIC, &start);
do_work();
clock_gettime(CLOCK_MONOTONIC, &end);

uint64_t elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000ULL
                     + (end.tv_nsec - start.tv_nsec);
printf("Elapsed: %llu ns\n", elapsed_ns);
```

---

### clock_settime

Set time of specified clock (CLOCK_REALTIME only).

```c
int nix_platform_win32_clock_settime(clockid_t clk_id, const struct timespec *tp);
```

**Requires:** Administrator privileges

**Example:**
```c
struct timespec ts;
ts.tv_sec = 1704067200;  // 2024-01-01 00:00:00 UTC
ts.tv_nsec = 0;

if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
    perror("clock_settime");  // Usually EPERM if not admin
}
```

---

### clock_getres

Get resolution of specified clock.

```c
int nix_platform_win32_clock_getres(clockid_t clk_id, struct timespec *res);
```

**Example:**
```c
struct timespec res;

clock_getres(CLOCK_REALTIME, &res);
printf("CLOCK_REALTIME resolution: %ld.%09ld s\n", res.tv_sec, res.tv_nsec);

clock_getres(CLOCK_MONOTONIC, &res);
printf("CLOCK_MONOTONIC resolution: %ld.%09ld s\n", res.tv_sec, res.tv_nsec);
```

**Typical values:**
- CLOCK_REALTIME: 15.625ms (0.015625000 s)
- CLOCK_MONOTONIC: 100ns-1µs (0.000000100-0.000001000 s)
- CLOCK_PROCESS/THREAD_CPUTIME_ID: 100ns (0.000000100 s)

---

### times

Get process CPU times.

```c
clock_t nix_platform_win32_times(struct tms *buf);

struct tms {
    clock_t tms_utime;   /* User CPU time */
    clock_t tms_stime;   /* System CPU time */
    clock_t tms_cutime;  /* Children user time (not tracked on Windows) */
    clock_t tms_cstime;  /* Children system time (not tracked on Windows) */
};
```

**Returns:** Elapsed time since boot (ms)

**Example:**
```c
struct tms start, end;
clock_t start_time, end_time;

start_time = times(&start);
do_work();
end_time = times(&end);

printf("User CPU:   %ld ms\n", end.tms_utime - start.tms_utime);
printf("System CPU: %ld ms\n", end.tms_stime - start.tms_stime);
printf("Total CPU:  %ld ms\n",
       (end.tms_utime + end.tms_stime) - (start.tms_utime + start.tms_stime));
printf("Real time:  %ld ms\n", end_time - start_time);
```

**Note:** Children times always 0 on Windows (no easy way to track).

---

### alarm

Schedule SIGALRM signal.

```c
unsigned int nix_platform_win32_alarm(unsigned int seconds);
```

**Returns:** Remaining time from previous alarm, or 0

**Example:**
```c
void alarm_handler(int sig) {
    printf("Alarm!\n");
    exit(0);
}

signal(SIGALRM, alarm_handler);
alarm(5);  // SIGALRM in 5 seconds

while (1) {
    // Do work...
}
```

**Implementation:** Uses CreateTimerQueueTimer for one-shot timer.

---

### ualarm

Schedule SIGALRM signal (microsecond resolution).

```c
unsigned int nix_platform_win32_ualarm(unsigned int usecs, unsigned int interval);
```

**Parameters:**
- `usecs` - Microseconds until first alarm
- `interval` - Microseconds between repeating alarms (0 = one-shot)

**Returns:** Remaining time from previous alarm, or 0

**Example:**
```c
void alarm_handler(int sig) {
    static int count = 0;
    printf("Tick %d\n", ++count);
    if (count >= 10) {
        ualarm(0, 0);  // Cancel alarm
        exit(0);
    }
}

signal(SIGALRM, alarm_handler);
ualarm(1000000, 1000000);  // First after 1s, then every 1s

while (1) {
    pause();  // Wait for signals
}
```

**Note:** Actual resolution ~1ms on Windows.

---

### nanosleep

High-resolution sleep.

```c
int nix_platform_win32_nanosleep(const struct timespec *req, struct timespec *rem);
```

**Example:**
```c
struct timespec ts;

// Sleep for 100ms
ts.tv_sec = 0;
ts.tv_nsec = 100000000;  // 100 million nanoseconds
nanosleep(&ts, NULL);

// Sleep for 1.5 seconds
ts.tv_sec = 1;
ts.tv_nsec = 500000000;  // 500 million nanoseconds
nanosleep(&ts, NULL);
```

**Note:** Windows Sleep() has ~1ms resolution, not true nanosecond precision.

---

### clock_nanosleep

Sleep on specific clock with absolute/relative time.

```c
int nix_platform_win32_clock_nanosleep(
    clockid_t clk_id,
    int flags,
    const struct timespec *request,
    struct timespec *remain
);
```

**Flags:**
- 0: Relative time (sleep for specified duration)
- TIMER_ABSTIME (1): Absolute time (sleep until specified time)

**Example:**
```c
// Relative sleep (same as nanosleep)
struct timespec ts = {1, 0};  // 1 second
clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);

// Absolute sleep (wake at specific time)
struct timespec target;
clock_gettime(CLOCK_REALTIME, &target);
target.tv_sec += 10;  // Wake in 10 seconds
clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &target, NULL);
```

---

### profil

Statistical profiling (stub).

```c
int nix_platform_win32_profil(
    unsigned short *buf,
    size_t bufsiz,
    size_t offset,
    unsigned int scale
);
```

**Status:** Stub implementation (returns success but does nothing)

**Note:** True profiling would require:
- High-frequency timer (QueryPerformanceCounter)
- Thread suspension (SuspendThread)
- Instruction pointer sampling (GetThreadContext)
- Address mapping to buffer

Use Windows Performance Tools (WPT) or profiling APIs instead.

---

## Usage Examples

### Measure Execution Time

```c
void benchmark_function(void) {
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Function to benchmark
    for (int i = 0; i < 1000000; i++) {
        compute_something();
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64_t ns = (end.tv_sec - start.tv_sec) * 1000000000ULL
                + (end.tv_nsec - start.tv_nsec);

    printf("Execution time: %llu µs\n", ns / 1000);
}
```

### Timeout with Absolute Time

```c
int wait_with_timeout(int seconds) {
    struct timespec deadline;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;

    while (1) {
        if (check_condition()) {
            return 0;  // Success
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        if (now.tv_sec >= deadline.tv_sec &&
            now.tv_nsec >= deadline.tv_nsec) {
            return -1;  // Timeout
        }

        // Sleep a bit
        struct timespec sleep_time = {0, 10000000};  // 10ms
        nanosleep(&sleep_time, NULL);
    }
}
```

### CPU Usage Tracking

```c
void monitor_cpu_usage(void) {
    struct tms prev, curr;
    clock_t prev_real, curr_real;

    prev_real = times(&prev);

    for (int i = 0; i < 10; i++) {
        sleep(1);

        curr_real = times(&curr);

        clock_t user_diff = curr.tms_utime - prev.tms_utime;
        clock_t sys_diff = curr.tms_stime - prev.tms_stime;
        clock_t real_diff = curr_real - prev_real;

        if (real_diff > 0) {
            double user_pct = (100.0 * user_diff) / real_diff;
            double sys_pct = (100.0 * sys_diff) / real_diff;

            printf("CPU: %.1f%% user, %.1f%% system\n", user_pct, sys_pct);
        }

        prev = curr;
        prev_real = curr_real;
    }
}
```

---

## Performance Characteristics

### Accuracy

| Clock Type | Resolution | Accuracy | Overhead |
|------------|-----------|----------|----------|
| CLOCK_REALTIME | ~15ms | ±15ms | ~100ns |
| CLOCK_MONOTONIC | ~100ns-1µs | ±1µs | ~100ns |
| CLOCK_PROCESS_CPUTIME_ID | 100ns | ±100ns | ~1µs |
| CLOCK_THREAD_CPUTIME_ID | 100ns | ±100ns | ~500ns |

### Sleep Accuracy

- nanosleep: ~1ms resolution, ±1ms accuracy
- Sleep on NT 3.1-XP: ~15ms resolution
- Sleep on Vista+: ~1ms resolution (with timeBeginPeriod)
- High-resolution timers: Use multimedia timers for <1ms

---

## Limitations

1. **Sleep Resolution**: Windows Sleep() limited to ~1ms, not true nanosecond sleep
2. **CLOCK_REALTIME**: Affected by system time changes (NTP, manual adjustments)
3. **Children Times**: Not tracked (tms_cutime/tms_cstime always 0)
4. **profil**: Stub only (use Windows profiling tools instead)
5. **ualarm Precision**: Actual precision ~1ms, not microsecond
6. **clock_settime**: Requires Administrator privileges

---

## Windows Version Compatibility

| Windows Version | Features | Notes |
|----------------|----------|-------|
| NT 3.1 | All except profil | Basic support |
| NT 4.0 | All | Better timer resolution |
| 2000/XP | All | QueryPerformanceCounter improved |
| Vista+ | All | ~1ms Sleep resolution |
| Win7+ | All | High-resolution timers |
| Win10+ | All | Best timer accuracy |

---

## Integration with Other Modules

```c
// Use with signals (alarm)
signal(SIGALRM, handler);
alarm(30);

// Use with file operations (timeout)
struct timespec deadline;
clock_gettime(CLOCK_REALTIME, &deadline);
deadline.tv_sec += 5;

while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &deadline, NULL) != 0) {
    if (try_lock_file()) break;
}

// Measure I/O performance
clock_gettime(CLOCK_MONOTONIC, &start);
read(fd, buf, size);
clock_gettime(CLOCK_MONOTONIC, &end);
```

---

**Implementation:** `nix-platform-win32-timing.c`
**Header:** `nix-platform-win32.h`
**Documentation:** This file
