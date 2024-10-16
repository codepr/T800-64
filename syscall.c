#include "syscall.h"
#include <stdlib.h>
#include <unistd.h>

void syscall_exit(void) { exit(EXIT_SUCCESS); }

size_t syscall_write(qword fd, qword *addr, size_t len)
{
    if (fd < 0)
        return -1;

    size_t n = 0;
    if (fd == 1) {
        n = write(fd, (const char *)addr, len);
    }
    return n;
}

size_t syscall_read(qword fd, qword *addr, size_t len)
{

    if (fd < 0)
        return -1;

    size_t n = 0;
    if (fd == 0) {
        n = read(fd, (char *)addr, len);
    }

    return n;
}
