/* NIX: $Id: netbsd-ioctl-defs.h 286 2007-06-26 13:26:56Z orlando $ */
/*      $NetBSD: ioccom.h,v 1.4 2006/05/18 21:27:25 miod Exp $ */
/*      $NetBSD: ioccom.h,v 1.4 1994/10/30 21:49:56 cgd Exp $   */

/*-
 * Copyright (c) 1982, 1986, 1990, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
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
 *      @(#)ioccom.h    8.2 (Berkeley) 3/28/94
 */

#ifndef __netbsd_ioctl_common_h
#define __netbsd_ioctl_common_h

#include "netbsd-guest-types.h"

/*
 * Ioctl's have the command encoded in the lower word, and the size of
 * any in or out parameters in the upper word.  The high 3 bits of the
 * upper word are used to encode the in/out status of the parameter.
 */
#define NETBSD_IOCPARM_MASK    0x1fff          /* parameter length, at most 13 bits */
#define NETBSD_IOCPARM_LEN(x)  (((x) >> 16) & NETBSD_IOCPARM_MASK)
#define NETBSD_IOCBASECMD(x)   ((x) & ~(NETBSD_IOCPARM_MASK << 16))
#define NETBSD_IOCGROUP(x)     (((x) >> 8) & 0xff)

#define NETBSD_IOC_VOID        (netbsd_ulong_t)0x20000000
#define NETBSD_IOC_OUT         (netbsd_ulong_t)0x40000000     /* copy parameters out */
#define NETBSD_IOC_IN          (netbsd_ulong_t)0x80000000     /* copy parameters in */
#define NETBSD_IOC_INOUT       (NETBSD_IOC_IN|NETBSD_IOC_OUT) /* copy parameters in and out */
#define NETBSD_IOC_DIRMASK     (netbsd_ulong_t)0xe0000000     /* mask for IN/OUT/VOID */

#define _NETBSD_IOC(inout, group, num, len) \
        (inout | ((len & NETBSD_IOCPARM_MASK) << 16) | ((group) << 8) | (num))
#define _NETBSD_IO(g, n)       _NETBSD_IOC (NETBSD_IOC_VOID,  (g), (n), 0)
#define _NETBSD_IOR(g, n, t)   _NETBSD_IOC (NETBSD_IOC_OUT,   (g), (n), sizeof (t))
#define _NETBSD_IOW(g, n, t)   _NETBSD_IOC (NETBSD_IOC_IN,    (g), (n), sizeof (t))
/* this should be _IORW, but stdio got there first */
#define _NETBSD_IOWR(g, n, t)  _NETBSD_IOC (NETBSD_IOC_INOUT, (g), (n), sizeof (t))

#endif  /* !__netbsd_ioctl_common_h */
