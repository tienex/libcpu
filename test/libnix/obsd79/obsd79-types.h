#ifndef __obsd79_types_h
#define __obsd79_types_h

#include "nix-host.h"
#include "obsd79-guest-types.h"

typedef int32_t  obsd79_dev_t;
typedef uint32_t obsd79_ino_t;
typedef uint32_t obsd79_mode_t;
typedef uint32_t obsd79_nlink_t;
typedef uint32_t obsd79_uid_t;
typedef uint32_t obsd79_gid_t;

typedef int64_t obsd79_off_t;

typedef struct
  {
    int32_t val[2];
  } __obsd79_guest_alignment obsd79_fsid_t;

struct obsd79_timespec
  {
    obsd79_time_t tv_sec;
    obsd79_long_t tv_nsec;
  } __obsd79_guest_alignment;

struct obsd79_timeval
  {
    obsd79_time_t tv_sec;
    obsd79_long_t tv_usec;
  } __obsd79_guest_alignment;

struct obsd79_timezone
  {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
  } __obsd79_guest_alignment;

struct obsd79_stat
  {
    obsd79_dev_t           st_dev;
    obsd79_ino_t           st_ino;
    obsd79_mode_t          st_mode;
    obsd79_nlink_t         st_nlink;
    obsd79_uid_t           st_uid;
    obsd79_gid_t           st_gid;
    obsd79_dev_t           st_rdev;
    int32_t                st_lspare0;
    struct obsd79_timespec st_atimespec;
    struct obsd79_timespec st_mtimespec;
    struct obsd79_timespec st_ctimespec;
    obsd79_off_t           st_size;
    int64_t                st_blocks;
    uint32_t               st_blksize;
    uint32_t               st_flags;
    uint32_t               st_gen;
    int32_t                st_lspare1;
    struct obsd79_timespec __st_birthtimespec;
    int64_t                st_qspare[2];
  } __obsd79_guest_alignment;

union obsd79_mount_info
  {
    char __align[160];
  } __obsd79_guest_alignment;

#define OBSD79_MNT_WAIT     1
#define OBSD79_MNT_NOWAIT   2

#define OBSD79_MFSNAMELEN   16
#define OBSD79_MNAMELEN     90

struct obsd79_statfs
  {
    uint32_t                f_flags;
    int32_t                 f_bsize;
    uint32_t                f_iosize;
    uint32_t                f_blocks;
    uint32_t                f_bfree;
    int32_t                 f_bavail;
    uint32_t                f_files;
    uint32_t                f_ffree;
    obsd79_fsid_t           f_fsid;
    obsd79_uid_t            f_owner;
    uint32_t                f_syncwrites;
    uint32_t                f_asyncwrites;
    uint32_t                f_ctime;
    uint32_t                f_spare[3];
    char                    f_fstypename[OBSD79_MFSNAMELEN];
    char                    f_mntonname[OBSD79_MNAMELEN];
    char                    f_mntfromname[OBSD79_MNAMELEN];
    union obsd79_mount_info mount_info;
  } __obsd79_guest_alignment;

struct obsd79_iovec32
  {
    uint32_t iov_base;
    uint32_t iov_len;
  } __obsd79_guest_alignment;

struct obsd79_sigaction32
  {
    uint32_t __sa_handler;
    uint32_t sa_flags;
    uint32_t sa_mask;
  } __obsd79_guest_alignment;

typedef uint32_t obsd79_sigset_t;
#define OBSD79_SIG_BLOCK   1
#define OBSD79_SIG_UNBLOCK 2
#define OBSD79_SIG_SETMASK 3

typedef int32_t  obsd79_socklen_t;
typedef uint8_t  obsd79_sa_family_t;
typedef uint16_t obsd79_in_port_t;
typedef uint32_t obsd79_in_addr_t;

struct obsd79_sockaddr
  {
    uint8_t            sa_len;
    obsd79_sa_family_t sa_family;
    char               sa_data[14];
  } __obsd79_guest_alignment;

struct obsd79_sockaddr_storage
  {
    uint8_t            ss_len;
    obsd79_sa_family_t ss_family;
    uint8_t            __ss_pad1[6];
    uint64_t           __ss_pad2;
    uint8_t            __ss_pad3[240];
  } __obsd79_guest_alignment;

