#ifndef __openbsd_stat_h
#define __openbsd_stat_h

#define OPENBSD_S_IFMT   0170000
#define OPENBSD_S_IFIFO  0010000
#define OPENBSD_S_IFCHR  0020000
#define OPENBSD_S_IFDIR  0040000
#define OPENBSD_S_IFBLK  0060000
#define OPENBSD_S_IFREG  0100000
#define OPENBSD_S_IFLNK  0120000
#define OPENBSD_S_IFSOCK 0140000

#define OPENBSD_S_ISUID  0004000
#define OPENBSD_S_ISGID  0002000
#define OPENBSD_S_ISVTX  0001000

#endif  /* !__openbsd_stat_h */
