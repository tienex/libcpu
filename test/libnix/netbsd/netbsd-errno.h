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

#ifndef __netbsd_errno_h
#define __netbsd_errno_h

#define NETBSD_PERM             1               /* Operation not permitted */
#define NETBSD_NOENT            2               /* No such file or directory */
#define NETBSD_SRCH             3               /* No such process */
#define NETBSD_INTR             4               /* Interrupted system call */
#define NETBSD_IO               5               /* Input/output error */
#define NETBSD_NXIO             6               /* Device not configured */
#define NETBSD_2BIG             7               /* Argument list too long */
#define NETBSD_NOEXEC           8               /* Exec format error */
#define NETBSD_BADF             9               /* Bad file descriptor */
#define NETBSD_CHILD            10              /* No child processes */
#define NETBSD_DEADLK           11              /* Resource deadlock avoided */
                                        /* 11 was EAGAIN */
#define NETBSD_NOMEM            12              /* Cannot allocate memory */
#define NETBSD_ACCES            13              /* Permission denied */
#define NETBSD_FAULT            14              /* Bad address */
#define NETBSD_NOTBLK           15              /* Block device required */
#define NETBSD_BUSY             16              /* Device busy */
#define NETBSD_EXIST            17              /* File exists */
#define NETBSD_XDEV             18              /* Cross-device link */
#define NETBSD_NODEV            19              /* Operation not supported by device */
#define NETBSD_NOTDIR           20              /* Not a directory */
#define NETBSD_ISDIR            21              /* Is a directory */
#define NETBSD_INVAL            22              /* Invalid argument */
#define NETBSD_NFILE            23              /* Too many open files in system */
#define NETBSD_MFILE            24              /* Too many open files */
#define NETBSD_NOTTY            25              /* Inappropriate ioctl for device */
#define NETBSD_TXTBSY           26              /* Text file busy */
#define NETBSD_FBIG             27              /* File too large */
#define NETBSD_NOSPC            28              /* No space left on device */
#define NETBSD_SPIPE            29              /* Illegal seek */
#define NETBSD_ROFS             30              /* Read-only file system */
#define NETBSD_MLINK            31              /* Too many links */
#define NETBSD_PIPE             32              /* Broken pipe */

/* math software */
#define NETBSD_DOM              33              /* Numerical argument out of domain */
#define NETBSD_RANGE            34              /* Result too large */

/* non-blocking and interrupt i/o */
#define NETBSD_AGAIN            35              /* Resource temporarily unavailable */
#define NETBSD_WOULDBLOCK       EAGAIN          /* Operation would block */
#define NETBSD_INPROGRESS       36              /* Operation now in progress */
#define NETBSD_ALREADY          37              /* Operation already in progress */

/* ipc/network software -- argument errors */
#define NETBSD_NOTSOCK          38              /* Socket operation on non-socket */
#define NETBSD_DESTADDRREQ      39              /* Destination address required */
#define NETBSD_MSGSIZE          40              /* Message too long */
#define NETBSD_PROTOTYPE        41              /* Protocol wrong type for socket */
#define NETBSD_NOPROTOOPT       42              /* Protocol not available */
#define NETBSD_PROTONOSUPPORT   43              /* Protocol not supported */
#define NETBSD_SOCKTNOSUPPORT   44              /* Socket type not supported */
#define NETBSD_OPNOTSUPP        45              /* Operation not supported */
#define NETBSD_PFNOSUPPORT      46              /* Protocol family not supported */
#define NETBSD_AFNOSUPPORT      47              /* Address family not supported by protocol family */
#define NETBSD_ADDRINUSE        48              /* Address already in use */
#define NETBSD_ADDRNOTAVAIL     49              /* Can't assign requested address */

/* ipc/network software -- operational errors */
#define NETBSD_NETDOWN          50              /* Network is down */
#define NETBSD_NETUNREACH       51              /* Network is unreachable */
#define NETBSD_NETRESET         52              /* Network dropped connection on reset */
#define NETBSD_CONNABORTED      53              /* Software caused connection abort */
#define NETBSD_CONNRESET        54              /* Connection reset by peer */
#define NETBSD_NOBUFS           55              /* No buffer space available */
#define NETBSD_ISCONN           56              /* Socket is already connected */
#define NETBSD_NOTCONN          57              /* Socket is not connected */
#define NETBSD_SHUTDOWN         58              /* Can't send after socket shutdown */
#define NETBSD_TOOMANYREFS      59              /* Too many references: can't splice */
#define NETBSD_TIMEDOUT         60              /* Operation timed out */
#define NETBSD_CONNREFUSED      61              /* Connection refused */

#define NETBSD_LOOP             62              /* Too many levels of symbolic links */
#define NETBSD_NAMETOOLONG      63              /* File name too long */

/* should be rearranged */
#define NETBSD_HOSTDOWN         64              /* Host is down */
#define NETBSD_HOSTUNREACH      65              /* No route to host */
#define NETBSD_NOTEMPTY         66              /* Directory not empty */

/* quotas & mush */
#define NETBSD_PROCLIM          67              /* Too many processes */
#define NETBSD_USERS            68              /* Too many users */
#define NETBSD_DQUOT            69              /* Disk quota exceeded */

/* Network File System */
#define NETBSD_STALE            70              /* Stale NFS file handle */
#define NETBSD_REMOTE           71              /* Too many levels of remote in path */
#define NETBSD_BADRPC           72              /* RPC struct is bad */
#define NETBSD_RPCMISMATCH      73              /* RPC version wrong */
#define NETBSD_PROGUNAVAIL      74              /* RPC prog. not avail */
#define NETBSD_PROGMISMATCH     75              /* Program version wrong */
#define NETBSD_PROCUNAVAIL      76              /* Bad procedure for program */

#define NETBSD_NOLCK            77              /* No locks available */
#define NETBSD_NOSYS            78              /* Function not implemented */

#define NETBSD_FTYPE            79              /* Inappropriate file type or format */
#define NETBSD_AUTH             80              /* Authentication error */
#define NETBSD_NEEDAUTH         81              /* Need authenticator */
#define NETBSD_IPSEC            82              /* IPsec processing failure */
#define NETBSD_NOATTR           83              /* Attribute not found */
#define NETBSD_ILSEQ            84              /* Illegal byte sequence */
#define NETBSD_NOMEDIUM         85              /* No medium found */
#define NETBSD_MEDIUMTYPE       86              /* Wrong Medium Type */
#define NETBSD_OVERFLOW         87              /* Conversion overflow */
#define NETBSD_CANCELED         88              /* Operation canceled */
#define NETBSD_LAST             88              /* Must be equal largest errno */

#endif  /* !__netbsd_errno_h */
