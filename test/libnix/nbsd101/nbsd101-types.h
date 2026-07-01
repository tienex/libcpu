#ifndef __nbsd101_types_h
#define __nbsd101_types_h

#include "nix-host.h"
#include "nbsd101-guest-types.h"

typedef int32_t  nbsd101_dev_t;
typedef uint32_t nbsd101_ino_t;
typedef uint32_t nbsd101_mode_t;
typedef uint32_t nbsd101_nlink_t;
typedef uint32_t nbsd101_uid_t;
typedef uint32_t nbsd101_gid_t;

typedef int64_t nbsd101_off_t;

typedef struct
  {
    int32_t val[2];
  } __nbsd101_guest_alignment nbsd101_fsid_t;

struct nbsd101_timespec
  {
    nbsd101_time_t tv_sec;
    nbsd101_long_t tv_nsec;
  } __nbsd101_guest_alignment;

struct nbsd101_timeval
  {
    nbsd101_time_t tv_sec;
    nbsd101_long_t tv_usec;
  } __nbsd101_guest_alignment;

struct nbsd101_timezone
  {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
  } __nbsd101_guest_alignment;

struct nbsd101_stat
  {
    nbsd101_dev_t           st_dev;
    nbsd101_ino_t           st_ino;
    nbsd101_mode_t          st_mode;
    nbsd101_nlink_t         st_nlink;
    nbsd101_uid_t           st_uid;
    nbsd101_gid_t           st_gid;
    nbsd101_dev_t           st_rdev;
    int32_t                st_lspare0;
    struct nbsd101_timespec st_atimespec;
    struct nbsd101_timespec st_mtimespec;
    struct nbsd101_timespec st_ctimespec;
    nbsd101_off_t           st_size;
    int64_t                st_blocks;
    uint32_t               st_blksize;
    uint32_t               st_flags;
    uint32_t               st_gen;
    int32_t                st_lspare1;
    struct nbsd101_timespec __st_birthtimespec;
    int64_t                st_qspare[2];
  } __nbsd101_guest_alignment;

union nbsd101_mount_info
  {
    char __align[160];
  } __nbsd101_guest_alignment;

#define NBSD101_MNT_WAIT     1
#define NBSD101_MNT_NOWAIT   2

#define NBSD101_MFSNAMELEN   16
#define NBSD101_MNAMELEN     90

struct nbsd101_statfs
  {
    uint32_t                f_flags;
    int32_t                 f_bsize;
    uint32_t                f_iosize;
    uint32_t                f_blocks;
    uint32_t                f_bfree;
    int32_t                 f_bavail;
    uint32_t                f_files;
    uint32_t                f_ffree;
    nbsd101_fsid_t           f_fsid;
    nbsd101_uid_t            f_owner;
    uint32_t                f_syncwrites;
    uint32_t                f_asyncwrites;
    uint32_t                f_ctime;
    uint32_t                f_spare[3];
    char                    f_fstypename[NBSD101_MFSNAMELEN];
    char                    f_mntonname[NBSD101_MNAMELEN];
    char                    f_mntfromname[NBSD101_MNAMELEN];
    union nbsd101_mount_info mount_info;
  } __nbsd101_guest_alignment;

struct nbsd101_iovec32
  {
    uint32_t iov_base;
    uint32_t iov_len;
  } __nbsd101_guest_alignment;

struct nbsd101_sigaction32
  {
    uint32_t __sa_handler;
    uint32_t sa_flags;
    uint32_t sa_mask;
  } __nbsd101_guest_alignment;

typedef uint32_t nbsd101_sigset_t;
#define NBSD101_SIG_BLOCK   1
#define NBSD101_SIG_UNBLOCK 2
#define NBSD101_SIG_SETMASK 3

typedef int32_t  nbsd101_socklen_t;
typedef uint8_t  nbsd101_sa_family_t;
typedef uint16_t nbsd101_in_port_t;
typedef uint32_t nbsd101_in_addr_t;

struct nbsd101_sockaddr
  {
    uint8_t            sa_len;
    nbsd101_sa_family_t sa_family;
    char               sa_data[14];
  } __nbsd101_guest_alignment;

