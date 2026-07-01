NAME: "NetBSD 10.1"
NAMESPACE: nbsd101
LIMIT: 500
BAE: EFAULT

CALLS


1 void exit (word)
2 word fork (void)
3 word read (word, ptr, word)
4 word write (word, ptr, word)
5 word open (ptr, word, word)
6 word close (word)

9 word link (ptr, ptr)
10 word unlink (ptr)

12 word chdir (ptr)
13 word fchdir (word)

15 word chmod (ptr, word)
16 word chown (ptr, word, word)
17 word obreak (intptr)

22 word unmount (ptr, word)
23 word setuid (word)

26 word ptrace (word, word, ptr, word)
27 word recvmsg (word, ptr, word)
28 word sendmsg (word, ptr, word)
29 word recvfrom (word, ptr, word, word, ptr, ptr)
30 word accept (word, ptr, ptr)
31 word getpeername (word, ptr, ptr)
32 word getsockname (word, ptr, ptr)
33 word access (ptr, word)
34 word chflags (ptr, intptr)
35 word fchflags (word, intptr)

37 word kill (word, word)

41 word dup (word)
42 dword pipe (void)

44 word profil (ptr, word, intptr, word)
45 word ktrace (ptr, word, word, word)

49 word __getlogin (ptr, word)
50 word __setlogin (ptr)
51 word acct (ptr)

54 word ioctl (word, intptr, intptr)

56 word revoke (ptr)
57 word symlink (ptr, ptr)
58 word readlink (ptr, ptr, word)
59 word execve (ptr, ptr, ptr)
60 word umask (word)
61 word chroot (ptr)

66 word vfork (void)

72 word ovadvise (word)
73 word munmap (ptr, word)
74 word mprotect (ptr, word, word)
75 word madvise (ptr, word, word)

78 word mincore (ptr, word, ptr)
79 word getgroups (word, ptr)
80 word setgroups (word, ptr)
81 word getpgrp (void)
82 word setpgid (word, word)

90 word dup2 (word, word)
91 word getrandom (ptr, word, word)
92 word fcntl (word, word, intptr)

95 word fsync (word)
96 word setpriority (word, word, word)

98 word connect (word, ptr, word)

100 word getpriority (word, word)

104 word bind (word, ptr, word)
105 word setsockopt (word, word, word, ptr, word)
106 word listen (word, word)

118 word getsockopt (word, word, word, ptr, ptr)

120 word readv (word, ptr, word)
121 word writev (word, ptr, word)

123 word fchown (word, word, word)
124 word fchmod (word, word)

126 word setreuid (word, word)
127 word setregid (word, word)
128 word rename (ptr, ptr)

131 word flock (word, word)
132 word mkfifo (ptr, word)
133 word sendto (word, ptr, word, word, ptr, word)
134 word shutdown (word, word)
135 word socketpair (word, word, word, ptr)
136 word mkdir (ptr, word)
137 word rmdir (ptr)

147 word setsid (void)

155 word nfssvc (word, ptr)

165 word sysarch (word, ptr)
166 word __futex (ptr, word, word, ptr, ptr, word, word)
167 word __futex_set_robust_list (ptr, word)
168 word __futex_get_robust_list (word, ptr, ptr)

173 word pread (word, ptr, word, word, dword)
174 word pwrite (word, ptr, word, word, dword)

176 word ntp_adjtime (ptr)
177 word timerfd_create (word, word)
178 word timerfd_settime (word, word, ptr, ptr)
179 word timerfd_gettime (word, ptr)

181 word setgid (word)
182 word setegid (word)
183 word seteuid (word)
184 word lfs_bmapv (ptr, ptr, word)
185 word lfs_markv (ptr, ptr, word)
186 word lfs_segclean (ptr, intptr)

191 intptr pathconf (ptr, word)
192 intptr fpathconf (word, word)
193 word getsockopt2 (word, word, word, ptr, ptr)
194 word getrlimit (word, ptr)
195 word setrlimit (word, ptr)

