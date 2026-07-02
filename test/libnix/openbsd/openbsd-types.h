#ifndef __openbsd_types_h
#define __openbsd_types_h

#include "nix-host.h"
#include "openbsd-guest-types.h"

typedef int32_t  openbsd_dev_t;
typedef uint32_t openbsd_ino_t;
typedef uint32_t openbsd_mode_t;
typedef uint32_t openbsd_nlink_t;
typedef uint32_t openbsd_uid_t;
typedef uint32_t openbsd_gid_t;

typedef int64_t openbsd_off_t;

typedef struct
  {
    int32_t val[2];
  } __openbsd_guest_alignment openbsd_fsid_t;

struct openbsd_timespec
  {
    openbsd_time_t tv_sec;
    openbsd_long_t tv_nsec;
  } __openbsd_guest_alignment;

struct openbsd_timeval
  {
    openbsd_time_t tv_sec;
    openbsd_long_t tv_usec;
  } __openbsd_guest_alignment;

struct openbsd_timezone
  {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
  } __openbsd_guest_alignment;

struct openbsd_stat
  {
    openbsd_dev_t           st_dev;
    openbsd_ino_t           st_ino;
    openbsd_mode_t          st_mode;
    openbsd_nlink_t         st_nlink;
    openbsd_uid_t           st_uid;
    openbsd_gid_t           st_gid;
    openbsd_dev_t           st_rdev;
    int32_t                st_lspare0;
    struct openbsd_timespec st_atimespec;
    struct openbsd_timespec st_mtimespec;
    struct openbsd_timespec st_ctimespec;
    openbsd_off_t           st_size;
    int64_t                st_blocks;
    uint32_t               st_blksize;
    uint32_t               st_flags;
    uint32_t               st_gen;
    int32_t                st_lspare1;
    struct openbsd_timespec __st_birthtimespec;
    int64_t                st_qspare[2];
  } __openbsd_guest_alignment;

union openbsd_mount_info
  {
    char __align[160];
  } __openbsd_guest_alignment;

#define OPENBSD_MNT_WAIT     1
#define OPENBSD_MNT_NOWAIT   2

#define OPENBSD_MFSNAMELEN   16
#define OPENBSD_MNAMELEN     90

struct openbsd_statfs
  {
    uint32_t                f_flags;
    int32_t                 f_bsize;
    uint32_t                f_iosize;
    uint32_t                f_blocks;
    uint32_t                f_bfree;
    int32_t                 f_bavail;
    uint32_t                f_files;
    uint32_t                f_ffree;
    openbsd_fsid_t           f_fsid;
    openbsd_uid_t            f_owner;
    uint32_t                f_syncwrites;
    uint32_t                f_asyncwrites;
    uint32_t                f_ctime;
    uint32_t                f_spare[3];
    char                    f_fstypename[OPENBSD_MFSNAMELEN];
    char                    f_mntonname[OPENBSD_MNAMELEN];
    char                    f_mntfromname[OPENBSD_MNAMELEN];
    union openbsd_mount_info mount_info;
  } __openbsd_guest_alignment;

struct openbsd_iovec32
  {
    uint32_t iov_base;
    uint32_t iov_len;
  } __openbsd_guest_alignment;

struct openbsd_sigaction32
  {
    uint32_t __sa_handler;
    uint32_t sa_flags;
    uint32_t sa_mask;
  } __openbsd_guest_alignment;

typedef uint32_t openbsd_sigset_t;
#define OPENBSD_SIG_BLOCK   1
#define OPENBSD_SIG_UNBLOCK 2
#define OPENBSD_SIG_SETMASK 3

typedef int32_t  openbsd_socklen_t;
typedef uint8_t  openbsd_sa_family_t;
typedef uint16_t openbsd_in_port_t;
typedef uint32_t openbsd_in_addr_t;

struct openbsd_sockaddr
  {
    uint8_t            sa_len;
    openbsd_sa_family_t sa_family;
    char               sa_data[14];
  } __openbsd_guest_alignment;

struct openbsd_sockaddr_storage
  {
    uint8_t            ss_len;
    openbsd_sa_family_t ss_family;
    uint8_t            __ss_pad1[6];
    uint64_t           __ss_pad2;
    uint8_t            __ss_pad3[240];
  } __openbsd_guest_alignment;

