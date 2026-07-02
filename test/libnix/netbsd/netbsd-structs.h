#ifndef __netbsd_structs_h
#define __netbsd_structs_h

#include "nix.h"
#include "nix-syscall.h" /* nix_endian_t + the personality ABI */

void
nix_timezone_to_netbsd_timezone(nix_endian_t               endian,
                                struct nix_timezone const *in,
                                struct netbsd_timezone    *out);

void
nix_timespec_to_netbsd_timespec(nix_endian_t               endian,
                                struct nix_timespec const *in,
                                struct netbsd_timespec    *out);

void
nix_timeval_to_netbsd_timeval(nix_endian_t              endian,
                              struct nix_timeval const *in,
                              struct netbsd_timeval    *out);

void
nix_stat_to_netbsd_stat(nix_endian_t           endian,
                        struct nix_stat const *in,
                        struct netbsd_stat    *out);

void
nix_sigaction_to_netbsd_sigaction32(nix_endian_t                endian,
                                    struct nix_sigaction const *out,
                                    struct netbsd_sigaction32  *in);

void
netbsd_timezone_to_nix_timezone(nix_endian_t                  endian,
                                struct netbsd_timezone const *in,
                                struct nix_timezone          *out);

void
netbsd_timespec_to_nix_timespec(nix_endian_t                  endian,
                                struct netbsd_timespec const *in,
                                struct nix_timespec          *out);

void
netbsd_timeval_to_nix_timeval(nix_endian_t                 endian,
                              struct netbsd_timeval const *in,
                              struct nix_timeval          *out);

void
netbsd_sigaction32_to_nix_sigaction(nix_endian_t                     endian,
                                    struct netbsd_sigaction32 const *in,
                                    struct nix_sigaction            *out);

void
nix_statfs_to_netbsd_statfs(nix_endian_t             endian,
                            struct nix_statfs const *in,
                            struct netbsd_statfs    *out);

int
nix_sockaddr_to_netbsd_sockaddr(nix_endian_t               endian,
                                struct nix_sockaddr const *in,
                                nix_socklen_t              inlen,
                                struct netbsd_sockaddr    *out,
                                netbsd_socklen_t          *outlen);

int
netbsd_sockaddr_to_nix_sockaddr(nix_endian_t                  endian,
                                struct netbsd_sockaddr const *in,
                                netbsd_socklen_t              inlen,
                                struct nix_sockaddr          *out,
                                nix_socklen_t                *outlen);

void
nix_rlimit_to_netbsd_rlimit(nix_endian_t             endian,
                            struct nix_rlimit const *in,
                            struct netbsd_rlimit    *out);

void
nix_rusage_to_netbsd_rusage(nix_endian_t             endian,
                            struct nix_rusage const *in,
                            struct netbsd_rusage    *out);

void
netbsd_termios_to_nix_termios(nix_endian_t                 endian,
                              struct netbsd_termios const *in,
                              struct nix_termios          *out);

void
nix_termios_to_netbsd_termios(nix_endian_t              endian,
                              struct nix_termios const *in,
                              struct netbsd_termios    *out);

void
netbsd_fd_set_to_nix_fd_set(nix_endian_t         endian,
                            netbsd_fd_set const *in,
                            nix_fd_set          *out);

void
nix_fd_set_to_netbsd_fd_set(nix_endian_t      endian,
                            nix_fd_set const *in,
                            netbsd_fd_set    *out);

void
netbsd_pollfd_to_nix_pollfd(nix_endian_t                endian,
                            struct netbsd_pollfd const *in,
                            struct nix_pollfd          *out);

void
nix_pollfd_to_netbsd_pollfd(nix_endian_t             endian,
                            struct nix_pollfd const *in,
                            struct netbsd_pollfd    *out);

#endif /* !__netbsd_structs_h */
