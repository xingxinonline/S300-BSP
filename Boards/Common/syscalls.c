// Minimal newlib syscalls stubs for bare-metal
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int _close(int file)
{
    (void)file;
    errno = ENOSYS;
    return -1;
}

__attribute__((weak)) int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

__attribute__((weak)) int _isatty(int file)
{
    (void)file;
    return 1;
}

__attribute__((weak)) int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    errno = ENOSYS;
    return -1;
}

__attribute__((weak)) int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    errno = ENOSYS;
    return 0;
}

__attribute__((weak)) int _getpid(void)
{
    return 1;
}

__attribute__((weak)) int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = ENOSYS;
    return -1;
}

__attribute__((weak)) void _exit(int status)
{
    (void)status;
    for (;;) { /* spin */ }
}

// Heap stub: simple bump-pointer in .bss
extern char _ebss; // from linker script
static char *heap_end;

__attribute__((weak)) void *_sbrk(ptrdiff_t incr)
{
    if (heap_end == 0)
    {
        heap_end = &_ebss;
    }
    char *prev = heap_end;
    heap_end += incr;
    return (void *)prev;
}
