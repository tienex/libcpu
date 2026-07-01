#ifndef __obsd79_sysctl_h
#define __obsd79_sysctl_h

#define OBSD79_CTL_KERN 1
#  define OBSD79_KERN_OSTYPE     1
#  define OBSD79_KERN_OSRELEASE  2
#  define OBSD79_KERN_ARGMAX     8
#  define OBSD79_KERN_HOSTNAME   10
#  define OBSD79_KERN_CLOCKRATE  12
#  define OBSD79_KERN_DOMAINNAME 22
#  define OBSD79_KERN_OSVERSION  27
#  define OBSD79_KERN_ARND       37

#define OBSD79_CTL_HW 6
#  define OBSD79_HW_MACHINE  1
#  define OBSD79_HW_NCPUS    3
#  define OBSD79_HW_PAGESIZE 7

#endif  /* !__obsd79_sysctl_h */