struct openbsd_sockaddr_in
  {
    uint8_t            sin_len;
    openbsd_sa_family_t sin_family;
    openbsd_in_port_t   sin_port;
    uint32_t           sin_addr;
    int8_t             sin_zero[8];
  } __openbsd_guest_alignment;

struct openbsd_sockaddr_un
  {
    uint8_t            sun_len;
    openbsd_sa_family_t sun_family;
    char               sun_path[104];
  } __openbsd_guest_alignment;

#define OPENBSD_RUSAGE_SELF     (0)
#define OPENBSD_RUSAGE_CHILDREN (-1)

struct openbsd_rusage
  {
    struct openbsd_timeval ru_utime;
    struct openbsd_timeval ru_stime;
    openbsd_long_t         ru_maxrss;
    openbsd_long_t         ru_ixrss;
    openbsd_long_t         ru_idrss;
    openbsd_long_t         ru_isrss;
    openbsd_long_t         ru_minflt;
    openbsd_long_t         ru_majflt;
    openbsd_long_t         ru_nswap;
    openbsd_long_t         ru_inblock;
    openbsd_long_t         ru_oublock;
    openbsd_long_t         ru_msgsnd;
    openbsd_long_t         ru_msgrcv;
    openbsd_long_t         ru_nsignals;
    openbsd_long_t         ru_nvcsw;
    openbsd_long_t         ru_nivcsw;
  } __openbsd_guest_alignment;

typedef uint64_t openbsd_rlim_t;

#define OPENBSD_RLIMIT_CPU     0 
#define OPENBSD_RLIMIT_FSIZE   1
#define OPENBSD_RLIMIT_DATA    2
#define OPENBSD_RLIMIT_STACK   3
#define OPENBSD_RLIMIT_CORE    4
#define OPENBSD_RLIMIT_RSS     5
#define OPENBSD_RLIMIT_MEMLOCK 6
#define OPENBSD_RLIMIT_NPROC   7
#define OPENBSD_RLIMIT_NOFILE  8

struct openbsd_rlimit
  {
    openbsd_rlim_t rlim_cur;
    openbsd_rlim_t rlim_max;
  } __openbsd_guest_alignment;

#define OPENBSD_POLLIN      0x0001
#define OPENBSD_POLLPRI     0x0002
#define OPENBSD_POLLOUT     0x0004
#define OPENBSD_POLLERR     0x0008
#define OPENBSD_POLLHUP     0x0010
#define OPENBSD_POLLNVAL    0x0020
#define OPENBSD_POLLRDNORM  0x0040
#define OPENBSD_POLLRDBAND  0x0080
#define OPENBSD_POLLWRBAND  0x0100

struct openbsd_pollfd
  {
    int32_t fd;
    int16_t events;
    int16_t revents;
  } __openbsd_guest_alignment;

/* Special Control Characters */
#define OPENBSD_VEOF     0
#define OPENBSD_VEOL     1
#define OPENBSD_VEOL2    2
#define OPENBSD_VERASE   3
#define OPENBSD_VWERASE  4
#define OPENBSD_VKILL    5
#define OPENBSD_VREPRINT 6
#define OPENBSD_VINTR    8
#define OPENBSD_VQUIT    9
#define OPENBSD_VSUSP    10
#define OPENBSD_VDSUSP   11
#define OPENBSD_VSTART   12
#define OPENBSD_VSTOP    13
#define OPENBSD_VLNEXT   14
#define OPENBSD_VDISCARD 15
#define OPENBSD_VMIN     16
#define OPENBSD_VTIME    17
#define OPENBSD_VSTATUS  18

#define OPENBSD_NCCS     20

/* Input flags */
#define OPENBSD_IGNBRK   0x00000001
#define OPENBSD_BRKINT   0x00000002
#define OPENBSD_IGNPAR   0x00000004
#define OPENBSD_PARMRK   0x00000008
#define OPENBSD_INPCK    0x00000010
#define OPENBSD_ISTRIP   0x00000020
#define OPENBSD_INLCR    0x00000040
#define OPENBSD_IGNCR    0x00000080
#define OPENBSD_ICRNL    0x00000100
#define OPENBSD_IXON     0x00000200
#define OPENBSD_IXOFF    0x00000400
#define OPENBSD_IXANY    0x00000800
#define OPENBSD_IUCLC    0x00001000
#define OPENBSD_IMAXBEL  0x00002000

