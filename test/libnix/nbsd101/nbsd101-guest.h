#ifndef __nbsd101_guest_h
#define __nbsd101_guest_h

#include "nix-syscall.h"
#include "nbsd101-us-syscall-priv.h"

void
nbsd101_guest_get_syscall (nbsd101_us_syscall_t *self,
                          nix_monitor_t *xmon,
                          int *scno);

int
nbsd101_guest_get_next_param (void                *self,
                             nix_monitor_t       *xmon,
                             unsigned             flags,
                             nix_param_type_t     type,
                             nix_param_t         *param);

void
nbsd101_guest_set_result (void                *self,
                         nix_monitor_t       *xmon,
                         int                  error,
                         nix_param_t const   *result);

#endif  /* !__nbsd101_guest_h */