197 intptr mmap (intptr, word, word, word, word, intptr, dword)

199 dword lseek (word, word, dword, word)
200 word truncate (ptr, word, dword)
201 word ftruncate (word, word, dword)
202 word __sysctl (ptr, word, ptr, ptr, ptr, word)
203 word mlock (ptr, word)
204 word munlock (ptr, word)
205 word undelete (ptr)

207 word getpgid (word)
208 word reboot (word, ptr)
209 word poll (ptr, word, word)

221 word semget (word, word, word)
222 word semop (word, ptr, word)
223 word semconfig (word)

225 word msgget (word, word)
226 word msgsnd (word, ptr, word, word)
227 word msgrcv (word, ptr, word, intptr, word)
228 intptr shmat (word, ptr, word)

230 word shmdt (ptr)
231 word shmget (word, word, word)

235 word timer_create (word, ptr, ptr)
236 word timer_delete (word)

239 word timer_getoverrun (word)

241 word fdatasync (word)
242 word mlockall (word)
243 word munlockall (void)

245 word sigqueueinfo (word, ptr)
246 word modctl (word, ptr)
247 word _ksem_init (word, ptr)
248 word _ksem_open (ptr, word, word, word, ptr)
249 word _ksem_unlink (ptr)
250 word _ksem_close (intptr)
251 word _ksem_post (intptr)
252 word _ksem_wait (intptr)
253 word _ksem_trywait (intptr)
254 word _ksem_getvalue (intptr, ptr)
255 word _ksem_destroy (intptr)
256 word _ksem_timedwait (intptr, ptr)
257 word mq_open (ptr, word, word, ptr)
258 word mq_close (word)
259 word mq_unlink (ptr)
260 word mq_getattr (word, ptr)
261 word mq_setattr (word, ptr, ptr)
262 word mq_notify (word, ptr)
263 word mq_send (word, ptr, word, word)
264 word mq_receive (word, ptr, word, ptr)

267 word eventfd (word, word)

270 word __posix_rename (ptr, ptr)
271 word swapctl (word, ptr, word)

273 word minherit (ptr, word, word)
274 word lchmod (ptr, word)
275 word lchown (ptr, word, word)

283 word __posix_chown (ptr, word, word)
284 word __posix_fchown (word, word, word)
285 word __posix_lchown (ptr, word, word)
286 word getsid (word)
287 word __clone (word, ptr)
288 word fktrace (word, word, word, word)
289 word preadv (word, ptr, word, word, dword)
290 word pwritev (word, ptr, word, word, dword)

296 word __getcwd (ptr, word)
297 word fchroot (word)

304 word lchflags (ptr, intptr)

306 word utrace (ptr, ptr, word)
307 word getcontext (ptr)
308 word setcontext (ptr)
309 word _lwp_create (ptr, intptr, ptr)
310 word _lwp_exit (void)
311 word _lwp_self (void)
312 word _lwp_wait (word, ptr)
313 word _lwp_suspend (word)
314 word _lwp_continue (word)
315 word _lwp_wakeup (word)
316 intptr _lwp_getprivate (void)
317 void _lwp_setprivate (ptr)
318 word _lwp_kill (word, word)
319 word _lwp_detach (word)

321 word _lwp_unpark (word, ptr)
322 word _lwp_unpark_all (ptr, word, ptr)
323 word _lwp_setname (word, ptr)
324 word _lwp_getname (word, ptr, word)
325 word _lwp_ctl (word, ptr)

340 word __sigaction_sigtramp (word, ptr, ptr, ptr, word)

343 word rasctl (ptr, word, word)
344 word kqueue (void)

346 word _sched_setparam (word, word, word, ptr)
347 word _sched_getparam (word, word, ptr, ptr)
348 word _sched_setaffinity (word, word, word, ptr)
349 word _sched_getaffinity (word, word, word, ptr)
350 word sched_yield (void)
351 word _sched_protect (word)

354 word fsync_range (word, word, dword, dword)
355 word uuidgen (ptr, word)

