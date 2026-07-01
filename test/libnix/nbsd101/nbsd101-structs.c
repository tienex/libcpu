#include "netbsd101.h"
#include "LibCPU/LcLog.h"
#include "nix-byte-order.h"

#include "nbsd101-stat.h"

#include <string.h>

extern void *g_bsd_log;

#define GE16(x) (((endian) != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int16(x) : (uint16_t)(x))
#define GE32(x) (((endian) != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int32(x) : (uint32_t)(x))
#define GE64(x) (((endian) != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int64(x) : (uint64_t)(x))

#define GELONG(x) ((sizeof(nbsd101_long_t) == sizeof(uint64_t)) ? (nbsd101_long_t)GE64(x) : (nbsd101_long_t)GE32(x))

void
nix_timezone_to_nbsd101_timezone(nix_endian_t               endian,
                                struct nix_timezone const *in,
                                struct nbsd101_timezone    *out)
{
	out->tz_minuteswest = GE32(in->tz_minuteswest);
	out->tz_dsttime = GE32(in->tz_dsttime);
}

void
nbsd101_timezone_to_nix_timezone(nix_endian_t                  endian,
                                struct nbsd101_timezone const *in,
                                struct nix_timezone          *out)
{
	out->tz_minuteswest = GE32(in->tz_minuteswest);
	out->tz_dsttime = GE32(in->tz_dsttime);
}

void
nix_timespec_to_nbsd101_timespec(nix_endian_t               endian,
                                struct nix_timespec const *in,
                                struct nbsd101_timespec    *out)
{
	if (sizeof(out->tv_sec) == sizeof(uint32_t))
		out->tv_sec = GE32(in->tv_sec & 0xffffffff);
	else if (sizeof(out->tv_sec) == sizeof(uint64_t))
		out->tv_sec = GE64(in->tv_sec);
	else
		LCAssert(g_bsd_log, 0);

	if (sizeof(out->tv_nsec) == sizeof(uint32_t))
		out->tv_nsec = GE32(in->tv_nsec & 0xffffffff);
	else if (sizeof(out->tv_nsec) == sizeof(uint64_t))
		out->tv_nsec = GE64(in->tv_nsec);
	else
		LCAssert(g_bsd_log, 0);
}

void
nbsd101_timespec_to_nix_timespec(nix_endian_t                  endian,
                                struct nbsd101_timespec const *in,
                                struct nix_timespec          *out)
{
	if (sizeof(in->tv_sec) == sizeof(uint32_t))
		out->tv_sec = GE32(in->tv_sec);
	else if (sizeof(in->tv_sec) == sizeof(uint64_t))
		out->tv_sec = GE64(in->tv_sec);
	else
		LCAssert(g_bsd_log, 0);

	if (sizeof(in->tv_nsec) == sizeof(uint32_t))
		out->tv_nsec = GE32(in->tv_nsec);
	else if (sizeof(in->tv_nsec) == sizeof(uint64_t))
		out->tv_nsec = GE64(in->tv_nsec);
	else
		LCAssert(g_bsd_log, 0);
}

void
nix_timeval_to_nbsd101_timeval(nix_endian_t              endian,
                              struct nix_timeval const *in,
                              struct nbsd101_timeval    *out)
{
	if (sizeof(out->tv_sec) == sizeof(uint32_t))
		out->tv_sec = GE32(in->tv_sec & 0xffffffff);
	else if (sizeof(out->tv_sec) == sizeof(uint64_t))
		out->tv_sec = GE64(in->tv_sec);
	else
		LCAssert(g_bsd_log, 0);

	if (sizeof(in->tv_usec) == sizeof(uint32_t))
		out->tv_usec = GE32(in->tv_usec & 0xffffffff);
	else if (sizeof(in->tv_usec) == sizeof(uint64_t))
		out->tv_usec = GE64(in->tv_usec);
	else
		LCAssert(g_bsd_log, 0);
}

void
nbsd101_timeval_to_nix_timeval(nix_endian_t                 endian,
                              struct nbsd101_timeval const *in,
                              struct nix_timeval          *out)
{
	if (sizeof(in->tv_sec) == sizeof(uint32_t))
		out->tv_sec = GE32(in->tv_sec & 0xffffffff);
	else if (sizeof(in->tv_sec) == sizeof(uint64_t))
		out->tv_sec = GE64(in->tv_sec);
	else
		LCAssert(g_bsd_log, 0);

