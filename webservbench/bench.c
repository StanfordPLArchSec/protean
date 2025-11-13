#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sched.h>

#define SIEGE_PREFIX "../siege"
#define NGINX_PREFIX "../nginx"
#define TARGET_IP   "127.0.0.1"
#define TARGET_PORT 8443

static pid_t subproc(const char *args[]) {
  const pid_t pid = fork();
  if (pid < 0) {
    err(EXIT_FAILURE, "fork");
  } else if (pid == 0) {
    execvp(args[0], (char **) args);
    err(EXIT_FAILURE, "execvp");
  }
  return pid;
}

static void do_wait(pid_t pid) {
  int status;
  if (waitpid(pid, &status, 0) < 0)
    err(EXIT_FAILURE, "waitpid");
  if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS)
    return;
  errx(EXIT_FAILURE, "ERROR: subproc failed");
}

#if 0
static void add_to_path(const char *dir) {
  if ((dir = realpath(dir, NULL)) == NULL)
    err(EXIT_FAILURE, "realpath");
  const char *old_path = getenv("PATH");
  char *new_path;
  if (asprintf(&new_path, "%s:%s", old_path, dir) < 0)
    err(EXIT_FAILURE, "asprintf");
  setenv("PATH", new_path, 1);
}
#endif

static void init_conn(const char *target_ip, short target_port, struct sockaddr_in *addr) {
  memset(addr, 0, sizeof *addr);
  addr->sin_family = AF_INET;
  addr->sin_port = htons(target_port);
  if (!inet_pton(AF_INET, target_ip, &addr->sin_addr))
    errx(EXIT_FAILURE, "inet_pton: bad ip: %s", target_ip);
}

static bool check_conn(const struct sockaddr_in *addr) {
  const int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    err(EXIT_FAILURE, "socket");

  const int conn = connect(sock, (const struct sockaddr *) addr, sizeof *addr);
  if (conn >= 0 && close(conn) < 0)
    err(EXIT_FAILURE, "close");
  if (close(sock) < 0)
    err(EXIT_FAILURE, "close");

  return conn >= 0;
}

int main(int argc, char *argv[]) {
  if (argc != 2)
    errx(EXIT_FAILURE, "usage: %s num_requests", argv[0]);

#if 0
  // Add nginx, siege to path.
  add_to_path(SIEGE_PREFIX);
#endif

  const char *num_reqs = argv[1];

  // Start up the nginx server.
  const char *nginx_cmd[] = {NGINX_PREFIX "/sbin/nginx", NULL};
  const pid_t nginx_pid = subproc(nginx_cmd);

  // Wait until a socket is open on 8443.
  struct sockaddr_in addr;
  init_conn(TARGET_IP, TARGET_PORT, &addr);
  while (!check_conn(&addr)) {
    if (sched_yield() < 0)
      err(EXIT_FAILURE, "sched_yield");
  }

  // Start up the siege process.
  const char *curl_cmd[] = {"curl", "-vk", "https://127.0.0.1:8443/", NULL};
  const pid_t curl_pid = subproc(curl_cmd);

  // Wait for curl to finish.
  do_wait(curl_pid);

  // Kill nginx.
  if (kill(nginx_pid, SIGINT) < 0)
    err(EXIT_FAILURE, "kill");
  do_wait(nginx_pid);
}
