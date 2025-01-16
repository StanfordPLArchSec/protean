#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "pool.h"
#include "util.h"

static void usage(const char *prog, FILE *f) {
  fprintf(f, "usage: %s [-h] [limits...] -- command [arg...]\n", prog);
}

int main(int argc, char *argv[]) {
  const char *prog = argv[0];

  // Parse command-line options.
  const char *orig_posixly_correct = getenv(POSIXLY_CORRECT);
  if (setenv(POSIXLY_CORRECT, "1", /*overwrite*/1) < 0)
    err(EXIT_FAILURE, "setenv");
  int optc;
  while ((optc = getopt(argc, argv, "h")) >= 0) {
    switch (optc) {
    case 'h':
      usage(prog, stdout);
      return EXIT_SUCCESS;

    default:
      usage(prog, stderr);
      return EXIT_FAILURE;
    }
  }
  if (orig_posixly_correct) {
    setenv(POSIXLY_CORRECT, orig_posixly_correct, /*overwrite*/1);
  } else {
    unsetenv(POSIXLY_CORRECT);
  }

  // Locate the limits and the commands.
  const int limit_begin = optind;
  int limit_end = limit_begin;
  for (; limit_end < argc && strcmp(argv[limit_end], "--") != 0; ++limit_end)
    ;
  if (limit_end == argc)
    errx(EXIT_FAILURE, "missing '--'");
  const int cmd_begin = limit_end + 1;
  if (cmd_begin == argc)
    errx(EXIT_FAILURE, "missing command");

  // Create the semaphore.
  int semid;
  if ((semid = semget(IPC_PRIVATE, limit_end - limit_begin, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR)) < 0)
    err(EXIT_FAILURE, "semget");

  // Initialize it.
  for (int limit_idx = limit_begin; limit_idx < limit_end; ++limit_idx) {
    const int limit = parse_int(argv[limit_idx]);
    if (semctl(semid, limit_idx - limit_begin, SETVAL, limit) < 0)
      err(EXIT_FAILURE, "semctl: SETVAL");
  }

  // Set the environment variable specifying the semid.
  char buf[16];
  sprintf(buf, "%d", semid);
  if (setenv(ENV_POOL_SEMID, buf, /*overwrite*/1) < 0)
    err(EXIT_FAILURE, "setenv");  

  // Fork and exec the child.
  return run_child(&argv[cmd_begin]);
}
