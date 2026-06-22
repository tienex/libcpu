/*
 * XEC - Optimizing Dynarec Engine
 *
 * Exception Handling -- Win32 backend.
 *
 * The win32 analog of xec-xcpt-unix.c: where the Unix backend traps memory faults
 * with a SIGSEGV/SIGBUS signal handler, win32 uses a Vectored Exception Handler that
 * catches EXCEPTION_ACCESS_VIOLATION. A registered handler is invoked the same way --
 * (signo, fault-address) -- and if it reports the fault handled (non-zero), execution
 * resumes; otherwise the exception propagates as before.
 *
 * Copyright (C) 2007 Orlando Bassotto. All rights reserved.
 */
#include <windows.h>
#include <stdint.h>

#include "xec-debug.h"
#include "xec-xcpt.h"

#ifndef SIGSEGV
#define SIGSEGV 11   /* win32 has no signal numbers; report faults as the Unix SIGSEGV value */
#endif

static xec_xcpt_handler_t  g_xcpt_handler = NULL;
static void               *g_xcpt_log = NULL;
static void               *g_veh = NULL;

static LONG CALLBACK
_xec_veh (EXCEPTION_POINTERS *ep)
{
  if (g_xcpt_handler != NULL
      && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
    /* ExceptionInformation[1] is the inaccessible address (the fault address). */
    void *addr = (void *) (uintptr_t) ep->ExceptionRecord->ExceptionInformation[1];
    if ((*g_xcpt_handler) (SIGSEGV, addr) != 0)
      return EXCEPTION_CONTINUE_EXECUTION;   /* the handler mapped/fixed the page */
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

xec_xcpt_handler_t
xec_xcpt_set_handler (xec_xcpt_handler_t handler)
{
  xec_xcpt_handler_t oldhandler = g_xcpt_handler;
  g_xcpt_handler = handler;
  return oldhandler;
}

void
__xec_xcpt_init (void)
{
  if (g_xcpt_log != NULL)
    return;

  g_xcpt_log = xec_log_register ("xcpt");

  /* First handler in the chain, so guest memory faults are seen before the CRT. */
  g_veh = AddVectoredExceptionHandler (1, _xec_veh);
}
