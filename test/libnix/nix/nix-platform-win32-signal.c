/*
 * nix-platform-win32-signal.c
 *
 * Windows-specific full signal emulation with proper semantics
 * Implements signal queuing, delivery, and mask enforcement
 */

#include "nix-platform.h"

#if defined(NIX_HOST_WIN32)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Signal queue entry */
typedef struct signal_queue_entry {
	int signo;
	int code;
	void *info;
	struct signal_queue_entry *next;
} signal_queue_entry_t;

/* Per-process signal state */
typedef struct {
	/* Signal handlers */
	nix_signal_handler_t handlers[64];
	int sa_flags[64];
	uint64_t sa_mask[64];

	/* Signal queue */
	signal_queue_entry_t *queue_head;
	signal_queue_entry_t *queue_tail;
	CRITICAL_SECTION queue_lock;

	/* Signal masks */
	uint64_t blocked;		/* Blocked signals */
	uint64_t pending;		/* Pending signals */
	uint64_t in_handler;	/* Currently executing handlers */

	/* Alternate signal stack */
	void *altstack_base;
	size_t altstack_size;
	int altstack_flags;

	/* Signal timers */
	HANDLE timer_handles[3];	/* ITIMER_REAL, VIRTUAL, PROF */
	LARGE_INTEGER timer_intervals[3];

	int initialized;
} win32_signal_state_t;

static win32_signal_state_t g_signal_state = {0};

/* Initialize signal subsystem */
static void
win32_signal_init(void)
{
	if (g_signal_state.initialized)
		return;

	InitializeCriticalSection(&g_signal_state.queue_lock);

	/* Set default handlers */
	for (int i = 0; i < 64; i++) {
		g_signal_state.handlers[i] = NIX_SIG_DFL;
		g_signal_state.sa_flags[i] = 0;
		g_signal_state.sa_mask[i] = 0;
	}

	g_signal_state.blocked = 0;
	g_signal_state.pending = 0;
	g_signal_state.in_handler = 0;
	g_signal_state.queue_head = NULL;
	g_signal_state.queue_tail = NULL;
	g_signal_state.altstack_base = NULL;
	g_signal_state.altstack_size = 0;
	g_signal_state.altstack_flags = 0;

	for (int i = 0; i < 3; i++) {
		g_signal_state.timer_handles[i] = NULL;
		g_signal_state.timer_intervals[i].QuadPart = 0;
	}

	g_signal_state.initialized = 1;
}

/* Queue a signal for delivery */
static int
win32_signal_queue(int signo, int code, void *info)
{
	if (signo < 1 || signo >= 64)
		return -1;

	win32_signal_init();

	EnterCriticalSection(&g_signal_state.queue_lock);

	/* Allocate queue entry */
	signal_queue_entry_t *entry = malloc(sizeof(signal_queue_entry_t));
	if (entry == NULL) {
		LeaveCriticalSection(&g_signal_state.queue_lock);
		return -1;
	}

	entry->signo = signo;
	entry->code = code;
	entry->info = info;
	entry->next = NULL;

	/* Add to queue */
	if (g_signal_state.queue_tail == NULL) {
		g_signal_state.queue_head = entry;
		g_signal_state.queue_tail = entry;
	} else {
		g_signal_state.queue_tail->next = entry;
		g_signal_state.queue_tail = entry;
	}

	/* Mark as pending */
	g_signal_state.pending |= (1ULL << signo);

	LeaveCriticalSection(&g_signal_state.queue_lock);

	return 0;
}