	if (sizeof(in->tv_usec) == sizeof(uint32_t))
		out->tv_usec = GE32(in->tv_usec & 0xffffffff);
	else if (sizeof(in->tv_usec) == sizeof(uint64_t))
		out->tv_usec = GE64(in->tv_usec);
	else
		LCAssert(g_bsd_log, 0);
}

static __inline nbsd101_mode_t
nix_mode_to_nbsd101_mode(nix_mode_t mode)
{
	nbsd101_mode_t result = 0;

	if (mode == 0)
		return (0);

	switch (mode & NIX_S_IFMT) {
	case NIX_S_IFIFO:
		result |= NBSD101_S_IFIFO;
		break;
	case NIX_S_IFCHR:
		result |= NBSD101_S_IFCHR;
		break;
	case NIX_S_IFDIR:
		result |= NBSD101_S_IFDIR;
		break;
	case NIX_S_IFBLK:
		result |= NBSD101_S_IFBLK;
		break;
	case NIX_S_IFREG:
		result |= NBSD101_S_IFREG;
		break;
	case NIX_S_IFLNK:
		result |= NBSD101_S_IFLNK;
		break;
	case NIX_S_IFSOCK:
		result |= NBSD101_S_IFSOCK;
		break;
	}

	if (mode & NIX_S_ISUID)
		result |= NBSD101_S_ISUID;
	if (mode & NIX_S_ISGID)
		result |= NBSD101_S_ISGID;
	if (mode & NIX_S_ISVTX)
		result |= NBSD101_S_ISVTX;

	return (result | (mode & 0777));
}

void
nix_stat_to_nbsd101_stat(nix_endian_t           endian,
                        struct nix_stat const *in,
                        struct nbsd101_stat    *out)
{
	out->st_dev = GE32(in->st_dev);
	out->st_ino = GE32(in->st_ino);
	out->st_mode = GE32(nix_mode_to_nbsd101_mode(in->st_mode));
	out->st_nlink = GE32(in->st_nlink);
	out->st_uid = GE32(in->st_uid);
	out->st_gid = GE32(in->st_gid);
	out->st_rdev = GE32(in->st_rdev);
	nix_timespec_to_nbsd101_timespec(endian, &in->st_atimespec, &out->st_atimespec);
	nix_timespec_to_nbsd101_timespec(endian, &in->st_mtimespec, &out->st_mtimespec);
	nix_timespec_to_nbsd101_timespec(endian, &in->st_ctimespec, &out->st_ctimespec);
	nix_timespec_to_nbsd101_timespec(endian, &in->st_btimespec, &out->__st_birthtimespec);
	out->st_size = GE64(in->st_size);
	out->st_blocks = GE32(in->st_blocks);
	out->st_blksize = GE32(in->st_blksize);
	/* XXX FLAGS SHOULD BE CONVERTED! */
	out->st_flags = GE32(in->st_flags);
	out->st_gen = GE32(in->st_gen & 0xffffffff);
}

void
nbsd101_sigaction32_to_nix_sigaction(nix_endian_t                     endian,
                                    struct nbsd101_sigaction32 const *in,
                                    struct nix_sigaction            *out)
{
	out->__sa_handler = GE32(in->__sa_handler);
	out->sa_flags = GE32(in->sa_flags);
	out->sa_mask = GE32(in->sa_mask);
}

void
nix_sigaction_to_nbsd101_sigaction32(nix_endian_t                endian,
                                    struct nix_sigaction const *in,
                                    struct nbsd101_sigaction32  *out)
{
	out->__sa_handler = GE32(in->__sa_handler);
	out->sa_flags = GE32(in->sa_flags);
	out->sa_mask = GE32(in->sa_mask);
}

void
nix_statfs_to_nbsd101_statfs(nix_endian_t             endian,
                            struct nix_statfs const *in,
                            struct nbsd101_statfs    *out)
{
	out->f_flags = GE32(in->f_flags);
	out->f_bsize = GE32(in->f_bsize);
	out->f_iosize = GE32(in->f_iosize);
	out->f_blocks = GE32(in->f_blocks);
	out->f_bfree = GE32(in->f_bfree);
	out->f_bavail = GE32(in->f_bavail);
	out->f_files = GE32(in->f_files);
	out->f_ffree = GE32(in->f_ffree);
	out->f_fsid.val[0] = GE32(in->f_fsid >> 32);
	out->f_fsid.val[1] = GE32(in->f_fsid & 0xffffffff);
	out->f_owner = GE32(in->f_owner);
	out->f_syncwrites = GE32(in->f_syncwrites);
	out->f_asyncwrites = GE32(in->f_asyncwrites);
	out->f_ctime = GE32(in->f_ctime.tv_sec & 0xffffffff);
	out->f_spare[0] = 0;
	out->f_spare[1] = 0;
	out->f_spare[2] = 0;
	strncpy(out->f_fstypename, in->f_fstypename, NIX_MIN(sizeof(out->f_fstypename), sizeof(in->f_fstypename)));
	strncpy(out->f_mntonname, in->f_mntonname, NIX_MIN(sizeof(out->f_mntonname), sizeof(in->f_mntonname)));
	strncpy(out->f_mntfromname, in->f_mntfromname, NIX_MIN(sizeof(out->f_mntfromname), sizeof(in->f_mntfromname)));

