#ifndef __netbsd_stat_h
#define __netbsd_stat_h

#define NETBSD_S_IFMT   0170000
#define NETBSD_S_IFIFO  0010000
#define NETBSD_S_IFCHR  0020000
#define NETBSD_S_IFDIR  0040000
#define NETBSD_S_IFBLK  0060000
#define NETBSD_S_IFREG  0100000
#define NETBSD_S_IFLNK  0120000
#define NETBSD_S_IFSOCK 0140000

#define NETBSD_S_ISUID  0004000
#define NETBSD_S_ISGID  0002000
#define NETBSD_S_ISVTX  0001000

#endif  /* !__netbsd_stat_h */