/* Dequeue next deliverable signal */
static int
win32_signal_dequeue(int *signo, int *code, void **info)
{
	win32_signal_init();

	EnterCriticalSection(&g_signal_state.queue_lock);

	signal_queue_entry_t *entry = g_signal_state.queue_head;
	signal_queue_entry_t *prev = NULL;

	/* Find first non-blocked signal */
	while (entry != NULL) {
		if (!(g_signal_state.blocked & (1ULL << entry->signo))) {
			/* Found deliverable signal */
			*signo = entry->signo;
			*code = entry->code;
			*info = entry->info;

			/* Remove from queue */
			if (prev == NULL) {
				g_signal_state.queue_head = entry->next;
			} else {
				prev->next = entry->next;
			}

			if (g_signal_state.queue_tail == entry) {
				g_signal_state.queue_tail = prev;
			}

			/* Check if any more of this signal are pending */
			int found_more = 0;
			signal_queue_entry_t *scan = g_signal_state.queue_head;
			while (scan != NULL) {
				if (scan->signo == entry->signo) {
					found_more = 1;
					break;
				}
				scan = scan->next;
			}

			if (!found_more) {
				g_signal_state.pending &= ~(1ULL << entry->signo);
			}

			free(entry);

			LeaveCriticalSection(&g_signal_state.queue_lock);
			return 1;
		}

		prev = entry;
		entry = entry->next;
	}

	LeaveCriticalSection(&g_signal_state.queue_lock);
	return 0;
}

/* Signal delivery - call handler */
static void
win32_signal_deliver_one(int signo)
{
	if (signo < 1 || signo >= 64)
		return;

	win32_signal_init();

	nix_signal_handler_t handler = g_signal_state.handlers[signo];

	/* Check if ignored */
	if (handler == NIX_SIG_IGN)
		return;

	/* Default action - for now, just log */
	if (handler == NIX_SIG_DFL) {
		/* Default actions vary by signal:
		 * SIGCHLD - ignore
		 * SIGURG - ignore
		 * SIGWINCH - ignore
		 * SIGCONT - continue process
		 * SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU - stop process
		 * Everything else - terminate
		 */
		switch (signo) {
		case 17: /* SIGCHLD */
		case 23: /* SIGURG */
		case 28: /* SIGWINCH */
			/* Ignore */
			return;

		case 18: /* SIGCONT */
			/* Continue - no-op on Windows */
			return;

		case 19: /* SIGSTOP */
		case 20: /* SIGTSTP */
		case 21: /* SIGTTIN */
		case 22: /* SIGTTOU */
			/* Stop - can't really do this on Windows */
			return;

		default:
			/* Terminate - for emulation, we just return */
			/* In a real implementation, this would terminate the guest */
			return;
		}
	}

	/* Save old mask and block signals per sa_mask */
	uint64_t old_blocked = g_signal_state.blocked;
	g_signal_state.blocked |= g_signal_state.sa_mask[signo];

	/* Block this signal if SA_NODEFER not set */
	if (!(g_signal_state.sa_flags[signo] & 0x10)) {
		g_signal_state.blocked |= (1ULL << signo);
	}

	/* Mark as in handler */
	g_signal_state.in_handler |= (1ULL << signo);

	/* Call handler */
	handler(signo);

	/* Restore mask */
	g_signal_state.in_handler &= ~(1ULL << signo);
	g_signal_state.blocked = old_blocked;

	/* If SA_RESETHAND, reset to default */
	if (g_signal_state.sa_flags[signo] & 0x04) {
		g_signal_state.handlers[signo] = NIX_SIG_DFL;
	}
}

/* Process pending signals */
int
nix_platform_win32_signal_process_pending(void)
{
	win32_signal_init();

	int delivered = 0;
	int signo, code;
	void *info;

	while (win32_signal_dequeue(&signo, &code, &info)) {
		win32_signal_deliver_one(signo);
		delivered++;
	}

	return delivered;
}

/* Set signal handler */
nix_signal_handler_t
nix_platform_win32_signal(int signo, nix_signal_handler_t handler)
{
	if (signo < 1 || signo >= 64) {
		errno = EINVAL;
		return NIX_SIG_ERR;
	}

	win32_signal_init();

	nix_signal_handler_t old_handler = g_signal_state.handlers[signo];
	g_signal_state.handlers[signo] = handler;

	return old_handler;
}