	memset(&out->mount_info, 0, sizeof(out->mount_info));
}

static void
nix_sockaddr_in_to_nbsd101_sockaddr_in(nix_endian_t                  endian,
                                      struct nix_sockaddr_in const *in,
                                      struct nbsd101_sockaddr_in    *out)
{
	out->sin_len = sizeof(*out);
	out->sin_family = in->sin_family;
	out->sin_port = in->sin_port;
	out->sin_addr = in->sin_addr;
}

static void
nbsd101_sockaddr_in_to_nix_sockaddr_in(nix_endian_t                     endian,
                                      struct nbsd101_sockaddr_in const *in,
                                      struct nix_sockaddr_in          *out)
{
	LCAssert(g_bsd_log, in->sin_len == 0 || in->sin_len == sizeof(*in));
	out->sin_family = in->sin_family;
	out->sin_port = in->sin_port;
	out->sin_addr = in->sin_addr;
}

static void
nix_sockaddr_un_to_nbsd101_sockaddr_un(nix_endian_t                  endian,
                                      struct nix_sockaddr_un const *in,
                                      struct nbsd101_sockaddr_un    *out)
{
	out->sun_len = sizeof(*out);
	out->sun_family = in->sun_family;
	strncpy(out->sun_path, in->sun_path, NIX_MIN(sizeof(in->sun_path), sizeof(out->sun_path)));
}

static void
nbsd101_sockaddr_un_to_nix_sockaddr_un(nix_endian_t                     endian,
                                      struct nbsd101_sockaddr_un const *in,
                                      struct nix_sockaddr_un          *out)
{
	LCAssert(g_bsd_log, in->sun_len == 0 || in->sun_len == sizeof(*in));
	out->sun_family = in->sun_family;
	strncpy(out->sun_path, in->sun_path, NIX_MIN(sizeof(in->sun_path), sizeof(out->sun_path)));
}

int
nix_sockaddr_to_nbsd101_sockaddr(nix_endian_t               endian,
                                struct nix_sockaddr const *in,
                                nix_socklen_t              inlen,
                                struct nbsd101_sockaddr    *out,
                                nbsd101_socklen_t          *outlen)
{
	switch (in->sa_family) {
	case NIX_AF_UNIX:
		LCAssert(g_bsd_log, inlen >= sizeof(struct nix_sockaddr_un));
		nix_sockaddr_un_to_nbsd101_sockaddr_un(endian,
		                                      (struct nix_sockaddr_un const *)in, (struct nbsd101_sockaddr_un *)out);
		*outlen = GE32(sizeof(struct nbsd101_sockaddr_un));
		return (1);

	case NIX_AF_INET:
		LCAssert(g_bsd_log, inlen >= sizeof(struct nix_sockaddr_in));
		nix_sockaddr_in_to_nbsd101_sockaddr_in(endian,
		                                      (struct nix_sockaddr_in const *)in, (struct nbsd101_sockaddr_in *)out);
		*outlen = GE32(sizeof(struct nbsd101_sockaddr_in));
		return (1);
	}

	return (0);
}

int
nbsd101_sockaddr_to_nix_sockaddr(nix_endian_t                  endian,
                                struct nbsd101_sockaddr const *in,
                                nbsd101_socklen_t              inlen,
                                struct nix_sockaddr          *out,
                                nix_socklen_t                *outlen)
{
	switch (in->sa_family) {
	case NIX_AF_UNIX:
		LCAssert(g_bsd_log, inlen >= (nbsd101_socklen_t)sizeof(struct nbsd101_sockaddr_un));
		LCAssert(g_bsd_log, *outlen >= (nix_socklen_t)sizeof(struct nix_sockaddr_un));
		nbsd101_sockaddr_un_to_nix_sockaddr_un(endian,
		                                      (struct nbsd101_sockaddr_un const *)in, (struct nix_sockaddr_un *)out);
		*outlen = sizeof(struct nix_sockaddr_un);
		return (1);

	case NIX_AF_INET:
		LCAssert(g_bsd_log, inlen >= (nbsd101_socklen_t)sizeof(struct nbsd101_sockaddr_in));
		LCAssert(g_bsd_log, *outlen >= (nix_socklen_t)sizeof(struct nix_sockaddr_in));
		nbsd101_sockaddr_in_to_nix_sockaddr_in(endian,
		                                      (struct nbsd101_sockaddr_in const *)in, (struct nix_sockaddr_in *)out);
		*outlen = sizeof(struct nix_sockaddr_in);
		return (1);
	}

	return (0);
}

