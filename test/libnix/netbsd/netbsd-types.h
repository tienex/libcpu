#ifndef __netbsd_types_h
#define __netbsd_types_h

#include "nix-host.h"
#include "netbsd-guest-types.h"

typedef int32_t  netbsd_dev_t;
typedef uint32_t netbsd_ino_t;
typedef uint32_t netbsd_mode_t;
typedef uint32_t netbsd_nlink_t;
typedef uint32_t netbsd_uid_t;
typedef uint32_t netbsd_gid_t;

typedef int64_t netbsd_off_t;

typedef struct
  {
    int32_t val[2];
  } __netbsd_guest_alignment netbsd_fsid_t;

struct netbsd_timespec
  {
    netbsd_time_t tv_sec;
    netbsd_long_t tv_nsec;
  } __netbsd_guest_alignment;

struct netbsd_timeval
  {
    netbsd_time_t tv_sec;
    netbsd_long_t tv_usec;
  } __netbsd_guest_alignment;

struct netbsd_timezone
  {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
  } __netbsd_guest_alignment;

struct netbsd_stat
  {
    netbsd_dev_t           st_dev;
    netbsd_ino_t           st_ino;
    netbsd_mode_t          st_mode;
    netbsd_nlink_t         st_nlink;
    netbsd_uid_t           st_uid;
    netbsd_gid_t           st_gid;
    netbsd_dev_t           st_rdev;
    int32_t                st_lspare0;
    struct netbsd_timespec st_atimespec;
    struct netbsd_timespec st_mtimespec;
    struct netbsd_timespec st_ctimespec;
    netbsd_off_t           st_size;
    int64_t                st_blocks;
    uint32_t               st_blksize;
    uint32_t               st_flags;
    uint32_t               st_gen;
    int32_t                st_lspare1;
    struct netbsd_timespec __st_birthtimespec;
    int64_t                st_qspare[2];
  } __netbsd_guest_alignment;

union netbsd_mount_info
  {
    char __align[160];
  } __netbsd_guest_alignment;

#define NETBSD_MNT_WAIT     1
#define NETBSD_MNT_NOWAIT   2

#define NETBSD_MFSNAMELEN   16
#define NETBSD_MNAMELEN     90

struct netbsd_statfs
  {
    uint32_t                f_flags;
    int32_t                 f_bsize;
    uint32_t                f_iosize;
    uint32_t                f_blocks;
    uint32_t                f_bfree;
    int32_t                 f_bavail;
    uint32_t                f_files;
    uint32_t                f_ffree;
    netbsd_fsid_t           f_fsid;
    netbsd_uid_t            f_owner;
    uint32_t                f_syncwrites;
    uint32_t                f_asyncwrites;
    uint32_t                f_ctime;
    uint32_t                f_spare[3];
    char                    f_fstypename[NETBSD_MFSNAMELEN];
    char                    f_mntonname[NETBSD_MNAMELEN];
    char                    f_mntfromname[NETBSD_MNAMELEN];
    union netbsd_mount_info mount_info;
  } __netbsd_guest_alignment;

struct netbsd_iovec32
  {
    uint32_t iov_base;
    uint32_t iov_len;
  } __netbsd_guest_alignment;

struct netbsd_sigaction32
  {
    uint32_t __sa_handler;
    uint32_t sa_flags;
    uint32_t sa_mask;
  } __netbsd_guest_alignment;

typedef uint32_t netbsd_sigset_t;
#define NETBSD_SIG_BLOCK   1
#define NETBSD_SIG_UNBLOCK 2
#define NETBSD_SIG_SETMASK 3

typedef int32_t  netbsd_socklen_t;
typedef uint8_t  netbsd_sa_family_t;
typedef uint16_t netbsd_in_port_t;
typedef uint32_t netbsd_in_addr_t;

struct netbsd_sockaddr
  {
    uint8_t            sa_len;
    netbsd_sa_family_t sa_family;
    char               sa_data[14];
  } __netbsd_guest_alignment;

struct netbsd_sockaddr_storage
  {
    uint8_t            ss_len;
    netbsd_sa_family_t ss_family;
    uint8_t            __ss_pad1[6];
    uint64_t           __ss_pad2;
    uint8_t            __ss_pad3[240];
  } __netbsd_guest_alignment;