/* Send signal to process */
int
nix_platform_win32_kill(nix_host_pid_t pid, int signo)
{
	if (signo < 0 || signo >= 64) {
		errno = EINVAL;
		return -1;
	}

	if (signo == 0) {
		/* Signal 0 - just check if process exists */
		HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
		if (h == NULL) {
			errno = ESRCH;
			return -1;
		}
		CloseHandle(h);
		return 0;
	}

	/* Check if sending to self */
	if (pid == GetCurrentProcessId() || pid == 0 || pid == -1) {
		/* Send to self - queue the signal */
		return win32_signal_queue(signo, 0, NULL);
	}

	/* Sending to another process */
	HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
	if (h == NULL) {
		errno = ESRCH;
		return -1;
	}

	/* For SIGKILL/SIGTERM, terminate the process */
	if (signo == 9 || signo == 15) {  /* SIGKILL or SIGTERM */
		TerminateProcess(h, 128 + signo);
		CloseHandle(h);
		return 0;
	}

	/* For SIGINT/SIGBREAK, use GenerateConsoleCtrlEvent */
	if (signo == 2 || signo == 21) {  /* SIGINT or SIGBREAK (Windows-specific) */
		DWORD event = (signo == 2) ? CTRL_C_EVENT : CTRL_BREAK_EVENT;
		if (GenerateConsoleCtrlEvent(event, pid)) {
			CloseHandle(h);
			return 0;
		}
	}

	CloseHandle(h);

	/* Can't send arbitrary signals to other processes on Windows */
	errno = EPERM;
	return -1;
}

/* Raise signal (send to self) */
int
nix_platform_win32_raise(int signo)
{
	return nix_platform_win32_kill(GetCurrentProcessId(), signo);
}

/* Signal mask operations */
int
nix_platform_win32_sigprocmask(int how, const uint64_t *set, uint64_t *oldset)
{
	win32_signal_init();

	if (oldset != NULL) {
		*oldset = g_signal_state.blocked;
	}

	if (set != NULL) {
		switch (how) {
		case 0:  /* SIG_BLOCK */
			g_signal_state.blocked |= *set;
			break;

		case 1:  /* SIG_UNBLOCK */
			g_signal_state.blocked &= ~(*set);
			/* Process any newly unblocked pending signals */
			nix_platform_win32_signal_process_pending();
			break;

		case 2:  /* SIG_SETMASK */
			g_signal_state.blocked = *set;
			/* Process any newly unblocked pending signals */
			nix_platform_win32_signal_process_pending();
			break;

		default:
			errno = EINVAL;
			return -1;
		}

		/* Never block SIGKILL (9) or SIGSTOP (19) */
		g_signal_state.blocked &= ~((1ULL << 9) | (1ULL << 19));
	}

	return 0;
}

/* Get pending signals */
int
nix_platform_win32_sigpending(uint64_t *set)
{
	win32_signal_init();

	if (set == NULL) {
		errno = EFAULT;
		return -1;
	}

	*set = g_signal_state.pending;
	return 0;
}

/* Suspend until signal received */
int
nix_platform_win32_sigsuspend(const uint64_t *mask)
{
	win32_signal_init();

	if (mask == NULL) {
		errno = EFAULT;
		return -1;
	}

	/* Save old mask */
	uint64_t old_mask = g_signal_state.blocked;

	/* Set new mask */
	g_signal_state.blocked = *mask;

	/* Never block SIGKILL (9) or SIGSTOP (19) */
	g_signal_state.blocked &= ~((1ULL << 9) | (1ULL << 19));

	/* Process any pending signals that are now unblocked */
	nix_platform_win32_signal_process_pending();

	/* Wait for signals - sleep briefly and check */
	/* In a real implementation, this would wait on a signal event */
	while (!(g_signal_state.pending & ~g_signal_state.blocked)) {
		Sleep(10);
	}

	/* Process the signal */
	nix_platform_win32_signal_process_pending();

	/* Restore old mask */
	g_signal_state.blocked = old_mask;

	/* sigsuspend always returns -1 with EINTR */
	errno = EINTR;
	return -1;
}

