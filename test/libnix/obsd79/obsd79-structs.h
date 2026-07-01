#ifndef __obsd79_structs_h
#define __obsd79_structs_h

#include "nix.h"
#include "nix-syscall.h" /* nix_endian_t + the personality ABI */

void
nix_timezone_to_obsd79_timezone(nix_endian_t               endian,
                                struct nix_timezone const *in,
                                struct obsd79_timezone    *out);

void
nix_timespec_to_obsd79_timespec(nix_endian_t               endian,
                                struct nix_timespec const *in,
                                struct obsd79_timespec    *out);

void
nix_timeval_to_obsd79_timeval(nix_endian_t              endian,
                              struct nix_timeval const *in,
                              struct obsd79_timeval    *out);

void
nix_stat_to_obsd79_stat(nix_endian_t           endian,
                        struct nix_stat const *in,
                        struct obsd79_stat    *out);

void
nix_sigaction_to_obsd79_sigaction32(nix_endian_t                endian,
                                    struct nix_sigaction const *out,
                                    struct obsd79_sigaction32  *in);

void
obsd79_timezone_to_nix_timezone(nix_endian_t                  endian,
                                struct obsd79_timezone const *in,
                                struct nix_timezone          *out);

void
obsd79_timespec_to_nix_timespec(nix_endian_t                  endian,
                                struct obsd79_timespec const *in,
                                struct nix_timespec          *out);

void
obsd79_timeval_to_nix_timeval(nix_endian_t                 endian,
                              struct obsd79_timeval const *in,
                              struct nix_timeval          *out);

void
obsd79_sigaction32_to_nix_sigaction(nix_endian_t                     endian,
                                    struct obsd79_sigaction32 const *in,
                                    struct nix_sigaction            *out);

void
nix_statfs_to_obsd79_statfs(nix_endian_t             endian,
                            struct nix_statfs const *in,
                            struct obsd79_statfs    *out);

int
nix_sockaddr_to_obsd79_sockaddr(nix_endian_t               endian,
                                struct nix_sockaddr const *in,
                                nix_socklen_t              inlen,
                                struct obsd79_sockaddr    *out,
                                obsd79_socklen_t          *outlen);

int
obsd79_sockaddr_to_nix_sockaddr(nix_endian_t                  endian,
                                struct obsd79_sockaddr const *in,
                                obsd79_socklen_t              inlen,
                                struct nix_sockaddr          *out,
                                nix_socklen_t                *outlen);

void
nix_rlimit_to_obsd79_rlimit(nix_endian_t             endian,
                            struct nix_rlimit const *in,
                            struct obsd79_rlimit    *out);

void
nix_rusage_to_obsd79_rusage(nix_endian_t             endian,
                            struct nix_rusage const *in,
                            struct obsd79_rusage    *out);

void
obsd79_termios_to_nix_termios(nix_endian_t                 endian,
                              struct obsd79_termios const *in,
                              struct nix_termios          *out);

void
nix_termios_to_obsd79_termios(nix_endian_t              endian,
                              struct nix_termios const *in,
                              struct obsd79_termios    *out);

void
obsd79_fd_set_to_nix_fd_set(nix_endian_t         endian,
                            obsd79_fd_set const *in,
                            nix_fd_set          *out);

void
nix_fd_set_to_obsd79_fd_set(nix_endian_t      endian,
                            nix_fd_set const *in,
                            obsd79_fd_set    *out);

void
obsd79_pollfd_to_nix_pollfd(nix_endian_t                endian,
                            struct obsd79_pollfd const *in,
                            struct nix_pollfd          *out);

void
nix_pollfd_to_obsd79_pollfd(nix_endian_t             endian,
                            struct nix_pollfd const *in,
                            struct obsd79_pollfd    *out);

#endif /* !__obsd79_structs_h */
