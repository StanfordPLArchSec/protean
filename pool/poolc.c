#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdbool.h>

#include "pool.h"
#include "util.h"

static void usage(const char *prog, FILE *f) {
  fprintf(stderr, "usage: %s [-h] [resource...] -- command [arg...]\n", prog);
}

int main(int argc, char *argv[]) {
  const char *prog = argv[0];

  bool verbose = false;

  // Parse command-line options.
  const char *orig_posixly_correct = getenv(POSIXLY_CORRECT);
  if (setenv(POSIXLY_CORRECT, "1", /*overwrite*/1) < 0)
    err(EXIT_FAILURE, "setenv");
  int optc;
  while ((optc = getopt(argc, argv, "hv")) >= 0) {
    switch (optc) {
    case 'h':
      usage(prog, stdout);
      return EXIT_SUCCESS;

    case 'v':
      verbose = true;
      break;

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

  // Locate the resource requests and the command.
  // TODO: Unify with pools.c
  const int limit_begin = optind;
  int limit_end = limit_begin;
  for (; limit_end < argc && strcmp(argv[limit_end], "--") != 0; ++limit_end)
    ;
  if (limit_end == argc)
    errx(EXIT_FAILURE, "missing '--'");
  const int cmd_begin = limit_end + 1;
  if (cmd_begin == argc)
    errx(EXIT_FAILURE, "missing command");

  // Parse the semaphore.
  const char *semid_str;
  if ((semid_str = getenv(ENV_POOL_SEMID)) == NULL)
    errx(EXIT_FAILURE, "getenv: '%s' not set", ENV_POOL_SEMID);
  const int semid = parse_int(semid_str);

  // Check the size of the semaphore against the number of limits provided.
  struct semid_ds semid_ds;
  if (semctl(semid, 0, IPC_STAT, &semid_ds) < 0)
    err(EXIT_FAILURE, "semctl");
  if (semid_ds.sem_nsems != limit_end - limit_begin)
    errx(EXIT_FAILURE, "expected %ld resource requests, got %d",
         semid_ds.sem_nsems, limit_end - limit_begin);

  // Request the resources via the semaphore.
  struct sembuf *sops;
  if ((sops = calloc(limit_end - limit_begin, sizeof(struct sembuf))) == NULL)
    err(EXIT_FAILURE, "calloc");
  for (int limit_idx = limit_begin; limit_idx != limit_end; ++limit_idx) {
    struct sembuf *sop = &sops[limit_idx - 1];
    sop->sem_num = limit_idx - limit_begin;
    sop->sem_op = -parse_short(argv[limit_idx]);
    sop->sem_flg = SEM_UNDO;
  }
  int semop_ret;
  while ((semop_ret = semop(semid, sops, limit_end - limit_begin)) == EINTR)
    ;
  if (semop_ret < 0)
    err(EXIT_FAILURE, "semop");

  // Print out message that this process is running.
  // TODO: Add verbose and/or quiet mode.
  if (verbose) {
    fprintf(stderr, "#");
    for (int cmd_idx = cmd_begin; cmd_idx != argc; ++cmd_idx)
      fprintf(stderr, " %s", argv[cmd_idx]);
    fprintf(stderr, "\n");
  }

  // Run the child program.
  return run_child(&argv[cmd_begin]);
}
