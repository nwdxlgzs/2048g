#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <direct.h>
typedef HANDLE os_lock_t;
typedef HANDLE os_process_t;
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <limits.h>
#include <sys/wait.h>
typedef int os_lock_t;
typedef pid_t os_process_t;
#endif

static uint64_t rng_state = 0;

static void init_rng(void)
{
    if (rng_state == 0)
    {
        rng_state = (uint64_t)time(NULL) ^ (uint64_t)clock();
#ifdef _WIN32
        rng_state ^= (uint64_t)GetCurrentProcessId();
#else
        rng_state ^= (uint64_t)getpid();
#endif
    }
}

unsigned unif_random(unsigned n)
{
    init_rng();
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    uint64_t r = rng_state * 0x2545F4914F6CDD1DULL;
    return (unsigned)(r % n);
}

int platform_mkdir(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

int platform_count_csv_files(const char *dir)
{
    int count = 0;
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.csv", dir);
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            count++;
    } while (FindNextFile(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode))
        {
            const char *ext = strrchr(ent->d_name, '.');
            if (ext && strcmp(ext, ".csv") == 0)
                count++;
        }
    }
    closedir(d);
#endif
    return count;
}

lock_handle_t platform_lock_file(const char *lockpath)
{
#ifdef _WIN32
    HANDLE h = CreateFile(lockpath, GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    while (1)
    {
        OVERLAPPED ov = {0};
        if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov))
            break;
        Sleep(100);
    }
    return (lock_handle_t)h;
#else
    int fd = open(lockpath, O_CREAT | O_RDWR, 0666);
    if (fd < 0)
        return (lock_handle_t)(intptr_t)-1;
    while (flock(fd, LOCK_EX) != 0)
    {
        usleep(100000);
    }
    return (lock_handle_t)(intptr_t)fd;
#endif
}

void platform_unlock_file(lock_handle_t lock)
{
#ifdef _WIN32
    HANDLE h = (HANDLE)lock;
    if (h)
    {
        UnlockFile(h, 0, 0, 1, 0);
        CloseHandle(h);
    }
#else
    int fd = (int)(intptr_t)lock;
    if (fd >= 0)
    {
        flock(fd, LOCK_UN);
        close(fd);
    }
#endif
}

process_handle_t platform_spawn(const char *exe_path, char *const argv[])
{
#ifdef _WIN32
    size_t cmdlen = strlen(exe_path) + 2;
    for (int i = 1; argv[i] != NULL; ++i)
    {
        cmdlen += strlen(argv[i]) + 3;
    }
    char *cmdline = (char *)malloc(cmdlen);
    char *p = cmdline;
    p += sprintf(p, "\"%s\"", exe_path);
    for (int i = 1; argv[i] != NULL; ++i)
    {
        if (strchr(argv[i], ' ') != NULL)
        {
            p += sprintf(p, " \"%s\"", argv[i]);
        }
        else
        {
            p += sprintf(p, " %s", argv[i]);
        }
    }
    STARTUPINFO si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    if (!CreateProcess(NULL, cmdline, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        free(cmdline);
        return NULL;
    }
    free(cmdline);
    CloseHandle(pi.hThread);
    return (process_handle_t)pi.hProcess;
#else
    pid_t pid = fork();
    if (pid == 0)
    {
        execv(exe_path, argv);
        perror("execv");
        exit(1);
    }
    if (pid == -1)
        return (process_handle_t)(intptr_t)-1;
    return (process_handle_t)(intptr_t)pid;
#endif
}

void platform_wait_for_processes(process_handle_t *handles, int count)
{
#ifdef _WIN32
    HANDLE *harr = (HANDLE *)handles;
    WaitForMultipleObjects(count, harr, TRUE, INFINITE);
    for (int i = 0; i < count; ++i)
        CloseHandle(harr[i]);
#else
    for (int i = 0; i < count; ++i)
    {
        pid_t pid = (pid_t)(intptr_t)handles[i];
        if (pid > 0)
            waitpid(pid, NULL, 0);
    }
#endif
}

void platform_unique_filename(char *buf, size_t bufsz, const char *dir, const char *suffix)
{
    init_rng();
    uint64_t pid;
#ifdef _WIN32
    pid = (uint64_t)GetCurrentProcessId();
#else
    pid = (uint64_t)getpid();
#endif
    uint64_t randval = unif_random(0x7FFFFFFF);
    uint64_t ts = (uint64_t)time(NULL) ^ (uint64_t)clock();
    snprintf(buf, bufsz, "%s/%llx_%llx_%llx%s", dir,
             (unsigned long long)pid, (unsigned long long)ts,
             (unsigned long long)randval, suffix);
}

void platform_sleep(unsigned seconds)
{
#ifdef _WIN32
    Sleep(seconds * 1000);
#else
    sleep(seconds);
#endif
}

int platform_get_executable_path(char *buf, size_t bufsz)
{
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)bufsz);
    if (len == 0 || len == bufsz)
        return -1;
    return 0;
#else
    ssize_t len = readlink("/proc/self/exe", buf, bufsz - 1);
    if (len == -1)
        return -1;
    buf[len] = '\0';
    return 0;
#endif
}

void platform_set_env(const char *name, const char *value)
{
#ifdef _WIN32
    SetEnvironmentVariableA(name, value);
#else
    setenv(name, value, 1);
#endif
}

void platform_terminate_process(process_handle_t handle)
{
#ifdef _WIN32
    TerminateProcess((HANDLE)handle, 1);
#else
    kill((pid_t)(intptr_t)handle, SIGTERM);
#endif
}