#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

unsigned unif_random(unsigned n);
int platform_mkdir(const char *path);
int platform_count_csv_files(const char *dir);
typedef void *lock_handle_t;
lock_handle_t platform_lock_file(const char *lockpath);
void platform_unlock_file(lock_handle_t lock);
typedef void *process_handle_t;
process_handle_t platform_spawn(const char *exe_path, char *const argv[]);
void platform_wait_for_processes(process_handle_t *handles, int count);
void platform_unique_filename(char *buf, size_t bufsz, const char *dir, const char *suffix);
void platform_sleep(unsigned seconds);
int platform_get_executable_path(char *buf, size_t bufsz);
void platform_set_env(const char *name, const char *value);
void platform_terminate_process(process_handle_t handle);

#endif