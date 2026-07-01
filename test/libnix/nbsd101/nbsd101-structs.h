#ifndef __nbsd101_structs_h
#define __nbsd101_structs_h

#include "nix.h"
#include "nix-syscall.h" /* nix_endian_t + the personality ABI */

void
nix_timezone_to_nbsd101_timezone(nix_endian_t               endian,
                                struct nix_timezone const *in,
                                struct nbsd101_timezone    *out);

void
nix_timespec_to_nbsd101_timespec(nix_endian_t               endian,
                                struct nix_timespec const *in,
                                struct nbsd101_timespec    *out);

void
nix_timeval_to_nbsd101_timeval(nix_endian_t              endian,
                              struct nix_timeval const *in,
                              struct nbsd101_timeval    *out);

void
nix_stat_to_nbsd101_stat(nix_endian_t           endian,
                        struct nix_stat const *in,
                        struct nbsd101_stat    *out);

void
nix_sigaction_to_nbsd101_sigaction32(nix_endian_t                endian,
                                    struct nix_sigaction const *out,
                                    struct nbsd101_sigaction32  *in);

void
nbsd101_timezone_to_nix_timezone(nix_endian_t                  endian,
                                struct nbsd101_timezone const *in,
                                struct nix_timezone          *out);

void
nbsd101_timespec_to_nix_timespec(nix_endian_t                  endian,
                                struct nbsd101_timespec const *in,
                                struct nix_timespec          *out);

void
nbsd101_timeval_to_nix_timeval(nix_endian_t                 endian,
                              struct nbsd101_timeval const *in,
                              struct nix_timeval          *out);

void
nbsd101_sigaction32_to_nix_sigaction(nix_endian_t                     endian,
                                    struct nbsd101_sigaction32 const *in,
                                    struct nix_sigaction            *out);

void
nix_statfs_to_nbsd101_statfs(nix_endian_t             endian,
                            struct nix_statfs const *in,
                            struct nbsd101_statfs    *out);

int
nix_sockaddr_to_nbsd101_sockaddr(nix_endian_t               endian,
                                struct nix_sockaddr const *in,
                                nix_socklen_t              inlen,
                                struct nbsd101_sockaddr    *out,
                                nbsd101_socklen_t          *outlen);

int
nbsd101_sockaddr_to_nix_sockaddr(nix_endian_t                  endian,
                                struct nbsd101_sockaddr const *in,
                                nbsd101_socklen_t              inlen,
                                struct nix_sockaddr          *out,
                                nix_socklen_t                *outlen);

void
nix_rlimit_to_nbsd101_rlimit(nix_endian_t             endian,
                            struct nix_rlimit const *in,
                            struct nbsd101_rlimit    *out);

void
nix_rusage_to_nbsd101_rusage(nix_endian_t             endian,
                            struct nix_rusage const *in,
                            struct nbsd101_rusage    *out);

void
nbsd101_termios_to_nix_termios(nix_endian_t                 endian,
                              struct nbsd101_termios const *in,
                              struct nix_termios          *out);

void
nix_termios_to_nbsd101_termios(nix_endian_t              endian,
                              struct nix_termios const *in,
                              struct nbsd101_termios    *out);

void
nbsd101_fd_set_to_nix_fd_set(nix_endian_t         endian,
                            nbsd101_fd_set const *in,
                            nix_fd_set          *out);

void
nix_fd_set_to_nbsd101_fd_set(nix_endian_t      endian,
                            nix_fd_set const *in,
                            nbsd101_fd_set    *out);

void
nbsd101_pollfd_to_nix_pollfd(nix_endian_t                endian,
                            struct nbsd101_pollfd const *in,
                            struct nix_pollfd          *out);

void
nix_pollfd_to_nbsd101_pollfd(nix_endian_t             endian,
                            struct nix_pollfd const *in,
                            struct nbsd101_pollfd    *out);

#endif /* !__nbsd101_structs_h */