void
nix_rusage_to_nbsd101_rusage(nix_endian_t             endian,
                            struct nix_rusage const *in,
                            struct nbsd101_rusage    *out)
{
	nix_timeval_to_nbsd101_timeval(endian, &in->ru_utime, &out->ru_utime);
	nix_timeval_to_nbsd101_timeval(endian, &in->ru_stime, &out->ru_stime);
	out->ru_maxrss = GELONG(in->ru_maxrss);
	out->ru_ixrss = GELONG(in->ru_ixrss);
	out->ru_idrss = GELONG(in->ru_idrss);
	out->ru_isrss = GELONG(in->ru_isrss);
	out->ru_minflt = GELONG(in->ru_minflt);
	out->ru_majflt = GELONG(in->ru_majflt);
	out->ru_nswap = GELONG(in->ru_nswap);
	out->ru_inblock = GELONG(in->ru_inblock);
	out->ru_oublock = GELONG(in->ru_oublock);
	out->ru_msgsnd = GELONG(in->ru_msgsnd);
	out->ru_msgrcv = GELONG(in->ru_msgrcv);
	out->ru_nsignals = GELONG(in->ru_nsignals);
	out->ru_nvcsw = GELONG(in->ru_nvcsw);
	out->ru_nivcsw = GELONG(in->ru_nivcsw);
}

void
nix_rlimit_to_nbsd101_rlimit(nix_endian_t             endian,
                            struct nix_rlimit const *in,
                            struct nbsd101_rlimit    *out)
{
	out->rlim_cur = GE64(in->rlim_cur);
	out->rlim_max = GE64(in->rlim_max);
}

static __inline int16_t
nix_poll_event_to_nbsd101_event(int events)
{
	int16_t oevents = 0;

	if (events & NIX_POLLIN)
		oevents |= NBSD101_POLLIN;
	if (events & NIX_POLLPRI)
		oevents |= NBSD101_POLLPRI;
	if (events & NIX_POLLOUT)
		oevents |= NBSD101_POLLOUT;
	if (events & NIX_POLLRDNORM)
		oevents |= NBSD101_POLLRDNORM;
	if (events & NIX_POLLRDBAND)
		oevents |= NBSD101_POLLRDBAND;
	if (events & NIX_POLLWRBAND)
		oevents |= NBSD101_POLLWRBAND;
	if (events & NIX_POLLERR)
		oevents |= NBSD101_POLLERR;
	if (events & NIX_POLLHUP)
		oevents |= NBSD101_POLLHUP;
	if (events & NIX_POLLNVAL)
		oevents |= NBSD101_POLLNVAL;

	return (oevents);
}

static __inline int
nbsd101_poll_event_to_nix_event(int16_t events)
{
	int xevents = 0;

	if (events & NBSD101_POLLIN)
		xevents |= NIX_POLLIN;
	if (events & NBSD101_POLLPRI)
		xevents |= NIX_POLLPRI;
	if (events & NBSD101_POLLOUT)
		xevents |= NIX_POLLOUT;
	if (events & NBSD101_POLLRDNORM)
		xevents |= NIX_POLLRDNORM;
	if (events & NBSD101_POLLRDBAND)
		xevents |= NIX_POLLRDBAND;
	if (events & NBSD101_POLLWRBAND)
		xevents |= NIX_POLLWRBAND;
	if (events & NBSD101_POLLERR)
		xevents |= NIX_POLLERR;
	if (events & NBSD101_POLLHUP)
		xevents |= NIX_POLLHUP;
	if (events & NBSD101_POLLNVAL)
		xevents |= NIX_POLLNVAL;

	return (xevents);
}

void
nbsd101_pollfd_to_nix_pollfd(nix_endian_t                endian,
                            struct nbsd101_pollfd const *in,
                            struct nix_pollfd          *out)
{
	out->fd = GE32(in->fd);
	out->events = nbsd101_poll_event_to_nix_event(GE16(in->events));
	out->revents = 0;
}