/* Output Flags */
#define OPENBSD_OPOST    0x00000001
#define OPENBSD_ONLCR    0x00000002
#define OPENBSD_OXTABS   0x00000004
#define OPENBSD_ONOEOT   0x00000008
#define OPENBSD_OCRNL    0x00000010
#define OPENBSD_OLCUC    0x00000020
#define OPENBSD_ONOCR    0x00000040
#define OPENBSD_ONLRET   0x00000080

/* Control Flags */
#define OPENBSD_CIGNORE  0x00000001
#define OPENBSD_CSIZE    0x00000300
#define OPENBSD_CS5      0x00000000
#define OPENBSD_CS6      0x00000100
#define OPENBSD_CS7      0x00000200
#define OPENBSD_CS8      0x00000300
#define OPENBSD_CSTOPB   0x00000400
#define OPENBSD_CREAD    0x00000800
#define OPENBSD_PARENB   0x00001000
#define OPENBSD_PARODD   0x00002000
#define OPENBSD_HUPCL    0x00004000
#define OPENBSD_CLOCAL   0x00008000
#define OPENBSD_CRTSCTS  0x00010000
#define OPENBSD_MDMBUF   0x00100000
#define OPENBSD_CHWFLOW  (OPENBSD_MDMBUF | OPENBSD_CRTSCTS)

/* Local Flags */
#define OPENBSD_ECHOKE     0x00000001
#define OPENBSD_ECHOE      0x00000002
#define OPENBSD_ECHOK      0x00000004
#define OPENBSD_ECHO       0x00000008
#define OPENBSD_ECHONL     0x00000010
#define OPENBSD_ECHOPRT    0x00000020
#define OPENBSD_ECHOCTL    0x00000040
#define OPENBSD_ISIG       0x00000080
#define OPENBSD_ICANON     0x00000100
#define OPENBSD_ALTWERASE  0x00000200
#define OPENBSD_IEXTEN     0x00000400
#define OPENBSD_EXTPROC    0x00000800
#define OPENBSD_TOSTOP     0x00400000
#define OPENBSD_FLUSHO     0x00800000
#define OPENBSD_XCASE      0x01000000
#define OPENBSD_NOKERNINFO 0x02000000
#define OPENBSD_PENDIN     0x20000000
#define OPENBSD_NOFLSH     0x80000000

/* Standard speeds */
#define OPENBSD_B0      0
#define OPENBSD_B50     50
#define OPENBSD_B75     75
#define OPENBSD_B110    110
#define OPENBSD_B134    134
#define OPENBSD_B150    150
#define OPENBSD_B200    200
#define OPENBSD_B300    300
#define OPENBSD_B600    600
#define OPENBSD_B1200   1200
#define OPENBSD_B1800   1800
#define OPENBSD_B2400   2400
#define OPENBSD_B4800   4800
#define OPENBSD_B7200   7200
#define OPENBSD_B9600   9600
#define OPENBSD_B14400  14400
#define OPENBSD_B19200  19200
#define OPENBSD_B28800  28800
#define OPENBSD_B38400  38400
#define OPENBSD_B57600  57600
#define OPENBSD_B76800  76800
#define OPENBSD_B115200 115200
#define OPENBSD_B230400 230400
#define OPENBSD_EXTA    19200
#define OPENBSD_EXTB    38400

typedef uint32_t openbsd_tcflag_t;
typedef uint32_t openbsd_speed_t;
typedef uint8_t  openbsd_cc_t;

struct openbsd_termios
  {
    openbsd_tcflag_t c_iflag;
    openbsd_tcflag_t c_oflag;
    openbsd_tcflag_t c_cflag;
    openbsd_tcflag_t c_lflag;
    openbsd_cc_t     c_cc[OPENBSD_NCCS];
    openbsd_speed_t  c_ispeed;
    openbsd_speed_t  c_ospeed;
  };

typedef uint32_t openbsd_fd_mask_t;

typedef struct openbsd_fd_set
  {
    openbsd_fd_mask_t fds_bits[256 >> 3]; /* OPENBSD_FD_SETSIZE */
  } openbsd_fd_set;

#endif  /* !__openbsd_types_h */
