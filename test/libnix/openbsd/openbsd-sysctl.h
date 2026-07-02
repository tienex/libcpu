#ifndef __openbsd_sysctl_h
#define __openbsd_sysctl_h

#define OPENBSD_CTL_KERN 1
#  define OPENBSD_KERN_OSTYPE     1
#  define OPENBSD_KERN_OSRELEASE  2
#  define OPENBSD_KERN_ARGMAX     8
#  define OPENBSD_KERN_HOSTNAME   10
#  define OPENBSD_KERN_CLOCKRATE  12
#  define OPENBSD_KERN_DOMAINNAME 22
#  define OPENBSD_KERN_OSVERSION  27
#  define OPENBSD_KERN_ARND       37

#define OPENBSD_CTL_HW 6
#  define OPENBSD_HW_MACHINE  1
#  define OPENBSD_HW_NCPUS    3
#  define OPENBSD_HW_PAGESIZE 7

#endif  /* !__openbsd_sysctl_h */
