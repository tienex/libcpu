#ifndef __obsd79_stat_h
#define __obsd79_stat_h

#define OBSD79_S_IFMT   0170000
#define OBSD79_S_IFIFO  0010000
#define OBSD79_S_IFCHR  0020000
#define OBSD79_S_IFDIR  0040000
#define OBSD79_S_IFBLK  0060000
#define OBSD79_S_IFREG  0100000
#define OBSD79_S_IFLNK  0120000
#define OBSD79_S_IFSOCK 0140000

#define OBSD79_S_ISUID  0004000
#define OBSD79_S_ISGID  0002000
#define OBSD79_S_ISVTX  0001000

#endif  /* !__obsd79_stat_h */
