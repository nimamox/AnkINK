#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/personality.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fputs("usage: ankink-legacy-layout PROGRAM [ARG ...]\n", stderr);
    return 2;
  }

  const int current = personality(0xffffffffUL);
  if (current < 0 ||
      personality((unsigned long)current | ADDR_COMPAT_LAYOUT |
                  ADDR_NO_RANDOMIZE) < 0) {
    fprintf(stderr, "AnkINK: enabling legacy ARM mmap layout failed: %s\n",
            strerror(errno));
    return 1;
  }

  execv(argv[1], &argv[1]);
  fprintf(stderr, "AnkINK: executing %s failed: %s\n", argv[1],
          strerror(errno));
  return 1;
}
