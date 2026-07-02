#ifndef __netbsd_guest_h
#define __netbsd_guest_h

#include "nix-syscall.h"
#include "netbsd-us-syscall-priv.h"

void
netbsd_guest_get_syscall (netbsd_us_syscall_t *self,
                          nix_monitor_t *xmon,
                          int *scno);

int
netbsd_guest_get_next_param (void                *self,
                             nix_monitor_t       *xmon,
                             unsigned             flags,
                             nix_param_type_t     type,
                             nix_param_t         *param);

void
netbsd_guest_set_result (void                *self,
                         nix_monitor_t       *xmon,
                         int                  error,
                         nix_param_t const   *result);

#endif  /* !__netbsd_guest_h */
