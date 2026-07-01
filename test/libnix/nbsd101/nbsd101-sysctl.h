#ifndef __nbsd101_sysctl_h
#define __nbsd101_sysctl_h

#define NBSD101_CTL_KERN 1
#  define NBSD101_KERN_OSTYPE     1
#  define NBSD101_KERN_OSRELEASE  2
#  define NBSD101_KERN_ARGMAX     8
#  define NBSD101_KERN_HOSTNAME   10
#  define NBSD101_KERN_CLOCKRATE  12
#  define NBSD101_KERN_DOMAINNAME 22
#  define NBSD101_KERN_OSVERSION  27
#  define NBSD101_KERN_ARND       37

#define NBSD101_CTL_HW 6
#  define NBSD101_HW_MACHINE  1
#  define NBSD101_HW_NCPUS    3
#  define NBSD101_HW_PAGESIZE 7

#endif  /* !__nbsd101_sysctl_h */