struct nbsd101_sockaddr_storage
  {
    uint8_t            ss_len;
    nbsd101_sa_family_t ss_family;
    uint8_t            __ss_pad1[6];
    uint64_t           __ss_pad2;
    uint8_t            __ss_pad3[240];
  } __nbsd101_guest_alignment;

struct nbsd101_sockaddr_in
  {
    uint8_t            sin_len;
    nbsd101_sa_family_t sin_family;
    nbsd101_in_port_t   sin_port;
    uint32_t           sin_addr;
    int8_t             sin_zero[8];
  } __nbsd101_guest_alignment;

struct nbsd101_sockaddr_un
  {
    uint8_t            sun_len;
    nbsd101_sa_family_t sun_family;
    char               sun_path[104];
  } __nbsd101_guest_alignment;

#define NBSD101_RUSAGE_SELF     (0)
#define NBSD101_RUSAGE_CHILDREN (-1)

struct nbsd101_rusage
  {
    struct nbsd101_timeval ru_utime;
    struct nbsd101_timeval ru_stime;
    nbsd101_long_t         ru_maxrss;
    nbsd101_long_t         ru_ixrss;
    nbsd101_long_t         ru_idrss;
    nbsd101_long_t         ru_isrss;
    nbsd101_long_t         ru_minflt;
    nbsd101_long_t         ru_majflt;
    nbsd101_long_t         ru_nswap;
    nbsd101_long_t         ru_inblock;
    nbsd101_long_t         ru_oublock;
    nbsd101_long_t         ru_msgsnd;
    nbsd101_long_t         ru_msgrcv;
    nbsd101_long_t         ru_nsignals;
    nbsd101_long_t         ru_nvcsw;
    nbsd101_long_t         ru_nivcsw;
  } __nbsd101_guest_alignment;

typedef uint64_t nbsd101_rlim_t;

#define NBSD101_RLIMIT_CPU     0 
#define NBSD101_RLIMIT_FSIZE   1
#define NBSD101_RLIMIT_DATA    2
#define NBSD101_RLIMIT_STACK   3
#define NBSD101_RLIMIT_CORE    4
#define NBSD101_RLIMIT_RSS     5
#define NBSD101_RLIMIT_MEMLOCK 6
#define NBSD101_RLIMIT_NPROC   7
#define NBSD101_RLIMIT_NOFILE  8

struct nbsd101_rlimit
  {
    nbsd101_rlim_t rlim_cur;
    nbsd101_rlim_t rlim_max;
  } __nbsd101_guest_alignment;

#define NBSD101_POLLIN      0x0001
#define NBSD101_POLLPRI     0x0002
#define NBSD101_POLLOUT     0x0004
#define NBSD101_POLLERR     0x0008
#define NBSD101_POLLHUP     0x0010
#define NBSD101_POLLNVAL    0x0020
#define NBSD101_POLLRDNORM  0x0040
#define NBSD101_POLLRDBAND  0x0080
#define NBSD101_POLLWRBAND  0x0100

struct nbsd101_pollfd
  {
    int32_t fd;
    int16_t events;
    int16_t revents;
  } __nbsd101_guest_alignment;

/* Special Control Characters */
#define NBSD101_VEOF     0
#define NBSD101_VEOL     1
#define NBSD101_VEOL2    2
#define NBSD101_VERASE   3
#define NBSD101_VWERASE  4
#define NBSD101_VKILL    5
#define NBSD101_VREPRINT 6
#define NBSD101_VINTR    8
#define NBSD101_VQUIT    9
#define NBSD101_VSUSP    10
#define NBSD101_VDSUSP   11
#define NBSD101_VSTART   12
#define NBSD101_VSTOP    13
#define NBSD101_VLNEXT   14
#define NBSD101_VDISCARD 15
#define NBSD101_VMIN     16
#define NBSD101_VTIME    17
#define NBSD101_VSTATUS  18

#define NBSD101_NCCS     20

/* Input flags */
#define NBSD101_IGNBRK   0x00000001
#define NBSD101_BRKINT   0x00000002
#define NBSD101_IGNPAR   0x00000004
#define NBSD101_PARMRK   0x00000008
#define NBSD101_INPCK    0x00000010
#define NBSD101_ISTRIP   0x00000020
#define NBSD101_INLCR    0x00000040
#define NBSD101_IGNCR    0x00000080
#define NBSD101_ICRNL    0x00000100
#define NBSD101_IXON     0x00000200
#define NBSD101_IXOFF    0x00000400
#define NBSD101_IXANY    0x00000800
#define NBSD101_IUCLC    0x00001000
#define NBSD101_IMAXBEL  0x00002000

