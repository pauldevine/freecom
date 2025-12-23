#ifndef SUPPL_COMPAT_DIRECT_H
#define SUPPL_COMPAT_DIRECT_H

#include <stddef.h>

/* Minimal direct.h shims for ia16-elf cross builds */
char *_getdcwd(int drive, char *buf, unsigned length);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int mkdir(const char *path);
int rmdir(const char *path);

#endif /* SUPPL_COMPAT_DIRECT_H */
