/* Minimal newlib syscall stubs for bare-metal STM32.
 * Overrides the weak nosys.specs stubs so the linker doesn't warn. */
#include <sys/stat.h>
#include <errno.h>

int _close(int fd)                        { (void)fd; errno = EBADF; return -1; }
int _fstat(int fd, struct stat *st)       { (void)fd; (void)st; errno = EBADF; return -1; }
int _isatty(int fd)                       { (void)fd; return 0; }
int _lseek(int fd, int off, int wh)       { (void)fd; (void)off; (void)wh; errno = EBADF; return -1; }
int _read(int fd, char *buf, int len)     { (void)fd; (void)buf; (void)len; errno = EBADF; return -1; }
int _getpid(void)                         { return 1; }
int _kill(int pid, int sig)               { (void)pid; (void)sig; errno = EINVAL; return -1; }
