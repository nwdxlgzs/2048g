#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "game.h"
#include "platform.h"

#ifdef _WIN32
#include <windows.h>
#endif

static int g_fork_count = 1;
static char g_save_dir[512] = "game_records";
static int g_min_tile = 32768;
static int g_max_records = 64;
static int g_stop_step = 0;
static volatile int g_stop = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_stop = 1;
}

#define INIT_HISTORY_CAP 4096

static void record_game(uint64_t board_history[], float conf_history[][4], int step_count)
{
    char lockpath[1024];
    snprintf(lockpath, sizeof(lockpath), "%s/.lock", g_save_dir);
    lock_handle_t lock = platform_lock_file(lockpath);

    while (1)
    {
        int cur_count = platform_count_csv_files(g_save_dir);
        if (cur_count < g_max_records)
            break;
        platform_unlock_file(lock);
        platform_sleep(1);
        lock = platform_lock_file(lockpath);
    }

    char filename[1024];
    platform_unique_filename(filename, sizeof(filename), g_save_dir, ".csv");

    FILE *f = fopen(filename, "w");
    if (!f)
    {
        platform_unlock_file(lock);
        fprintf(stderr, "Failed to create %s\n", filename);
        return;
    }

    for (int step = 0; step < step_count; ++step)
    {
        int values[16];
        game_board_to_values(board_history[step], values);
        fprintf(f, "%d", step);
        for (int i = 0; i < 16; ++i)
            fprintf(f, ",%d", values[i]);
        for (int d = 0; d < 4; ++d)
            fprintf(f, ",%f", conf_history[step][d]);
        fprintf(f, "\n");
    }

    fclose(f);
    platform_unlock_file(lock);
    printf("Saved game with %d steps (max tile %d)\n", step_count, g_min_tile);
}

static void worker_loop(void)
{
    /* 子进程也注册信号，以便 Ctrl+C 能终止 */
    signal(SIGINT, signal_handler);
#ifdef _WIN32
    signal(SIGTERM, signal_handler);
#endif

    game_init();

    while (!g_stop)
    {
        uint64_t board = game_initial_board();
        int step = 0;
        int record = 0;
        int max_tile_seen = 0;

        size_t cap = INIT_HISTORY_CAP;
        uint64_t *board_history = malloc(cap * sizeof(uint64_t));
        float (*conf_history)[4] = malloc(cap * sizeof(float[4]));
        if (!board_history || !conf_history)
        {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }

        while (!game_is_over(board))
        {
            int cur_max = game_max_tile_value(board);
            if (cur_max > max_tile_seen)
                max_tile_seen = cur_max;

            float scores[4];
            game_compute_scores(board, scores);
            float conf[4];
            game_normalize_scores(scores, conf);

            /* 扩容 */
            if (step >= (int)cap)
            {
                cap *= 2;
                uint64_t *new_board = realloc(board_history, cap * sizeof(uint64_t));
                float (*new_conf)[4] = realloc(conf_history, cap * sizeof(float[4]));
                if (!new_board || !new_conf)
                {
                    free(board_history);
                    free(conf_history);
                    fprintf(stderr, "realloc failed\n");
                    return;
                }
                board_history = new_board;
                conf_history = new_conf;
            }

            board_history[step] = board;
            memcpy(conf_history[step], conf, sizeof(conf));
            step++;

            /* 检查是否需要记录（基于当前步数或最大数值） */
            if (!record && (cur_max >= g_min_tile || (g_stop_step > 0 && step >= g_stop_step)))
            {
                record = 1;
            }

            /* 如果达到停止步数，直接结束对局，不再移动 */
            if (g_stop_step > 0 && step >= g_stop_step)
            {
                break;
            }

            /* 选择最佳移动 */
            int best_move = 0;
            float best_score = scores[0];
            for (int m = 1; m < 4; ++m)
            {
                if (scores[m] > best_score)
                {
                    best_score = scores[m];
                    best_move = m;
                }
            }
            uint64_t old_board = board;
            board = game_execute_move(best_move, board);
            /* 只有棋盘发生变化时，才添加随机新方块 */
            if (board != old_board)
            {
                board = game_add_random_tile(board);
            }
        }

        if (record)
        {
            record_game(board_history, conf_history, step);
        }

        free(board_history);
        free(conf_history);
    }
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "  --fork N            Number of worker processes (default 1)\n");
    fprintf(stderr, "  --save_dir PATH     Directory to save CSV files (default game_records/)\n");
    fprintf(stderr, "  --min_tile VALUE    Minimum tile value to record game (default 32768)\n");
    fprintf(stderr, "  --max_records N     Max CSV files in save_dir (default 64)\n");
    fprintf(stderr, "  --stop STEPS        Stop after this many steps and record (default 0 = unlimited)\n");
    fprintf(stderr, "  --help              Show this help\n");
}