struct obsd79_sockaddr_in
  {
    uint8_t            sin_len;
    obsd79_sa_family_t sin_family;
    obsd79_in_port_t   sin_port;
    uint32_t           sin_addr;
    int8_t             sin_zero[8];
  } __obsd79_guest_alignment;

struct obsd79_sockaddr_un
  {
    uint8_t            sun_len;
    obsd79_sa_family_t sun_family;
    char               sun_path[104];
  } __obsd79_guest_alignment;

#define OBSD79_RUSAGE_SELF     (0)
#define OBSD79_RUSAGE_CHILDREN (-1)

struct obsd79_rusage
  {
    struct obsd79_timeval ru_utime;
    struct obsd79_timeval ru_stime;
    obsd79_long_t         ru_maxrss;
    obsd79_long_t         ru_ixrss;
    obsd79_long_t         ru_idrss;
    obsd79_long_t         ru_isrss;
    obsd79_long_t         ru_minflt;
    obsd79_long_t         ru_majflt;
    obsd79_long_t         ru_nswap;
    obsd79_long_t         ru_inblock;
    obsd79_long_t         ru_oublock;
    obsd79_long_t         ru_msgsnd;
    obsd79_long_t         ru_msgrcv;
    obsd79_long_t         ru_nsignals;
    obsd79_long_t         ru_nvcsw;
    obsd79_long_t         ru_nivcsw;
  } __obsd79_guest_alignment;

typedef uint64_t obsd79_rlim_t;

#define OBSD79_RLIMIT_CPU     0 
#define OBSD79_RLIMIT_FSIZE   1
#define OBSD79_RLIMIT_DATA    2
#define OBSD79_RLIMIT_STACK   3
#define OBSD79_RLIMIT_CORE    4
#define OBSD79_RLIMIT_RSS     5
#define OBSD79_RLIMIT_MEMLOCK 6
#define OBSD79_RLIMIT_NPROC   7
#define OBSD79_RLIMIT_NOFILE  8

struct obsd79_rlimit
  {
    obsd79_rlim_t rlim_cur;
    obsd79_rlim_t rlim_max;
  } __obsd79_guest_alignment;

#define OBSD79_POLLIN      0x0001
#define OBSD79_POLLPRI     0x0002
#define OBSD79_POLLOUT     0x0004
#define OBSD79_POLLERR     0x0008
#define OBSD79_POLLHUP     0x0010
#define OBSD79_POLLNVAL    0x0020
#define OBSD79_POLLRDNORM  0x0040
#define OBSD79_POLLRDBAND  0x0080
#define OBSD79_POLLWRBAND  0x0100

struct obsd79_pollfd
  {
    int32_t fd;
    int16_t events;
    int16_t revents;
  } __obsd79_guest_alignment;

/* Special Control Characters */
#define OBSD79_VEOF     0
#define OBSD79_VEOL     1
#define OBSD79_VEOL2    2
#define OBSD79_VERASE   3
#define OBSD79_VWERASE  4
#define OBSD79_VKILL    5
#define OBSD79_VREPRINT 6
#define OBSD79_VINTR    8
#define OBSD79_VQUIT    9
#define OBSD79_VSUSP    10
#define OBSD79_VDSUSP   11
#define OBSD79_VSTART   12
#define OBSD79_VSTOP    13
#define OBSD79_VLNEXT   14
#define OBSD79_VDISCARD 15
#define OBSD79_VMIN     16
#define OBSD79_VTIME    17
#define OBSD79_VSTATUS  18

#define OBSD79_NCCS     20

/* Input flags */
#define OBSD79_IGNBRK   0x00000001
#define OBSD79_BRKINT   0x00000002
#define OBSD79_IGNPAR   0x00000004
#define OBSD79_PARMRK   0x00000008
#define OBSD79_INPCK    0x00000010
#define OBSD79_ISTRIP   0x00000020
#define OBSD79_INLCR    0x00000040
#define OBSD79_IGNCR    0x00000080
#define OBSD79_ICRNL    0x00000100
#define OBSD79_IXON     0x00000200
#define OBSD79_IXOFF    0x00000400
#define OBSD79_IXANY    0x00000800
#define OBSD79_IUCLC    0x00001000
#define OBSD79_IMAXBEL  0x00002000