struct netbsd_sockaddr_in
  {
    uint8_t            sin_len;
    netbsd_sa_family_t sin_family;
    netbsd_in_port_t   sin_port;
    uint32_t           sin_addr;
    int8_t             sin_zero[8];
  } __netbsd_guest_alignment;

struct netbsd_sockaddr_un
  {
    uint8_t            sun_len;
    netbsd_sa_family_t sun_family;
    char               sun_path[104];
  } __netbsd_guest_alignment;

#define NETBSD_RUSAGE_SELF     (0)
#define NETBSD_RUSAGE_CHILDREN (-1)

struct netbsd_rusage
  {
    struct netbsd_timeval ru_utime;
    struct netbsd_timeval ru_stime;
    netbsd_long_t         ru_maxrss;
    netbsd_long_t         ru_ixrss;
    netbsd_long_t         ru_idrss;
    netbsd_long_t         ru_isrss;
    netbsd_long_t         ru_minflt;
    netbsd_long_t         ru_majflt;
    netbsd_long_t         ru_nswap;
    netbsd_long_t         ru_inblock;
    netbsd_long_t         ru_oublock;
    netbsd_long_t         ru_msgsnd;
    netbsd_long_t         ru_msgrcv;
    netbsd_long_t         ru_nsignals;
    netbsd_long_t         ru_nvcsw;
    netbsd_long_t         ru_nivcsw;
  } __netbsd_guest_alignment;

typedef uint64_t netbsd_rlim_t;

#define NETBSD_RLIMIT_CPU     0 
#define NETBSD_RLIMIT_FSIZE   1
#define NETBSD_RLIMIT_DATA    2
#define NETBSD_RLIMIT_STACK   3
#define NETBSD_RLIMIT_CORE    4
#define NETBSD_RLIMIT_RSS     5
#define NETBSD_RLIMIT_MEMLOCK 6
#define NETBSD_RLIMIT_NPROC   7
#define NETBSD_RLIMIT_NOFILE  8

struct netbsd_rlimit
  {
    netbsd_rlim_t rlim_cur;
    netbsd_rlim_t rlim_max;
  } __netbsd_guest_alignment;

#define NETBSD_POLLIN      0x0001
#define NETBSD_POLLPRI     0x0002
#define NETBSD_POLLOUT     0x0004
#define NETBSD_POLLERR     0x0008
#define NETBSD_POLLHUP     0x0010
#define NETBSD_POLLNVAL    0x0020
#define NETBSD_POLLRDNORM  0x0040
#define NETBSD_POLLRDBAND  0x0080
#define NETBSD_POLLWRBAND  0x0100

struct netbsd_pollfd
  {
    int32_t fd;
    int16_t events;
    int16_t revents;
  } __netbsd_guest_alignment;

/* Special Control Characters */
#define NETBSD_VEOF     0
#define NETBSD_VEOL     1
#define NETBSD_VEOL2    2
#define NETBSD_VERASE   3
#define NETBSD_VWERASE  4
#define NETBSD_VKILL    5
#define NETBSD_VREPRINT 6
#define NETBSD_VINTR    8
#define NETBSD_VQUIT    9
#define NETBSD_VSUSP    10
#define NETBSD_VDSUSP   11
#define NETBSD_VSTART   12
#define NETBSD_VSTOP    13
#define NETBSD_VLNEXT   14
#define NETBSD_VDISCARD 15
#define NETBSD_VMIN     16
#define NETBSD_VTIME    17
#define NETBSD_VSTATUS  18

#define NETBSD_NCCS     20

/* Input flags */
#define NETBSD_IGNBRK   0x00000001
#define NETBSD_BRKINT   0x00000002
#define NETBSD_IGNPAR   0x00000004
#define NETBSD_PARMRK   0x00000008
#define NETBSD_INPCK    0x00000010
#define NETBSD_ISTRIP   0x00000020
#define NETBSD_INLCR    0x00000040
#define NETBSD_IGNCR    0x00000080
#define NETBSD_ICRNL    0x00000100
#define NETBSD_IXON     0x00000200
#define NETBSD_IXOFF    0x00000400
#define NETBSD_IXANY    0x00000800
#define NETBSD_IUCLC    0x00001000
#define NETBSD_IMAXBEL  0x00002000