void
nix_pollfd_to_nbsd101_pollfd(nix_endian_t             endian,
                            struct nix_pollfd const *in,
                            struct nbsd101_pollfd    *out)
{
	out->revents = GE16(nix_poll_event_to_nbsd101_event(in->revents));
}

static __inline nix_tcflag_t
nbsd101_termios_iflag_to_nix_termios_iflag(nbsd101_tcflag_t flags)
{
	nix_tcflag_t oflags = 0;

	if (flags == 0)
		return (0);

	if (flags & NBSD101_IGNBRK)
		oflags |= NIX_IGNBRK;
	if (flags & NBSD101_BRKINT)
		oflags |= NIX_BRKINT;
	if (flags & NBSD101_PARMRK)
		oflags |= NIX_PARMRK;
	if (flags & NBSD101_INPCK)
		oflags |= NIX_INPCK;
	if (flags & NBSD101_ISTRIP)
		oflags |= NIX_ISTRIP;
	if (flags & NBSD101_INLCR)
		oflags |= NIX_INLCR;
	if (flags & NBSD101_IGNCR)
		oflags |= NIX_IGNCR;
	if (flags & NBSD101_ICRNL)
		oflags |= NIX_ICRNL;
	if (flags & NBSD101_IXON)
		oflags |= NIX_IXON;
	if (flags & NBSD101_IXOFF)
		oflags |= NIX_IXOFF;
	if (flags & NBSD101_IXANY)
		oflags |= NIX_IXANY;
	if (flags & NBSD101_IUCLC)
		oflags |= NIX_IUCLC;
	if (flags & NBSD101_IMAXBEL)
		oflags |= NIX_IMAXBEL;

	return (oflags);
}

static __inline nbsd101_tcflag_t
nix_termios_iflag_to_nbsd101_termios_iflag(nix_tcflag_t flags)
{
	nbsd101_tcflag_t oflags = 0;

	if (flags == 0)
		return (0);

	if (flags & NIX_IGNBRK)
		oflags |= NBSD101_IGNBRK;
	if (flags & NIX_BRKINT)
		oflags |= NBSD101_BRKINT;
	if (flags & NIX_PARMRK)
		oflags |= NBSD101_PARMRK;
	if (flags & NIX_INPCK)
		oflags |= NBSD101_INPCK;
	if (flags & NIX_ISTRIP)
		oflags |= NBSD101_ISTRIP;
	if (flags & NIX_INLCR)
		oflags |= NBSD101_INLCR;
	if (flags & NIX_IGNCR)
		oflags |= NBSD101_IGNCR;
	if (flags & NIX_ICRNL)
		oflags |= NBSD101_ICRNL;
	if (flags & NIX_IXON)
		oflags |= NBSD101_IXON;
	if (flags & NIX_IXOFF)
		oflags |= NBSD101_IXOFF;
	if (flags & NIX_IXANY)
		oflags |= NBSD101_IXANY;
	if (flags & NIX_IUCLC)
		oflags |= NBSD101_IUCLC;
	if (flags & NIX_IMAXBEL)
		oflags |= NBSD101_IMAXBEL;

	return (oflags);
}

static __inline nix_tcflag_t
nbsd101_termios_oflag_to_nix_termios_oflag(nbsd101_tcflag_t flags)
{
	nix_tcflag_t oflags = 0;

	if (flags == 0)
		return (0);

	if (flags & NBSD101_OPOST)
		oflags |= NIX_OPOST;
	if (flags & NBSD101_ONLCR)
		oflags |= NIX_ONLCR;
	if (flags & NBSD101_OXTABS)
		oflags |= NIX_OXTABS;
	if (flags & NBSD101_ONOEOT)
		oflags |= NIX_ONOEOT;
	if (flags & NBSD101_OCRNL)
		oflags |= NIX_OCRNL;
	if (flags & NBSD101_OLCUC)
		oflags |= NIX_OLCUC;
	if (flags & NBSD101_ONOCR)
		oflags |= NIX_ONOCR;
	if (flags & NBSD101_ONLRET)
		oflags |= NIX_ONLRET;

	return (oflags);
}

