#ifndef __openbsd_structs_h
#define __openbsd_structs_h

#include "nix.h"
#include "nix-syscall.h" /* nix_endian_t + the personality ABI */

void
nix_timezone_to_openbsd_timezone(nix_endian_t               endian,
                                struct nix_timezone const *in,
                                struct openbsd_timezone    *out);

void
nix_timespec_to_openbsd_timespec(nix_endian_t               endian,
                                struct nix_timespec const *in,
                                struct openbsd_timespec    *out);

void
nix_timeval_to_openbsd_timeval(nix_endian_t              endian,
                              struct nix_timeval const *in,
                              struct openbsd_timeval    *out);

void
nix_stat_to_openbsd_stat(nix_endian_t           endian,
                        struct nix_stat const *in,
                        struct openbsd_stat    *out);

void
nix_sigaction_to_openbsd_sigaction32(nix_endian_t                endian,
                                    struct nix_sigaction const *out,
                                    struct openbsd_sigaction32  *in);

void
openbsd_timezone_to_nix_timezone(nix_endian_t                  endian,
                                struct openbsd_timezone const *in,
                                struct nix_timezone          *out);

void
openbsd_timespec_to_nix_timespec(nix_endian_t                  endian,
                                struct openbsd_timespec const *in,
                                struct nix_timespec          *out);

void
openbsd_timeval_to_nix_timeval(nix_endian_t                 endian,
                              struct openbsd_timeval const *in,
                              struct nix_timeval          *out);

void
openbsd_sigaction32_to_nix_sigaction(nix_endian_t                     endian,
                                    struct openbsd_sigaction32 const *in,
                                    struct nix_sigaction            *out);

void
nix_statfs_to_openbsd_statfs(nix_endian_t             endian,
                            struct nix_statfs const *in,
                            struct openbsd_statfs    *out);

int
nix_sockaddr_to_openbsd_sockaddr(nix_endian_t               endian,
                                struct nix_sockaddr const *in,
                                nix_socklen_t              inlen,
                                struct openbsd_sockaddr    *out,
                                openbsd_socklen_t          *outlen);

int
openbsd_sockaddr_to_nix_sockaddr(nix_endian_t                  endian,
                                struct openbsd_sockaddr const *in,
                                openbsd_socklen_t              inlen,
                                struct nix_sockaddr          *out,
                                nix_socklen_t                *outlen);

void
nix_rlimit_to_openbsd_rlimit(nix_endian_t             endian,
                            struct nix_rlimit const *in,
                            struct openbsd_rlimit    *out);

void
nix_rusage_to_openbsd_rusage(nix_endian_t             endian,
                            struct nix_rusage const *in,
                            struct openbsd_rusage    *out);

void
openbsd_termios_to_nix_termios(nix_endian_t                 endian,
                              struct openbsd_termios const *in,
                              struct nix_termios          *out);

void
nix_termios_to_openbsd_termios(nix_endian_t              endian,
                              struct nix_termios const *in,
                              struct openbsd_termios    *out);

void
openbsd_fd_set_to_nix_fd_set(nix_endian_t         endian,
                            openbsd_fd_set const *in,
                            nix_fd_set          *out);

void
nix_fd_set_to_openbsd_fd_set(nix_endian_t      endian,
                            nix_fd_set const *in,
                            openbsd_fd_set    *out);

void
openbsd_pollfd_to_nix_pollfd(nix_endian_t                endian,
                            struct openbsd_pollfd const *in,
                            struct nix_pollfd          *out);

void
nix_pollfd_to_openbsd_pollfd(nix_endian_t             endian,
                            struct nix_pollfd const *in,
                            struct openbsd_pollfd    *out);

#endif /* !__openbsd_structs_h */