/* Output Flags */
#define OBSD79_OPOST    0x00000001
#define OBSD79_ONLCR    0x00000002
#define OBSD79_OXTABS   0x00000004
#define OBSD79_ONOEOT   0x00000008
#define OBSD79_OCRNL    0x00000010
#define OBSD79_OLCUC    0x00000020
#define OBSD79_ONOCR    0x00000040
#define OBSD79_ONLRET   0x00000080

/* Control Flags */
#define OBSD79_CIGNORE  0x00000001
#define OBSD79_CSIZE    0x00000300
#define OBSD79_CS5      0x00000000
#define OBSD79_CS6      0x00000100
#define OBSD79_CS7      0x00000200
#define OBSD79_CS8      0x00000300
#define OBSD79_CSTOPB   0x00000400
#define OBSD79_CREAD    0x00000800
#define OBSD79_PARENB   0x00001000
#define OBSD79_PARODD   0x00002000
#define OBSD79_HUPCL    0x00004000
#define OBSD79_CLOCAL   0x00008000
#define OBSD79_CRTSCTS  0x00010000
#define OBSD79_MDMBUF   0x00100000
#define OBSD79_CHWFLOW  (OBSD79_MDMBUF | OBSD79_CRTSCTS)

/* Local Flags */
#define OBSD79_ECHOKE     0x00000001
#define OBSD79_ECHOE      0x00000002
#define OBSD79_ECHOK      0x00000004
#define OBSD79_ECHO       0x00000008
#define OBSD79_ECHONL     0x00000010
#define OBSD79_ECHOPRT    0x00000020
#define OBSD79_ECHOCTL    0x00000040
#define OBSD79_ISIG       0x00000080
#define OBSD79_ICANON     0x00000100
#define OBSD79_ALTWERASE  0x00000200
#define OBSD79_IEXTEN     0x00000400
#define OBSD79_EXTPROC    0x00000800
#define OBSD79_TOSTOP     0x00400000
#define OBSD79_FLUSHO     0x00800000
#define OBSD79_XCASE      0x01000000
#define OBSD79_NOKERNINFO 0x02000000
#define OBSD79_PENDIN     0x20000000
#define OBSD79_NOFLSH     0x80000000

/* Standard speeds */
#define OBSD79_B0      0
#define OBSD79_B50     50
#define OBSD79_B75     75
#define OBSD79_B110    110
#define OBSD79_B134    134
#define OBSD79_B150    150
#define OBSD79_B200    200
#define OBSD79_B300    300
#define OBSD79_B600    600
#define OBSD79_B1200   1200
#define OBSD79_B1800   1800
#define OBSD79_B2400   2400
#define OBSD79_B4800   4800
#define OBSD79_B7200   7200
#define OBSD79_B9600   9600
#define OBSD79_B14400  14400
#define OBSD79_B19200  19200
#define OBSD79_B28800  28800
#define OBSD79_B38400  38400
#define OBSD79_B57600  57600
#define OBSD79_B76800  76800
#define OBSD79_B115200 115200
#define OBSD79_B230400 230400
#define OBSD79_EXTA    19200
#define OBSD79_EXTB    38400

typedef uint32_t obsd79_tcflag_t;
typedef uint32_t obsd79_speed_t;
typedef uint8_t  obsd79_cc_t;

struct obsd79_termios
  {
    obsd79_tcflag_t c_iflag;
    obsd79_tcflag_t c_oflag;
    obsd79_tcflag_t c_cflag;
    obsd79_tcflag_t c_lflag;
    obsd79_cc_t     c_cc[OBSD79_NCCS];
    obsd79_speed_t  c_ispeed;
    obsd79_speed_t  c_ospeed;
  };

typedef uint32_t obsd79_fd_mask_t;

typedef struct obsd79_fd_set
  {
    obsd79_fd_mask_t fds_bits[256 >> 3]; /* OBSD79_FD_SETSIZE */
  } obsd79_fd_set;

#endif  /* !__obsd79_types_h */