static __inline nbsd101_tcflag_t
nix_termios_oflag_to_nbsd101_termios_oflag(nix_tcflag_t flags)
{
	nbsd101_tcflag_t oflags = 0;

	if (flags == 0)
		return (0);

	if (flags & NIX_OPOST)
		oflags |= NBSD101_OPOST;
	if (flags & NIX_ONLCR)
		oflags |= NBSD101_ONLCR;
	if (flags & NIX_OXTABS)
		oflags |= NBSD101_OXTABS;
	if (flags & NIX_ONOEOT)
		oflags |= NBSD101_ONOEOT;
	if (flags & NIX_OCRNL)
		oflags |= NBSD101_OCRNL;
	if (flags & NIX_OLCUC)
		oflags |= NBSD101_OLCUC;
	if (flags & NIX_ONOCR)
		oflags |= NBSD101_ONOCR;
	if (flags & NIX_ONLRET)
		oflags |= NBSD101_ONLRET;

	return (oflags);
}

static __inline nix_tcflag_t
nbsd101_termios_cflag_to_nix_termios_cflag(nbsd101_tcflag_t flags)
{
	nix_tcflag_t oflags = 0;

	if (flags == 0)
		return (0);

	switch (flags & NBSD101_CSIZE) {
	case NBSD101_CS5:
		oflags |= NIX_CS5;
		break;
	case NBSD101_CS6:
		oflags |= NIX_CS6;
		break;
	case NBSD101_CS7:
		oflags |= NIX_CS7;
		break;
	case NBSD101_CS8:
		oflags |= NIX_CS8;
		break;
	}
	if (flags & NBSD101_CIGNORE)
		oflags |= NIX_CIGNORE;
	if (flags & NBSD101_CSTOPB)
		oflags |= NIX_CSTOPB;
	if (flags & NBSD101_CREAD)
		oflags |= NIX_CREAD;
	if (flags & NBSD101_PARENB)
		oflags |= NIX_PARENB;
	if (flags & NBSD101_PARODD)
		oflags |= NIX_PARODD;
	if (flags & NBSD101_HUPCL)
		oflags |= NIX_HUPCL;
	if (flags & NBSD101_CLOCAL)
		oflags |= NIX_CLOCAL;
	if (flags & NBSD101_CRTSCTS)
		oflags |= NIX_CRTSCTS;
	if (flags & NBSD101_MDMBUF)
		oflags |= NIX_MDMBUF;

	return (oflags);
}

static __inline nbsd101_tcflag_t
nix_termios_cflag_to_nbsd101_termios_cflag(nix_tcflag_t flags)
{
	nbsd101_tcflag_t oflags = 0;

	if (flags == 0)
		return (0);

	switch (flags & NIX_CSIZE) {
	case NIX_CS5:
		oflags |= NBSD101_CS5;
		break;
	case NIX_CS6:
		oflags |= NBSD101_CS6;
		break;
	case NIX_CS7:
		oflags |= NBSD101_CS7;
		break;
	case NIX_CS8:
		oflags |= NBSD101_CS8;
		break;
	}
	if (flags & NIX_CIGNORE)
		oflags |= NBSD101_CIGNORE;
	if (flags & NIX_CSTOPB)
		oflags |= NBSD101_CSTOPB;
	if (flags & NIX_CREAD)
		oflags |= NBSD101_CREAD;
	if (flags & NIX_PARENB)
		oflags |= NBSD101_PARENB;
	if (flags & NIX_PARODD)
		oflags |= NBSD101_PARODD;
	if (flags & NIX_HUPCL)
		oflags |= NBSD101_HUPCL;
	if (flags & NIX_CLOCAL)
		oflags |= NBSD101_CLOCAL;
	if (flags & NIX_CRTSCTS)
		oflags |= NBSD101_CRTSCTS;
	if (flags & NIX_MDMBUF)
		oflags |= NBSD101_MDMBUF;

	return (oflags);
}