360 word extattrctl (ptr, word, ptr, word, ptr)
361 word extattr_set_file (ptr, word, ptr, ptr, word)
362 word extattr_get_file (ptr, word, ptr, ptr, word)
363 word extattr_delete_file (ptr, word, ptr)
364 word extattr_set_fd (word, word, ptr, ptr, word)
365 word extattr_get_fd (word, word, ptr, ptr, word)
366 word extattr_delete_fd (word, word, ptr)
367 word extattr_set_link (ptr, word, ptr, ptr, word)
368 word extattr_get_link (ptr, word, ptr, ptr, word)
369 word extattr_delete_link (ptr, word, ptr)
370 word extattr_list_fd (word, word, ptr, word)
371 word extattr_list_file (ptr, word, ptr, word)
372 word extattr_list_link (ptr, word, ptr, word)

375 word setxattr (ptr, ptr, ptr, word, word)
376 word lsetxattr (ptr, ptr, ptr, word, word)
377 word fsetxattr (word, ptr, ptr, word, word)
378 word getxattr (ptr, ptr, ptr, word)
379 word lgetxattr (ptr, ptr, ptr, word)
380 word fgetxattr (word, ptr, ptr, word)
381 word listxattr (ptr, ptr, word)
382 word llistxattr (ptr, ptr, word)
383 word flistxattr (word, ptr, word)
384 word removexattr (ptr, ptr)
385 word lremovexattr (ptr, ptr)
386 word fremovexattr (word, ptr)

399 word aio_cancel (word, ptr)
400 word aio_error (ptr)
401 word aio_fsync (word, ptr)
402 word aio_read (ptr)
403 word aio_return (ptr)

405 word aio_write (ptr)
406 word lio_listio (word, ptr, word, ptr)

411 intptr mremap (ptr, word, ptr, word, word)
412 word pset_create (ptr)
413 word pset_destroy (word)
414 word pset_assign (word, word, ptr)
415 word _pset_bind (word, word, word, word, ptr)

453 word pipe2 (ptr, word)
454 word dup3 (word, word, word)
455 word kqueue1 (word)
456 word paccept (word, ptr, ptr, ptr, word)
457 word linkat (word, ptr, word, ptr, word)
458 word renameat (word, ptr, word, ptr)
459 word mkfifoat (word, ptr, word)
460 word mknodat (word, ptr, word, word, dword)
461 word mkdirat (word, ptr, word)
462 word faccessat (word, ptr, word, word)
463 word fchmodat (word, ptr, word, word)
464 word fchownat (word, ptr, word, word, word)
465 word fexecve (word, ptr, ptr)
466 word fstatat (word, ptr, ptr, word)
467 word utimensat (word, ptr, ptr, word)
468 word openat (word, ptr, word, word)
469 word readlinkat (word, ptr, ptr, word)
470 word symlinkat (ptr, word, ptr)
471 word unlinkat (word, ptr, word)
472 word futimens (word, ptr)
473 word __quotactl (ptr, ptr)

475 word recvmmsg (word, ptr, word, word, ptr)
476 word sendmmsg (word, ptr, word, word)

480 word fdiscard (word, word, dword, dword)
481 word wait6 (word, word, ptr, word, ptr, ptr)
482 word clock_getcpuclockid2 (word, word, ptr)

487 word __acl_get_link (ptr, word, ptr)
488 word __acl_set_link (ptr, word, ptr)
489 word __acl_delete_link (ptr, word)
490 word __acl_aclcheck_link (ptr, word, ptr)
491 word __acl_get_file (ptr, word, ptr)
492 word __acl_set_file (ptr, word, ptr)
493 word __acl_get_fd (word, word, ptr)
494 word __acl_set_fd (word, word, ptr)
495 word __acl_delete_file (ptr, word)
496 word __acl_delete_fd (word, word)
497 word __acl_aclcheck_file (ptr, word, ptr)
498 word __acl_aclcheck_fd (word, word, ptr)
499 intptr lpathconf (ptr, word)
