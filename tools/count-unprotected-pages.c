#include <sys/ptrace.h>
#include <stdlib.h>
#include <err.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>

static int
wait_chk(void)
{
    int status;
    if (wait(&status) < 0)
        err(EXIT_FAILURE, "wait");
    if (WIFSIGNALED(status)) {
        errx(EXIT_FAILURE, "child signaled %s",
            strsignal(WTERMSIG(status)));
    }
    return status;
}

static void
usage(FILE *f, const char *prog)
{
    fprintf(f, "%s cmd [arg...]\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    char **cmd = &argv[1];

    const pid_t pid = fork();
    if (pid < 0) {
        err(EXIT_FAILURE, "fork");
    } else if (pid == 0) {
        if (ptrace(PTRACE_TRACEME) < 0)
            err(EXIT_FAILURE, "ptrace: TRACEME");
        execvp(cmd[0], cmd);
        err(EXIT_FAILURE, "execvp");
    }

    int status;

    status = wait_chk();
    if (!WIFSTOPPED(status)) {
        assert(WIFEXITED(status));
        errx(EXIT_FAILURE, "wait: child unexpectedly exited (%d)",
             WEXITSTATUS(status));
    }

    if (ptrace(PTRACE_CONT, pid, 0, 0) < 0)
        err(EXIT_FAILURE, "ptrace: CONT");

    status = wait_chk();
    assert(WIFEXITED(status));

    return WEXITSTATUS(status);
}
