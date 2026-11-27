#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "proclaunch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

#define TRICKY_ARG "hello \"big\" world\\"

static int failed = 0;

static void check(int ok, const char *name, const char *details)
{
    printf("[%s] %s%s%s\n", ok ? " OK " : "FAIL", name, *details ? " - " : "", details);
    if (!ok) failed++;
}

static int run(const char *const argv[], int *code)
{
    pl_process p;
    return pl_start(argv, &p) == 0 && pl_wait(&p, code) == 0;
}

int main(int argc, char *argv[])
{
    if (argc >= 4 && strcmp(argv[1], "--child") == 0) {
        sleep_ms(atoi(argv[3]));
        return atoi(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "--argcheck") == 0)
        return strcmp(argv[2], TRICKY_ARG) == 0 ? 0 : 1;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    const char *self = argv[0];
    char buf[64];
    int code = -1, ok;

    const char *a1[] = { self, "--child", "42", "1000", NULL };
    pl_process p;
    int iters = 0, r = 0;
    ok = pl_start(a1, &p) == 0;
    while (ok && (r = pl_poll(&p, &code)) == 0) { iters++; sleep_ms(100); }
    snprintf(buf, sizeof buf, "итераций: %d, код: %d", iters, code);
    check(ok && r == 1 && iters > 3 && code == 42, "1. фоновый запуск + pl_poll", buf);

    const char *a2[] = { self, "--child", "7", "300", NULL };
    code = -1;
    ok = run(a2, &code);
    snprintf(buf, sizeof buf, "код: %d", code);
    check(ok && code == 7, "2. pl_wait и код возврата", buf);

    const char *a3[] = { self, "--argcheck", TRICKY_ARG, NULL };
    code = -1;
    ok = run(a3, &code);
    check(ok && code == 0, "3. аргумент с пробелами и кавычками", "");

    const char *a4[] = { "no_such_program_12345", NULL };
    check(pl_start(a4, &p) == -1, "4. несуществующая программа", "");

    const char *a5[] = { self, "--child", "300", "0", NULL };
#ifdef _WIN32
    int expected = 300;
#else
    int expected = 44;
#endif
    code = -1;
    ok = run(a5, &code);
    snprintf(buf, sizeof buf, "код: %d (ожидалось %d)", code, expected);
    check(ok && code == expected, "5. код 300", buf);

    if (failed) printf("\nПровалено тестов: %d\n", failed);
    else printf("\nВсе тесты пройдены\n");
    return failed ? 1 : 0;
}