static __inline nix_tcflag_t
nbsd101_termios_lflag_to_nix_termios_lflag(nbsd101_tcflag_t flags)
{
	nix_tcflag_t oflags = 0;

	if (flags == 0)
		return (0);

	if (flags & NBSD101_ECHOKE)
		oflags |= NIX_ECHOKE;
	if (flags & NBSD101_ECHOE)
		oflags |= NIX_ECHOE;
	if (flags & NBSD101_ECHOK)
		oflags |= NIX_ECHOK;
	if (flags & NBSD101_ECHO)
		oflags |= NIX_ECHO;
	if (flags & NBSD101_ECHONL)
		oflags |= NIX_ECHONL;
	if (flags & NBSD101_ECHOPRT)
		oflags |= NIX_ECHOPRT;
	if (flags & NBSD101_ECHOCTL)
		oflags |= NIX_ECHOCTL;
	if (flags & NBSD101_ISIG)
		oflags |= NIX_ISIG;
	if (flags & NBSD101_ICANON)
		oflags |= NIX_ICANON;
	if (flags & NBSD101_ALTWERASE)
		oflags |= NIX_ALTWERASE;
	if (flags & NBSD101_IEXTEN)
		oflags |= NIX_IEXTEN;
	if (flags & NBSD101_EXTPROC)
		oflags |= NIX_EXTPROC;
	if (flags & NBSD101_TOSTOP)
		oflags |= NIX_TOSTOP;
	if (flags & NBSD101_FLUSHO)
		oflags |= NIX_FLUSHO;
	if (flags & NBSD101_XCASE)
		oflags |= NIX_XCASE;
	if (flags & NBSD101_NOKERNINFO)
		oflags |= NIX_NOKERNINFO;
	if (flags & NBSD101_PENDIN)
		oflags |= NIX_PENDIN;
	if (flags & NBSD101_NOFLSH)
		oflags |= NIX_NOFLSH;

	return (oflags);
}

static __inline nbsd101_tcflag_t
nix_termios_lflag_to_nbsd101_termios_lflag(nix_tcflag_t flags)
{
	nbsd101_tcflag_t oflags = 0;

	if (flags == 0)
		return (0);

	if (flags & NIX_ECHOKE)
		oflags |= NBSD101_ECHOKE;
	if (flags & NIX_ECHOE)
		oflags |= NBSD101_ECHOE;
	if (flags & NIX_ECHOK)
		oflags |= NBSD101_ECHOK;
	if (flags & NIX_ECHO)
		oflags |= NBSD101_ECHO;
	if (flags & NIX_ECHONL)
		oflags |= NBSD101_ECHONL;
	if (flags & NIX_ECHOPRT)
		oflags |= NBSD101_ECHOPRT;
	if (flags & NIX_ECHOCTL)
		oflags |= NBSD101_ECHOCTL;
	if (flags & NIX_ISIG)
		oflags |= NBSD101_ISIG;
	if (flags & NIX_ICANON)
		oflags |= NBSD101_ICANON;
	if (flags & NIX_ALTWERASE)
		oflags |= NBSD101_ALTWERASE;
	if (flags & NIX_IEXTEN)
		oflags |= NBSD101_IEXTEN;
	if (flags & NIX_EXTPROC)
		oflags |= NBSD101_EXTPROC;
	if (flags & NIX_TOSTOP)
		oflags |= NBSD101_TOSTOP;
	if (flags & NIX_FLUSHO)
		oflags |= NBSD101_FLUSHO;
	if (flags & NIX_XCASE)
		oflags |= NBSD101_XCASE;
	if (flags & NIX_NOKERNINFO)
		oflags |= NBSD101_NOKERNINFO;
	if (flags & NIX_PENDIN)
		oflags |= NBSD101_PENDIN;
	if (flags & NIX_NOFLSH)
		oflags |= NBSD101_NOFLSH;

	return (oflags);
}

static __inline void
nbsd101_termios_cc_to_nix_termios_cc(nbsd101_cc_t const *in,
                                    nix_cc_t          *out)
{
	out[NIX_VEOF] = in[NBSD101_VEOF];
	out[NIX_VEOL] = in[NBSD101_VEOL];
	out[NIX_VEOL2] = in[NBSD101_VEOL2];
	out[NIX_VERASE] = in[NBSD101_VERASE];
	out[NIX_VWERASE] = in[NBSD101_VWERASE];
	out[NIX_VKILL] = in[NBSD101_VKILL];
	out[NIX_VREPRINT] = in[NBSD101_VREPRINT];
	out[NIX_VINTR] = in[NBSD101_VINTR];
	out[NIX_VQUIT] = in[NBSD101_VQUIT];
	out[NIX_VSUSP] = in[NBSD101_VSUSP];
	out[NIX_VDSUSP] = in[NBSD101_VDSUSP];
	out[NIX_VSTART] = in[NBSD101_VSTART];
	out[NIX_VSTOP] = in[NBSD101_VSTOP];
	out[NIX_VLNEXT] = in[NBSD101_VLNEXT];
	out[NIX_VDISCARD] = in[NBSD101_VDISCARD];
	out[NIX_VMIN] = in[NBSD101_VMIN];
	out[NIX_VTIME] = in[NBSD101_VTIME];
	out[NIX_VSTATUS] = in[NBSD101_VSTATUS];
}

