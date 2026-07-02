#ifndef __netbsd_sysctl_h
#define __netbsd_sysctl_h

#define NETBSD_CTL_KERN 1
#  define NETBSD_KERN_OSTYPE     1
#  define NETBSD_KERN_OSRELEASE  2
#  define NETBSD_KERN_ARGMAX     8
#  define NETBSD_KERN_HOSTNAME   10
#  define NETBSD_KERN_CLOCKRATE  12
#  define NETBSD_KERN_DOMAINNAME 22
#  define NETBSD_KERN_OSVERSION  27
#  define NETBSD_KERN_ARND       37

#define NETBSD_CTL_HW 6
#  define NETBSD_HW_MACHINE  1
#  define NETBSD_HW_NCPUS    3
#  define NETBSD_HW_PAGESIZE 7

#endif  /* !__netbsd_sysctl_h */