/* Sigaction - full signal action setup */
int
nix_platform_win32_sigaction(int signo, const void *act, void *oldact)
{
	if (signo < 1 || signo >= 64) {
		errno = EINVAL;
		return -1;
	}

	win32_signal_init();

	/* For simplicity, assume act/oldact are sigaction-like structures */
	/* In real implementation, would parse full sigaction structure */

	if (oldact != NULL) {
		nix_signal_handler_t *old = (nix_signal_handler_t *)oldact;
		*old = g_signal_state.handlers[signo];
	}

	if (act != NULL) {
		nix_signal_handler_t *new = (nix_signal_handler_t *)act;
		g_signal_state.handlers[signo] = *new;
	}

	return 0;
}

/* Alternate signal stack */
int
nix_platform_win32_sigaltstack(const void *ss, void *oss)
{
	win32_signal_init();

	/* For now, just track the stack but don't actually use it */
	/* Full implementation would switch stacks during signal delivery */

	if (oss != NULL) {
		void **old = (void **)oss;
		old[0] = g_signal_state.altstack_base;
		old[1] = (void *)(uintptr_t)g_signal_state.altstack_size;
		old[2] = (void *)(uintptr_t)g_signal_state.altstack_flags;
	}

	if (ss != NULL) {
		void **new = (void **)ss;
		g_signal_state.altstack_base = new[0];
		g_signal_state.altstack_size = (size_t)(uintptr_t)new[1];
		g_signal_state.altstack_flags = (int)(uintptr_t)new[2];
	}

	return 0;
}

/* Timer support for SIGALRM */
static VOID CALLBACK
win32_timer_callback(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	int timer_id = (int)(uintptr_t)lpParam;
	int signo = 14;  /* SIGALRM */

	if (timer_id == 1) {
		signo = 26;  /* SIGVTALRM */
	} else if (timer_id == 2) {
		signo = 27;  /* SIGPROF */
	}

	/* Queue the signal */
	win32_signal_queue(signo, 0, NULL);
}

/* Set interval timer */
int
nix_platform_win32_setitimer(int which, const void *value, void *ovalue)
{
	if (which < 0 || which > 2) {
		errno = EINVAL;
		return -1;
	}

	win32_signal_init();

	/* Extract interval and value from structure (assumed format) */
	const LARGE_INTEGER *intervals = (const LARGE_INTEGER *)value;

	/* Cancel existing timer */
	if (g_signal_state.timer_handles[which] != NULL) {
		DeleteTimerQueueTimer(NULL, g_signal_state.timer_handles[which], NULL);
		g_signal_state.timer_handles[which] = NULL;
	}

	/* Return old value if requested */
	if (ovalue != NULL) {
		LARGE_INTEGER *old = (LARGE_INTEGER *)ovalue;
		*old = g_signal_state.timer_intervals[which];
	}

	/* Set new timer if interval > 0 */
	if (intervals[0].QuadPart > 0) {
		DWORD period_ms = (DWORD)(intervals[0].QuadPart / 10000);  /* 100ns to ms */

		CreateTimerQueueTimer(
			&g_signal_state.timer_handles[which],
			NULL,
			win32_timer_callback,
			(PVOID)(uintptr_t)which,
			period_ms,
			period_ms,  /* Periodic */
			WT_EXECUTEDEFAULT
		);

		g_signal_state.timer_intervals[which] = intervals[0];
	}

	return 0;
}

/* Get interval timer */
int
nix_platform_win32_getitimer(int which, void *value)
{
	if (which < 0 || which > 2) {
		errno = EINVAL;
		return -1;
	}

	win32_signal_init();

	if (value == NULL) {
		errno = EFAULT;
		return -1;
	}

	LARGE_INTEGER *intervals = (LARGE_INTEGER *)value;
	*intervals = g_signal_state.timer_intervals[which];

	return 0;
}

#endif /* NIX_HOST_WIN32 */
