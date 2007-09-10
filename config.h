#ifndef CONFIG_H
#define CONFIG_H

#define HAVE_POLL 1


#ifndef __GNUC__
# define __attribute__(x)
# if __STDC_VERSION__ + 0 >= 199901L
#  define __inline__ inline
# else
#  define __inline__
# endif
#endif


#endif
