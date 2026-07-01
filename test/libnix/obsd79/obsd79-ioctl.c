#include <errno.h>

#include "openbsd79.h"

#include "LibCPU/LcLog.h"
#include "nix-byte-order.h"

extern void *g_bsd_log;

#define GE16(x) ((endian) != NIX_ENDIAN_NATIVE ? nix_byte_swap_int16(x) : (x))
#define GE32(x) ((endian) != NIX_ENDIAN_NATIVE ? nix_byte_swap_int32(x) : (x))
#define GE64(x) ((endian) != NIX_ENDIAN_NATIVE ? nix_byte_swap_int64(x) : (x))

#define GELONG(x) ((sizeof(obsd79_long_t) == sizeof(uint64_t)) ? GE64(x) : GE32(x))

int
obsd79_ioctl_tty(nix_env_t       *env,
                 nix_endian_t     endian,
                 int              fd,
                 obsd79_ulong_t   request,
                 obsd79_uintptr_t arg)
{
	nix_mem_if_t *mem = nix_env_get_memory(env);

	switch (request) {
	case OBSD79_TIOCGETA: {
		struct nix_termios     ios;
		struct obsd79_termios *oios;
		nix_memflg_t           mf = 0;

		/* arg is a pointer in the guest addres space. */
		oios = (struct obsd79_termios *)nix_mem_gtoh(mem, arg, &mf);
		if (mf != 0) {
			nix_env_set_errno(env, EFAULT);
			return (-1);
		}

		if (nix_ioctl(fd, NIX_TIOCGETA, &ios, env) != 0)
			return (-1);

		__nix_try
		{
			nix_termios_to_obsd79_termios(endian, &ios, oios);
		}
		__nix_catch_any
		{
			nix_env_set_errno(env, EFAULT);
			return (-1);
		}
		__nix_end_try

		    return (0);
	}

	case OBSD79_TIOCSETA:
	case OBSD79_TIOCSETAW:
	case OBSD79_TIOCSETAF: {
		struct nix_termios     ios;
		struct obsd79_termios *oios;
		int                    req = 0;
		nix_memflg_t           mf = 0;

		switch (request) {
		case OBSD79_TIOCSETA:
			req = NIX_TIOCSETA;
			break;
		case OBSD79_TIOCSETAW:
			req = NIX_TIOCSETAW;
			break;
		case OBSD79_TIOCSETAF:
			req = NIX_TIOCSETAF;
			break;
		}

		/* arg is a pointer in the guest addres space. */
		oios = (struct obsd79_termios *)nix_mem_gtoh(mem, arg, &mf);
		if (mf != 0) {
			nix_env_set_errno(env, EFAULT);
			return (-1);
		}

		__nix_try
		{
			obsd79_termios_to_nix_termios(endian, oios, &ios);
		}
		__nix_catch_any
		{
			nix_env_set_errno(env, EFAULT);
			return (-1);
		}
		__nix_end_try

		    return (nix_ioctl(fd, req, &ios, env));
	}

	default:
		break;
	}

	nix_env_set_errno(env, EINVAL);
	return (-1);
}

int
obsd79_ioctl_file(nix_env_t       *env,
                  nix_endian_t     endian,
                  int              fd,
                  obsd79_ulong_t   request,
                  obsd79_uintptr_t arg)
{
	nix_mem_if_t *mem = nix_env_get_memory(env);

	switch (request) {
	case OBSD79_FIONREAD: {
		size_t       bytes;
		uint32_t    *obytes;
		nix_memflg_t mf = 0;

		/* arg is a pointer in the guest addres space. */
		obytes = (uint32_t *)nix_mem_gtoh(mem, arg, &mf);
		if (mf != 0) {
			nix_env_set_errno(env, EFAULT);
			return (-1);
		}

		if (nix_ioctl(fd, NIX_FIONREAD, &bytes, env) < 0)
			return (-1);

		__nix_try
		{
			*obytes = GE32(bytes);
		}
		__nix_catch_any
		{
			nix_env_set_errno(env, EFAULT);
			return (-1);
		}
		__nix_end_try

		    return (0);
	}

	case OBSD79_TIOCSETA:
	case OBSD79_TIOCSETAW:
	case OBSD79_TIOCSETAF: {
		struct nix_termios     ios;
		struct obsd79_termios *oios;
		int                    req = 0;
		nix_memflg_t           mf = 0;

		switch (request) {
		case OBSD79_TIOCSETA:
			req = NIX_TIOCSETA;
			break;
		case OBSD79_TIOCSETAW:
			req = NIX_TIOCSETAW;
			break;
		case OBSD79_TIOCSETAF:
			req = NIX_TIOCSETAF;
			break;
		}

		/* arg is a pointer in the guest addres space. */
		oios = (struct obsd79_termios *)nix_mem_gtoh(mem, arg, &mf);
		if (mf != 0) {
			nix_env_set_errno(env, EFAULT);
			return (-1);
		}

		__nix_try
		{
			obsd79_termios_to_nix_termios(endian, oios, &ios);
		}
		__nix_catch_any
		{
			nix_env_set_errno(env, EFAULT);
			return (-1);
		}
		__nix_end_try

		    return (nix_ioctl(fd, req, &ios, env));
	}

	default:
		break;
	}

	nix_env_set_errno(env, EINVAL);
	return (-1);
}

int
obsd79_ioctl_dispatch(nix_env_t       *env,
                      nix_endian_t     endian,
                      int              fd,
                      obsd79_ulong_t   request,
                      obsd79_uintptr_t arg)
{
	LCLog(g_bsd_log, LCLogDebug, 0,
	      "group='%c' command=%u length=%u [%c%c%c]\n",
	      OBSD79_IOCGROUP(request),
	      request & 0xff,
	      OBSD79_IOCPARM_LEN(request),
	      (request & OBSD79_IOC_VOID) ? 'v' : '-',
	      (request & OBSD79_IOC_IN) ? 'i' : '-',
	      (request & OBSD79_IOC_OUT) ? 'o' : '-');

	nix_env_set_errno(env, 0);
	switch (OBSD79_IOCGROUP(request)) {
	case 't':
		return (obsd79_ioctl_tty(env, endian, fd, request, arg));
	case 'f':
		return (obsd79_ioctl_file(env, endian, fd, request, arg));
	}

	return (0); // XXX

	nix_env_set_errno(env, EINVAL);
	return (-1);
}
