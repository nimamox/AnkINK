#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// glibc redirects the source-level mmap name to mmap64 when
// _FILE_OFFSET_BITS=64. We must interpose both ELF symbols because Mesa uses
// both entry points internally.
#ifdef mmap
#undef mmap
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#endif

#ifndef F_GET_SEALS
#define F_GET_SEALS 1034
#endif

typedef int (*ankink_fcntl_fn)(int, int, ...);
typedef void *(*ankink_mmap32_fn)(void *, size_t, int, int, int, int32_t);
typedef void *(*ankink_mmap64_fn)(void *, size_t, int, int, int, int64_t);

// Linux 3.0's ARM mmap allocator can exhaust its preferred allocation window
// without falling back to large holes below the shared-library area. Modern
// Mesa triggers this after loading its many DSOs: mmap(NULL, ...) returns
// ENOMEM even though /proc/<pid>/maps shows hundreds of free megabytes. A
// non-fixed low hint makes that old allocator search the otherwise-unused hole.
static void *const ankink_low_mmap_hint = (void *)(uintptr_t)0x01000000U;

void *ankink_mmap32(void *address, size_t length, int protection, int flags,
                    int descriptor, int32_t offset) __asm__("mmap");

void *ankink_mmap32(void *address, size_t length, int protection, int flags,
                    int descriptor, int32_t offset) {
  static ankink_mmap32_fn function;
  if (!function) {
    void *symbol = dlsym(RTLD_NEXT, "mmap");
    memcpy(&function, &symbol, sizeof(function));
  }
  if (!function) {
    errno = ENOSYS;
    return MAP_FAILED;
  }
  void *result =
      function(address, length, protection, flags, descriptor, offset);
  if (result != MAP_FAILED || address || (flags & MAP_FIXED) ||
      errno != ENOMEM)
    return result;
  return function(ankink_low_mmap_hint, length, protection, flags, descriptor,
                  offset);
}

void *mmap64(void *address, size_t length, int protection, int flags,
             int descriptor, int64_t offset) {
  static ankink_mmap64_fn function;
  if (!function) {
    void *symbol = dlsym(RTLD_NEXT, "mmap64");
    memcpy(&function, &symbol, sizeof(function));
  }
  if (!function) {
    errno = ENOSYS;
    return MAP_FAILED;
  }
  void *result =
      function(address, length, protection, flags, descriptor, offset);
  if (result != MAP_FAILED || address || (flags & MAP_FIXED) ||
      errno != ENOMEM)
    return result;
  return function(ankink_low_mmap_hint, length, protection, flags, descriptor,
                  offset);
}

static ankink_fcntl_fn resolve_fcntl(void) {
  static ankink_fcntl_fn function;
  if (!function) {
    void *symbol = dlsym(RTLD_NEXT, "fcntl");
    memcpy(&function, &symbol, sizeof(function));
  }
  return function;
}

int memfd_create(const char *name, unsigned int flags) {
  (void)name;
  if (flags & ~(MFD_CLOEXEC | MFD_ALLOW_SEALING)) {
    errno = EINVAL;
    return -1;
  }

  const char *directory = getenv("XDG_RUNTIME_DIR");
  if (!directory || directory[0] != '/')
    directory = "/tmp";

  char path[PATH_MAX];
  const int length =
      snprintf(path, sizeof(path), "%s/ankink-memfd-XXXXXX", directory);
  if (length < 0 || (size_t)length >= sizeof(path)) {
    errno = ENAMETOOLONG;
    return -1;
  }

  const int descriptor = mkstemp(path);
  if (descriptor < 0)
    return -1;
  unlink(path);

  if (flags & MFD_CLOEXEC) {
    ankink_fcntl_fn real_fcntl = resolve_fcntl();
    if (!real_fcntl || real_fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
      const int error = errno;
      close(descriptor);
      errno = error;
      return -1;
    }
  }
  return descriptor;
}

int fcntl(int descriptor, int command, ...) {
  ankink_fcntl_fn real_fcntl = resolve_fcntl();
  if (!real_fcntl) {
    errno = ENOSYS;
    return -1;
  }

  if (command == F_GET_SEALS) {
    const int result = real_fcntl(descriptor, command);
    if (result < 0 && (errno == EINVAL || errno == ENOSYS))
      return 0;
    return result;
  }

  switch (command) {
  case F_GETFD:
  case F_GETFL:
  case F_GETOWN:
#ifdef F_GETSIG
  case F_GETSIG:
#endif
#ifdef F_GETLEASE
  case F_GETLEASE:
#endif
#ifdef F_GETPIPE_SZ
  case F_GETPIPE_SZ:
#endif
    return real_fcntl(descriptor, command);
  default:
    break;
  }

  va_list arguments;
  va_start(arguments, command);
  const uintptr_t argument = va_arg(arguments, uintptr_t);
  va_end(arguments);

  const int result = real_fcntl(descriptor, command, argument);
  if (command == F_ADD_SEALS && result < 0 &&
      (errno == EINVAL || errno == ENOSYS))
    return 0;
  return result;
}
