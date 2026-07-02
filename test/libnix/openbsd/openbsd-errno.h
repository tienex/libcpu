/*      $OpenBSD: errno.h,v 1.19 2007/05/21 17:01:49 jasper Exp $       */
/*      $NetBSD: errno.h,v 1.10 1996/01/20 01:33:53 jtc Exp $   */

/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *      The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)errno.h     8.5 (Berkeley) 1/21/94
 */

#ifndef __openbsd_errno_h
#define __openbsd_errno_h

#define OPENBSD_PERM             1               /* Operation not permitted */
#define OPENBSD_NOENT            2               /* No such file or directory */
#define OPENBSD_SRCH             3               /* No such process */
#define OPENBSD_INTR             4               /* Interrupted system call */
#define OPENBSD_IO               5               /* Input/output error */
#define OPENBSD_NXIO             6               /* Device not configured */
#define OPENBSD_2BIG             7               /* Argument list too long */
#define OPENBSD_NOEXEC           8               /* Exec format error */
#define OPENBSD_BADF             9               /* Bad file descriptor */
#define OPENBSD_CHILD            10              /* No child processes */
#define OPENBSD_DEADLK           11              /* Resource deadlock avoided */
                                        /* 11 was EAGAIN */
#define OPENBSD_NOMEM            12              /* Cannot allocate memory */
#define OPENBSD_ACCES            13              /* Permission denied */
#define OPENBSD_FAULT            14              /* Bad address */
#define OPENBSD_NOTBLK           15              /* Block device required */
#define OPENBSD_BUSY             16              /* Device busy */
#define OPENBSD_EXIST            17              /* File exists */
#define OPENBSD_XDEV             18              /* Cross-device link */
#define OPENBSD_NODEV            19              /* Operation not supported by device */
#define OPENBSD_NOTDIR           20              /* Not a directory */
#define OPENBSD_ISDIR            21              /* Is a directory */
#define OPENBSD_INVAL            22              /* Invalid argument */
#define OPENBSD_NFILE            23              /* Too many open files in system */
#define OPENBSD_MFILE            24              /* Too many open files */
#define OPENBSD_NOTTY            25              /* Inappropriate ioctl for device */
#define OPENBSD_TXTBSY           26              /* Text file busy */
#define OPENBSD_FBIG             27              /* File too large */
#define OPENBSD_NOSPC            28              /* No space left on device */
#define OPENBSD_SPIPE            29              /* Illegal seek */
#define OPENBSD_ROFS             30              /* Read-only file system */
#define OPENBSD_MLINK            31              /* Too many links */
#define OPENBSD_PIPE             32              /* Broken pipe */

/* math software */
#define OPENBSD_DOM              33              /* Numerical argument out of domain */
#define OPENBSD_RANGE            34              /* Result too large */

/* non-blocking and interrupt i/o */
#define OPENBSD_AGAIN            35              /* Resource temporarily unavailable */
#define OPENBSD_WOULDBLOCK       EAGAIN          /* Operation would block */
#define OPENBSD_INPROGRESS       36              /* Operation now in progress */
#define OPENBSD_ALREADY          37              /* Operation already in progress */

/* ipc/network software -- argument errors */
#define OPENBSD_NOTSOCK          38              /* Socket operation on non-socket */
#define OPENBSD_DESTADDRREQ      39              /* Destination address required */
#define OPENBSD_MSGSIZE          40              /* Message too long */
#define OPENBSD_PROTOTYPE        41              /* Protocol wrong type for socket */
#define OPENBSD_NOPROTOOPT       42              /* Protocol not available */
#define OPENBSD_PROTONOSUPPORT   43              /* Protocol not supported */
#define OPENBSD_SOCKTNOSUPPORT   44              /* Socket type not supported */
#define OPENBSD_OPNOTSUPP        45              /* Operation not supported */
#define OPENBSD_PFNOSUPPORT      46              /* Protocol family not supported */
#define OPENBSD_AFNOSUPPORT      47              /* Address family not supported by protocol family */
#define OPENBSD_ADDRINUSE        48              /* Address already in use */
#define OPENBSD_ADDRNOTAVAIL     49              /* Can't assign requested address */

/* ipc/network software -- operational errors */
#define OPENBSD_NETDOWN          50              /* Network is down */
#define OPENBSD_NETUNREACH       51              /* Network is unreachable */
#define OPENBSD_NETRESET         52              /* Network dropped connection on reset */
#define OPENBSD_CONNABORTED      53              /* Software caused connection abort */
#define OPENBSD_CONNRESET        54              /* Connection reset by peer */
#define OPENBSD_NOBUFS           55              /* No buffer space available */
#define OPENBSD_ISCONN           56              /* Socket is already connected */
#define OPENBSD_NOTCONN          57              /* Socket is not connected */
#define OPENBSD_SHUTDOWN         58              /* Can't send after socket shutdown */
#define OPENBSD_TOOMANYREFS      59              /* Too many references: can't splice */
#define OPENBSD_TIMEDOUT         60              /* Operation timed out */
#define OPENBSD_CONNREFUSED      61              /* Connection refused */

#define OPENBSD_LOOP             62              /* Too many levels of symbolic links */
#define OPENBSD_NAMETOOLONG      63              /* File name too long */

/* should be rearranged */
#define OPENBSD_HOSTDOWN         64              /* Host is down */
#define OPENBSD_HOSTUNREACH      65              /* No route to host */
#define OPENBSD_NOTEMPTY         66              /* Directory not empty */

/* quotas & mush */
#define OPENBSD_PROCLIM          67              /* Too many processes */
#define OPENBSD_USERS            68              /* Too many users */
#define OPENBSD_DQUOT            69              /* Disk quota exceeded */

/* Network File System */
#define OPENBSD_STALE            70              /* Stale NFS file handle */
#define OPENBSD_REMOTE           71              /* Too many levels of remote in path */
#define OPENBSD_BADRPC           72              /* RPC struct is bad */
#define OPENBSD_RPCMISMATCH      73              /* RPC version wrong */
#define OPENBSD_PROGUNAVAIL      74              /* RPC prog. not avail */
#define OPENBSD_PROGMISMATCH     75              /* Program version wrong */
#define OPENBSD_PROCUNAVAIL      76              /* Bad procedure for program */

#define OPENBSD_NOLCK            77              /* No locks available */
#define OPENBSD_NOSYS            78              /* Function not implemented */

#define OPENBSD_FTYPE            79              /* Inappropriate file type or format */
#define OPENBSD_AUTH             80              /* Authentication error */
#define OPENBSD_NEEDAUTH         81              /* Need authenticator */
#define OPENBSD_IPSEC            82              /* IPsec processing failure */
#define OPENBSD_NOATTR           83              /* Attribute not found */
#define OPENBSD_ILSEQ            84              /* Illegal byte sequence */
#define OPENBSD_NOMEDIUM         85              /* No medium found */
#define OPENBSD_MEDIUMTYPE       86              /* Wrong Medium Type */
#define OPENBSD_OVERFLOW         87              /* Conversion overflow */
#define OPENBSD_CANCELED         88              /* Operation canceled */
#define OPENBSD_LAST             88              /* Must be equal largest errno */

#endif  /* !__openbsd_errno_h */