static void parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--fork") == 0 && i + 1 < argc)
        {
            g_fork_count = atoi(argv[++i]);
            if (g_fork_count < 1)
                g_fork_count = 1;
        }
        else if (strcmp(argv[i], "--save_dir") == 0 && i + 1 < argc)
        {
            strncpy(g_save_dir, argv[++i], sizeof(g_save_dir) - 1);
            g_save_dir[sizeof(g_save_dir) - 1] = '\0';
        }
        else if (strcmp(argv[i], "--min_tile") == 0 && i + 1 < argc)
        {
            g_min_tile = atoi(argv[++i]);
            if (g_min_tile < 4)
                g_min_tile = 4;
        }
        else if (strcmp(argv[i], "--max_records") == 0 && i + 1 < argc)
        {
            g_max_records = atoi(argv[++i]);
            if (g_max_records < 1)
                g_max_records = 1;
        }
        else if (strcmp(argv[i], "--stop") == 0 && i + 1 < argc)
        {
            g_stop_step = atoi(argv[++i]);
            if (g_stop_step < 0)
                g_stop_step = 0;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            exit(0);
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

int main(int argc, char **argv)
{
    parse_args(argc, argv);

    if (platform_mkdir(g_save_dir) != 0 && errno != EEXIST)
    {
        perror("mkdir");
        return 1;
    }

    signal(SIGINT, signal_handler);
#ifdef _WIN32
    signal(SIGTERM, signal_handler);
#endif

    /* 检查是否已经以工作进程方式运行 */
    char *worker_flag = getenv("B2048_WORKER");
    if (worker_flag && strcmp(worker_flag, "1") == 0)
    {
        worker_loop();
        return 0;
    }

    /* 获取当前可执行文件路径（使用平台封装） */
    char exe_path[1024];
    if (platform_get_executable_path(exe_path, sizeof(exe_path)) != 0)
    {
        fprintf(stderr, "Failed to get executable path\n");
        return 1;
    }

    process_handle_t *handles = malloc(g_fork_count * sizeof(process_handle_t));
    if (!handles)
        return 1;

    for (int i = 0; i < g_fork_count; ++i)
    {
        platform_set_env("B2048_WORKER", "1");
        handles[i] = platform_spawn(exe_path, argv);
        platform_set_env("B2048_WORKER", NULL);
    }

    /* 可中断等待子进程结束 */
    int active = g_fork_count;
    while (active > 0 && !g_stop)
    {
#ifdef _WIN32
        DWORD ret = WaitForMultipleObjects(active, (HANDLE *)handles, FALSE, 100);
        if (ret == WAIT_TIMEOUT)
        {
            continue;
        }
        else if (ret >= WAIT_OBJECT_0 && ret < WAIT_OBJECT_0 + active)
        {
            CloseHandle(handles[ret - WAIT_OBJECT_0]);
            /* 移除已结束的句柄 */
            for (int j = ret - WAIT_OBJECT_0; j < active - 1; ++j)
                handles[j] = handles[j + 1];
            active--;
        }
        else
        {
            break;
        }
#else
        pid_t pid = waitpid(-1, NULL, WNOHANG);
        if (pid == 0)
        {
            usleep(100000); /* 0.1秒 */
            continue;
        }
        else if (pid > 0)
        {
            /* 移除已结束的 pid */
            for (int i = 0; i < active; ++i)
            {
                if ((pid_t)(intptr_t)handles[i] == pid)
                {
                    for (int j = i; j < active - 1; ++j)
                        handles[j] = handles[j + 1];
                    break;
                }
            }
            active--;
        }
        else if (pid == -1 && errno != ECHILD)
        {
            break;
        }
#endif
    }

    /* 如果是因为 g_stop 退出，终止所有剩余子进程 */
    if (g_stop)
    {
        for (int i = 0; i < active; ++i)
        {
            platform_terminate_process(handles[i]);
        }
        /* 等待它们真正结束 */
        platform_wait_for_processes(handles, active);
    }

    free(handles);
    return 0;
}