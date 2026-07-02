#ifndef __openbsd_guest_h
#define __openbsd_guest_h

#include "nix-syscall.h"
#include "openbsd-us-syscall-priv.h"

void
openbsd_guest_get_syscall (openbsd_us_syscall_t *self,
                          nix_monitor_t *xmon,
                          int *scno);

int
openbsd_guest_get_next_param (void                *self,
                             nix_monitor_t       *xmon,
                             unsigned             flags,
                             nix_param_type_t     type,
                             nix_param_t         *param);

void
openbsd_guest_set_result (void                *self,
                         nix_monitor_t       *xmon,
                         int                  error,
                         nix_param_t const   *result);

#endif  /* !__openbsd_guest_h */
