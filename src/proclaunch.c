#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "proclaunch.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

static char *build_cmdline(const char *const argv[])
{
    size_t total = 1;
    int i;
    for (i = 0; argv[i]; i++) total += strlen(argv[i]) * 2 + 4;
    char *buf = malloc(total), *p = buf;
    if (!buf) return NULL;
    for (i = 0; argv[i]; i++) {
        const char *a = argv[i];
        if (i) *p++ = ' ';
        if (*a && !strpbrk(a, " \t\n\v\"")) {
            size_t n = strlen(a);
            memcpy(p, a, n);
            p += n;
            continue;
        }
        *p++ = '"';
        for (;;) {
            size_t bs = 0, k;
            while (*a == '\\') { a++; bs++; }
            if (!*a) {
                for (k = 0; k < bs * 2; k++) *p++ = '\\';
                break;
            }
            for (k = 0; k < bs * (*a == '"' ? 2 : 1); k++) *p++ = '\\';
            if (*a == '"') *p++ = '\\';
            *p++ = *a++;
        }
        *p++ = '"';
    }
    *p = '\0';
    return buf;
}

int pl_start(const char *const argv[], pl_process *proc)
{
    if (!argv || !argv[0] || !proc) return -1;
    char *cmd = build_cmdline(argv);
    if (!cmd) return -1;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok) return -1;

    CloseHandle(pi.hThread);
    proc->handle = (intptr_t)pi.hProcess;
    proc->pid = (long)pi.dwProcessId;
    return 0;
}

static int finish(pl_process *proc, int *exit_code)
{
    DWORD code;
    if (!GetExitCodeProcess((HANDLE)proc->handle, &code)) return -1;
    if (exit_code) *exit_code = (int)code;
    CloseHandle((HANDLE)proc->handle);
    proc->handle = 0;
    return 0;
}

int pl_wait(pl_process *proc, int *exit_code)
{
    if (!proc || !proc->handle) return -1;
    if (WaitForSingleObject((HANDLE)proc->handle, INFINITE) != WAIT_OBJECT_0) return -1;
    return finish(proc, exit_code);
}

int pl_poll(pl_process *proc, int *exit_code)
{
    if (!proc || !proc->handle) return -1;
    DWORD r = WaitForSingleObject((HANDLE)proc->handle, 0);
    if (r == WAIT_TIMEOUT) return 0;
    if (r != WAIT_OBJECT_0) return -1;
    return finish(proc, exit_code) == 0 ? 1 : -1;
}

#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

int pl_start(const char *const argv[], pl_process *proc)
{
    if (!argv || !argv[0] || !proc) return -1;
    int fds[2];
    if (pipe(fds) != 0) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        int e = errno;
        ssize_t w = write(fds[1], &e, sizeof e);
        (void)w;
        _exit(127);
    }

    close(fds[1]);
    int err = 0;
    ssize_t n;
    do {
        n = read(fds[0], &err, sizeof err);
    } while (n < 0 && errno == EINTR);
    close(fds[0]);

    if (n > 0) {
        int st;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        return -1;
    }
    proc->pid = (long)pid;
    proc->handle = 0;
    return 0;
}

static int decode(int status)
{
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

int pl_wait(pl_process *proc, int *exit_code)
{
    if (!proc || proc->pid <= 0) return -1;
    int status;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) return -1;
    if (exit_code) *exit_code = decode(status);
    proc->pid = 0;
    return 0;
}

int pl_poll(pl_process *proc, int *exit_code)
{
    if (!proc || proc->pid <= 0) return -1;
    int status;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, WNOHANG);
    } while (r < 0 && errno == EINTR);
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (exit_code) *exit_code = decode(status);
    proc->pid = 0;
    return 1;
}
#endif