/* Output Flags */
#define NETBSD_OPOST    0x00000001
#define NETBSD_ONLCR    0x00000002
#define NETBSD_OXTABS   0x00000004
#define NETBSD_ONOEOT   0x00000008
#define NETBSD_OCRNL    0x00000010
#define NETBSD_OLCUC    0x00000020
#define NETBSD_ONOCR    0x00000040
#define NETBSD_ONLRET   0x00000080

/* Control Flags */
#define NETBSD_CIGNORE  0x00000001
#define NETBSD_CSIZE    0x00000300
#define NETBSD_CS5      0x00000000
#define NETBSD_CS6      0x00000100
#define NETBSD_CS7      0x00000200
#define NETBSD_CS8      0x00000300
#define NETBSD_CSTOPB   0x00000400
#define NETBSD_CREAD    0x00000800
#define NETBSD_PARENB   0x00001000
#define NETBSD_PARODD   0x00002000
#define NETBSD_HUPCL    0x00004000
#define NETBSD_CLOCAL   0x00008000
#define NETBSD_CRTSCTS  0x00010000
#define NETBSD_MDMBUF   0x00100000
#define NETBSD_CHWFLOW  (NETBSD_MDMBUF | NETBSD_CRTSCTS)

/* Local Flags */
#define NETBSD_ECHOKE     0x00000001
#define NETBSD_ECHOE      0x00000002
#define NETBSD_ECHOK      0x00000004
#define NETBSD_ECHO       0x00000008
#define NETBSD_ECHONL     0x00000010
#define NETBSD_ECHOPRT    0x00000020
#define NETBSD_ECHOCTL    0x00000040
#define NETBSD_ISIG       0x00000080
#define NETBSD_ICANON     0x00000100
#define NETBSD_ALTWERASE  0x00000200
#define NETBSD_IEXTEN     0x00000400
#define NETBSD_EXTPROC    0x00000800
#define NETBSD_TOSTOP     0x00400000
#define NETBSD_FLUSHO     0x00800000
#define NETBSD_XCASE      0x01000000
#define NETBSD_NOKERNINFO 0x02000000
#define NETBSD_PENDIN     0x20000000
#define NETBSD_NOFLSH     0x80000000

/* Standard speeds */
#define NETBSD_B0      0
#define NETBSD_B50     50
#define NETBSD_B75     75
#define NETBSD_B110    110
#define NETBSD_B134    134
#define NETBSD_B150    150
#define NETBSD_B200    200
#define NETBSD_B300    300
#define NETBSD_B600    600
#define NETBSD_B1200   1200
#define NETBSD_B1800   1800
#define NETBSD_B2400   2400
#define NETBSD_B4800   4800
#define NETBSD_B7200   7200
#define NETBSD_B9600   9600
#define NETBSD_B14400  14400
#define NETBSD_B19200  19200
#define NETBSD_B28800  28800
#define NETBSD_B38400  38400
#define NETBSD_B57600  57600
#define NETBSD_B76800  76800
#define NETBSD_B115200 115200
#define NETBSD_B230400 230400
#define NETBSD_EXTA    19200
#define NETBSD_EXTB    38400

typedef uint32_t netbsd_tcflag_t;
typedef uint32_t netbsd_speed_t;
typedef uint8_t  netbsd_cc_t;

struct netbsd_termios
  {
    netbsd_tcflag_t c_iflag;
    netbsd_tcflag_t c_oflag;
    netbsd_tcflag_t c_cflag;
    netbsd_tcflag_t c_lflag;
    netbsd_cc_t     c_cc[NETBSD_NCCS];
    netbsd_speed_t  c_ispeed;
    netbsd_speed_t  c_ospeed;
  };

typedef uint32_t netbsd_fd_mask_t;

typedef struct netbsd_fd_set
  {
    netbsd_fd_mask_t fds_bits[256 >> 3]; /* NETBSD_FD_SETSIZE */
  } netbsd_fd_set;

#endif  /* !__netbsd_types_h */
