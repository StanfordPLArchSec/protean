#include <sys/ptrace.h>
#include <linux/ptrace.h>
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

    // Trace all syscalls.
    int sig = 0;
    while (1) {
        if (ptrace(PTRACE_SYSCALL, pid, 0, sig) < 0)
            err(EXIT_FAILURE, "ptrace: SYSCALL");
        status = wait_chk();
        if (WIFEXITED(status))
            break;
        assert(WIFSTOPPED(status));
        sig = WSTOPSIG(status);
        if (sig != SIGTRAP)
            continue;
        sig = 0;

        // Stopped at syscall.
        struct ptrace_syscall_info sys;
        if (ptrace(PTRACE_GET_SYSCALL_INFO, pid, sizeof sys, &sys) < 0)
            err(EXIT_FAILURE, "GET_SYSCALL_INFO");
    }

    return WEXITSTATUS(status);
}
