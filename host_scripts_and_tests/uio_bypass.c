#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <errno.h>

#define TARGET_UIO_DEV   "/dev/uio0"
#define REPLACEMENT_DEV  "/dev/mem"
#define SHMEM_PHYS_ADDR  0x37000000ULL

/* Function pointers to original syscall/GLIBC wrappers */
static int (*real_open)(const char *pathname, int flags, ...) = NULL;
static int (*real_openat)(int dirfd, const char *pathname, int flags, ...) = NULL;
static void *(*real_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset) = NULL;

static int redirected_fd = -1;

/* Resolve symbols via dlsym */
static void init_hooks(void) {
    if (!real_open) {
        real_open = dlsym(RTLD_NEXT, "open");
    }
    if (!real_openat) {
        real_openat = dlsym(RTLD_NEXT, "openat");
    }
    if (!real_mmap) {
        real_mmap = dlsym(RTLD_NEXT, "mmap");
    }
}

/* Intercept open() */
int open(const char *pathname, int flags, ...) {
    init_hooks();

    if (pathname && strcmp(pathname, TARGET_UIO_DEV) == 0) {
        fprintf(stderr, "[uio_bypass] Intercepted open(\"%s\") -> Redirecting to %s (O_SYNC)\n", 
                pathname, REPLACEMENT_DEV);
        
        /* Force O_SYNC to attempt altering ARM64 page attributes */
        int mem_flags = O_RDWR | O_SYNC;
        
        /* Invoke direct syscall to avoid potential GLIBC version mismatches */
        redirected_fd = syscall(SYS_openat, AT_FDCWD, REPLACEMENT_DEV, mem_flags);
        if (redirected_fd < 0) {
            fprintf(stderr, "[uio_bypass] CRITICAL: Failed to open %s: %s\n", 
                    REPLACEMENT_DEV, strerror(errno));
        }
        return redirected_fd;
    }

    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);

    return real_open(pathname, flags, mode);
}

/* Intercept openat() */
int openat(int dirfd, const char *pathname, int flags, ...) {
    init_hooks();

    if (pathname && strcmp(pathname, TARGET_UIO_DEV) == 0) {
        fprintf(stderr, "[uio_bypass] Intercepted openat(\"%s\") -> Redirecting to %s\n", 
                pathname, REPLACEMENT_DEV);
        
        int mem_flags = O_RDWR | O_SYNC;
        redirected_fd = syscall(SYS_openat, AT_FDCWD, REPLACEMENT_DEV, mem_flags);
        return redirected_fd;
    }

    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);

    return real_openat(dirfd, pathname, flags, mode);
}

/* Intercept mmap() */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    init_hooks();

    /* Check if operation targets the redirected file descriptor */
    if (fd == redirected_fd && redirected_fd != -1) {
        fprintf(stderr, "[uio_bypass] Intercepted mmap on redirected fd (%d)\n", fd);
        fprintf(stderr, "[uio_bypass] Remapping offset: 0x%lx -> Physical 0x%llx (Size: 0x%zx)\n", 
                (unsigned long)offset, SHMEM_PHYS_ADDR, length);

        /* UIO assumes offset 0, whereas /dev/mem requires the absolute physical address */
        off_t target_offset = SHMEM_PHYS_ADDR;

        /* Execute direct mmap syscall */
        void *ret = (void *)syscall(SYS_mmap, addr, length, prot, flags, fd, target_offset);
        if (ret == MAP_FAILED) {
            fprintf(stderr, "[uio_bypass] mmap failed: %s\n", strerror(errno));
        } else {
            fprintf(stderr, "[uio_bypass] Memory mapped successfully at VADDR: %p\n", ret);
        }
        return ret;
    }

    return real_mmap(addr, length, prot, flags, fd, offset);
}