/* Output Flags */
#define NBSD101_OPOST    0x00000001
#define NBSD101_ONLCR    0x00000002
#define NBSD101_OXTABS   0x00000004
#define NBSD101_ONOEOT   0x00000008
#define NBSD101_OCRNL    0x00000010
#define NBSD101_OLCUC    0x00000020
#define NBSD101_ONOCR    0x00000040
#define NBSD101_ONLRET   0x00000080

/* Control Flags */
#define NBSD101_CIGNORE  0x00000001
#define NBSD101_CSIZE    0x00000300
#define NBSD101_CS5      0x00000000
#define NBSD101_CS6      0x00000100
#define NBSD101_CS7      0x00000200
#define NBSD101_CS8      0x00000300
#define NBSD101_CSTOPB   0x00000400
#define NBSD101_CREAD    0x00000800
#define NBSD101_PARENB   0x00001000
#define NBSD101_PARODD   0x00002000
#define NBSD101_HUPCL    0x00004000
#define NBSD101_CLOCAL   0x00008000
#define NBSD101_CRTSCTS  0x00010000
#define NBSD101_MDMBUF   0x00100000
#define NBSD101_CHWFLOW  (NBSD101_MDMBUF | NBSD101_CRTSCTS)

/* Local Flags */
#define NBSD101_ECHOKE     0x00000001
#define NBSD101_ECHOE      0x00000002
#define NBSD101_ECHOK      0x00000004
#define NBSD101_ECHO       0x00000008
#define NBSD101_ECHONL     0x00000010
#define NBSD101_ECHOPRT    0x00000020
#define NBSD101_ECHOCTL    0x00000040
#define NBSD101_ISIG       0x00000080
#define NBSD101_ICANON     0x00000100
#define NBSD101_ALTWERASE  0x00000200
#define NBSD101_IEXTEN     0x00000400
#define NBSD101_EXTPROC    0x00000800
#define NBSD101_TOSTOP     0x00400000
#define NBSD101_FLUSHO     0x00800000
#define NBSD101_XCASE      0x01000000
#define NBSD101_NOKERNINFO 0x02000000
#define NBSD101_PENDIN     0x20000000
#define NBSD101_NOFLSH     0x80000000

/* Standard speeds */
#define NBSD101_B0      0
#define NBSD101_B50     50
#define NBSD101_B75     75
#define NBSD101_B110    110
#define NBSD101_B134    134
#define NBSD101_B150    150
#define NBSD101_B200    200
#define NBSD101_B300    300
#define NBSD101_B600    600
#define NBSD101_B1200   1200
#define NBSD101_B1800   1800
#define NBSD101_B2400   2400
#define NBSD101_B4800   4800
#define NBSD101_B7200   7200
#define NBSD101_B9600   9600
#define NBSD101_B14400  14400
#define NBSD101_B19200  19200
#define NBSD101_B28800  28800
#define NBSD101_B38400  38400
#define NBSD101_B57600  57600
#define NBSD101_B76800  76800
#define NBSD101_B115200 115200
#define NBSD101_B230400 230400
#define NBSD101_EXTA    19200
#define NBSD101_EXTB    38400

typedef uint32_t nbsd101_tcflag_t;
typedef uint32_t nbsd101_speed_t;
typedef uint8_t  nbsd101_cc_t;

struct nbsd101_termios
  {
    nbsd101_tcflag_t c_iflag;
    nbsd101_tcflag_t c_oflag;
    nbsd101_tcflag_t c_cflag;
    nbsd101_tcflag_t c_lflag;
    nbsd101_cc_t     c_cc[NBSD101_NCCS];
    nbsd101_speed_t  c_ispeed;
    nbsd101_speed_t  c_ospeed;
  };

typedef uint32_t nbsd101_fd_mask_t;

typedef struct nbsd101_fd_set
  {
    nbsd101_fd_mask_t fds_bits[256 >> 3]; /* NBSD101_FD_SETSIZE */
  } nbsd101_fd_set;

#endif  /* !__nbsd101_types_h */