static __inline void
nix_termios_cc_to_nbsd101_termios_cc(nix_cc_t const *in,
                                    nbsd101_cc_t    *out)
{
	out[NBSD101_VEOF] = in[NIX_VEOF];
	out[NBSD101_VEOL] = in[NIX_VEOL];
	out[NBSD101_VEOL2] = in[NIX_VEOL2];
	out[NBSD101_VERASE] = in[NIX_VERASE];
	out[NBSD101_VWERASE] = in[NIX_VWERASE];
	out[NBSD101_VKILL] = in[NIX_VKILL];
	out[NBSD101_VREPRINT] = in[NIX_VREPRINT];
	out[NBSD101_VINTR] = in[NIX_VINTR];
	out[NBSD101_VQUIT] = in[NIX_VQUIT];
	out[NBSD101_VSUSP] = in[NIX_VSUSP];
	out[NBSD101_VDSUSP] = in[NIX_VDSUSP];
	out[NBSD101_VSTART] = in[NIX_VSTART];
	out[NBSD101_VSTOP] = in[NIX_VSTOP];
	out[NBSD101_VLNEXT] = in[NIX_VLNEXT];
	out[NBSD101_VDISCARD] = in[NIX_VDISCARD];
	out[NBSD101_VMIN] = in[NIX_VMIN];
	out[NBSD101_VTIME] = in[NIX_VTIME];
	out[NBSD101_VSTATUS] = in[NIX_VSTATUS];
}

static __inline nix_speed_t
nbsd101_termios_speed_to_nix_termios_speed(nbsd101_speed_t speed)
{
	return (speed);
}

static __inline nix_speed_t
nix_termios_speed_to_nbsd101_termios_speed(nbsd101_speed_t speed)
{
	return (speed);
}

void
nbsd101_termios_to_nix_termios(nix_endian_t                 endian,
                              struct nbsd101_termios const *in,
                              struct nix_termios          *out)
{
	out->c_iflag = nbsd101_termios_iflag_to_nix_termios_iflag(GE32(in->c_iflag));
	out->c_oflag = nbsd101_termios_oflag_to_nix_termios_oflag(GE32(in->c_oflag));
	out->c_cflag = nbsd101_termios_cflag_to_nix_termios_cflag(GE32(in->c_cflag));
	out->c_lflag = nbsd101_termios_lflag_to_nix_termios_lflag(GE32(in->c_lflag));
	out->c_ispeed = nbsd101_termios_speed_to_nix_termios_speed(GE32(in->c_ispeed));
	out->c_ospeed = nbsd101_termios_speed_to_nix_termios_speed(GE32(in->c_ospeed));
	nbsd101_termios_cc_to_nix_termios_cc(in->c_cc, out->c_cc);
}

void
nix_termios_to_nbsd101_termios(nix_endian_t              endian,
                              struct nix_termios const *in,
                              struct nbsd101_termios    *out)
{
	out->c_iflag = GE32(nix_termios_iflag_to_nbsd101_termios_iflag(in->c_iflag));
	out->c_oflag = GE32(nix_termios_oflag_to_nbsd101_termios_oflag(in->c_oflag));
	out->c_cflag = GE32(nix_termios_cflag_to_nbsd101_termios_cflag(in->c_cflag));
	out->c_lflag = GE32(nix_termios_lflag_to_nbsd101_termios_lflag(in->c_lflag));
	out->c_ispeed = GE32(nix_termios_speed_to_nbsd101_termios_speed(in->c_ispeed));
	out->c_ospeed = GE32(nix_termios_speed_to_nbsd101_termios_speed(in->c_ospeed));
	nbsd101_termios_cc_to_nix_termios_cc(in->c_cc, out->c_cc);
}

void
nbsd101_fd_set_to_nix_fd_set(nix_endian_t         endian,
                            nbsd101_fd_set const *in,
                            nix_fd_set          *out)
{
	size_t n;

	for (n = 0; n < sizeof(*in) / sizeof(nbsd101_fd_mask_t) &&
	            n < sizeof(*out) / sizeof(nix_fd_mask_t);
	     n++) {
		out->fds_bits[n] = GE32(in->fds_bits[n]);
	}
}

void
nix_fd_set_to_nbsd101_fd_set(nix_endian_t      endian,
                            nix_fd_set const *in,
                            nbsd101_fd_set    *out)
{
	size_t n;

	for (n = 0; n < sizeof(*in) / sizeof(nix_fd_mask_t) &&
	            n < sizeof(*out) / sizeof(nbsd101_fd_mask_t);
	     n++) {
		out->fds_bits[n] = GE32(in->fds_bits[n]);
	}
}
