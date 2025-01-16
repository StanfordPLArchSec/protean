#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <err.h>
#include <sys/wait.h>

int parse_int(const char *s) {
  char *end;
  errno = 0;
  const long unchecked = strtol(s, &end, 0);
  if (errno)
    err(EXIT_FAILURE, "strtol: %s", s);
  if (*s == '\0' || *end != '\0')
    errx(EXIT_FAILURE, "bad integer: %s", s);
  if (unchecked > INT_MAX || unchecked < INT_MIN)
    errx(EXIT_FAILURE, "value does not fit in int: %s", s);
  return (int) unchecked;
}

short parse_short(const char *s) {
  const int unchecked = parse_int(s);
  if (unchecked > SHRT_MAX || unchecked < SHRT_MIN)
    errx(EXIT_FAILURE, "value does not fit in short: %s", s);
  return (short) unchecked;
}

pid_t spawn_child(char **cmd) {
  const pid_t pid = fork();
  if (pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if (pid == 0) {
    // Execute the process.
    execvp(cmd[0], cmd);
    err(EXIT_FAILURE, "execvp: %s", cmd[0]);
  }
  
  return pid;
}

int run_child(char **cmd) {
  // Start up the child.
  spawn_child(cmd);

  // Remove self from the foreground group.
  if (setpgid(0, 0) < 0)
    err(EXIT_FAILURE, "setpgid");

  // Wait for the child to finish.
  int status;
  if (wait(&status) < 0)
    err(EXIT_FAILURE, "wait");

  // Handle error events.
  int exit_code;
  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    warnx("child terminated: %s", strsignal(WTERMSIG(status)));
    exit_code = 1;
  } else {
    warnx("unhandled status code");
    abort();
  }

  return exit_code;
}
