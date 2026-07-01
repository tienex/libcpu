/*      $NetBSD: errno.h,v 1.19 2007/05/21 17:01:49 jasper Exp $       */
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

#ifndef __nbsd101_errno_h
#define __nbsd101_errno_h

#define NBSD101_PERM             1               /* Operation not permitted */
#define NBSD101_NOENT            2               /* No such file or directory */
#define NBSD101_SRCH             3               /* No such process */
#define NBSD101_INTR             4               /* Interrupted system call */
#define NBSD101_IO               5               /* Input/output error */
#define NBSD101_NXIO             6               /* Device not configured */
#define NBSD101_2BIG             7               /* Argument list too long */
#define NBSD101_NOEXEC           8               /* Exec format error */
#define NBSD101_BADF             9               /* Bad file descriptor */
#define NBSD101_CHILD            10              /* No child processes */
#define NBSD101_DEADLK           11              /* Resource deadlock avoided */
                                        /* 11 was EAGAIN */
#define NBSD101_NOMEM            12              /* Cannot allocate memory */
#define NBSD101_ACCES            13              /* Permission denied */
#define NBSD101_FAULT            14              /* Bad address */
#define NBSD101_NOTBLK           15              /* Block device required */
#define NBSD101_BUSY             16              /* Device busy */
#define NBSD101_EXIST            17              /* File exists */
#define NBSD101_XDEV             18              /* Cross-device link */
#define NBSD101_NODEV            19              /* Operation not supported by device */
#define NBSD101_NOTDIR           20              /* Not a directory */
#define NBSD101_ISDIR            21              /* Is a directory */
#define NBSD101_INVAL            22              /* Invalid argument */
#define NBSD101_NFILE            23              /* Too many open files in system */
#define NBSD101_MFILE            24              /* Too many open files */
#define NBSD101_NOTTY            25              /* Inappropriate ioctl for device */
#define NBSD101_TXTBSY           26              /* Text file busy */
#define NBSD101_FBIG             27              /* File too large */
#define NBSD101_NOSPC            28              /* No space left on device */
#define NBSD101_SPIPE            29              /* Illegal seek */
#define NBSD101_ROFS             30              /* Read-only file system */
#define NBSD101_MLINK            31              /* Too many links */
#define NBSD101_PIPE             32              /* Broken pipe */

/* math software */
#define NBSD101_DOM              33              /* Numerical argument out of domain */
#define NBSD101_RANGE            34              /* Result too large */

/* non-blocking and interrupt i/o */
#define NBSD101_AGAIN            35              /* Resource temporarily unavailable */
#define NBSD101_WOULDBLOCK       EAGAIN          /* Operation would block */
#define NBSD101_INPROGRESS       36              /* Operation now in progress */
#define NBSD101_ALREADY          37              /* Operation already in progress */

/* ipc/network software -- argument errors */
#define NBSD101_NOTSOCK          38              /* Socket operation on non-socket */
#define NBSD101_DESTADDRREQ      39              /* Destination address required */
#define NBSD101_MSGSIZE          40              /* Message too long */
#define NBSD101_PROTOTYPE        41              /* Protocol wrong type for socket */
#define NBSD101_NOPROTOOPT       42              /* Protocol not available */
#define NBSD101_PROTONOSUPPORT   43              /* Protocol not supported */
#define NBSD101_SOCKTNOSUPPORT   44              /* Socket type not supported */
#define NBSD101_OPNOTSUPP        45              /* Operation not supported */
#define NBSD101_PFNOSUPPORT      46              /* Protocol family not supported */
#define NBSD101_AFNOSUPPORT      47              /* Address family not supported by protocol family */
#define NBSD101_ADDRINUSE        48              /* Address already in use */
#define NBSD101_ADDRNOTAVAIL     49              /* Can't assign requested address */

/* ipc/network software -- operational errors */
#define NBSD101_NETDOWN          50              /* Network is down */
#define NBSD101_NETUNREACH       51              /* Network is unreachable */
#define NBSD101_NETRESET         52              /* Network dropped connection on reset */
#define NBSD101_CONNABORTED      53              /* Software caused connection abort */
#define NBSD101_CONNRESET        54              /* Connection reset by peer */
#define NBSD101_NOBUFS           55              /* No buffer space available */
#define NBSD101_ISCONN           56              /* Socket is already connected */
#define NBSD101_NOTCONN          57              /* Socket is not connected */
#define NBSD101_SHUTDOWN         58              /* Can't send after socket shutdown */
#define NBSD101_TOOMANYREFS      59              /* Too many references: can't splice */
#define NBSD101_TIMEDOUT         60              /* Operation timed out */
#define NBSD101_CONNREFUSED      61              /* Connection refused */

#define NBSD101_LOOP             62              /* Too many levels of symbolic links */
#define NBSD101_NAMETOOLONG      63              /* File name too long */

/* should be rearranged */
#define NBSD101_HOSTDOWN         64              /* Host is down */
#define NBSD101_HOSTUNREACH      65              /* No route to host */
#define NBSD101_NOTEMPTY         66              /* Directory not empty */

/* quotas & mush */
#define NBSD101_PROCLIM          67              /* Too many processes */
#define NBSD101_USERS            68              /* Too many users */
#define NBSD101_DQUOT            69              /* Disk quota exceeded */

/* Network File System */
#define NBSD101_STALE            70              /* Stale NFS file handle */
#define NBSD101_REMOTE           71              /* Too many levels of remote in path */
#define NBSD101_BADRPC           72              /* RPC struct is bad */
#define NBSD101_RPCMISMATCH      73              /* RPC version wrong */
#define NBSD101_PROGUNAVAIL      74              /* RPC prog. not avail */
#define NBSD101_PROGMISMATCH     75              /* Program version wrong */
#define NBSD101_PROCUNAVAIL      76              /* Bad procedure for program */

#define NBSD101_NOLCK            77              /* No locks available */
#define NBSD101_NOSYS            78              /* Function not implemented */

#define NBSD101_FTYPE            79              /* Inappropriate file type or format */
#define NBSD101_AUTH             80              /* Authentication error */
#define NBSD101_NEEDAUTH         81              /* Need authenticator */
#define NBSD101_IPSEC            82              /* IPsec processing failure */
#define NBSD101_NOATTR           83              /* Attribute not found */
#define NBSD101_ILSEQ            84              /* Illegal byte sequence */
#define NBSD101_NOMEDIUM         85              /* No medium found */
#define NBSD101_MEDIUMTYPE       86              /* Wrong Medium Type */
#define NBSD101_OVERFLOW         87              /* Conversion overflow */
#define NBSD101_CANCELED         88              /* Operation canceled */
#define NBSD101_LAST             88              /* Must be equal largest errno */

#endif  /* !__nbsd101_errno_h */
