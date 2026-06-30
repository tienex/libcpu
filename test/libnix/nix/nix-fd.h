#ifndef __nix_fd_h
#define __nix_fd_h

#include "nix-env.h"

int
nix_fd_init(size_t count);

int
nix_fd_alloc(int fd, nix_env_t *env);

int
nix_fd_alloc_at(int gfd, int fd, nix_env_t *env);

int
nix_fd_release(int fd, nix_env_t *env);

int
nix_fd_get(int fd);

int
nix_fd_get_nearest(nix_env_t *env, int fd, int dir);

int
nix_getdtablesize(void);

#ifdef _WIN32
/* On win32 a socket is a SOCKET handle, a namespace distinct from CRT file descriptors, so the
   I/O layer must route socket fds to recv/send/closesocket instead of read/write/close. POSIX has
   no such split (a socket IS an fd), so this tagging is win32-only. The gfd is the guest fd. */
void
nix_fd_set_socket(int gfd, int is_socket);

int
nix_fd_is_socket(int gfd);
#endif

#endif /* !__nix_fd_h */
