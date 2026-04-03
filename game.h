#ifndef GAME_H
#define GAME_H

#include <stdint.h>

typedef uint64_t board_t;

/* 初始化移动表和评估表（只需调用一次） */
void game_init(void);

/* 获取一个随机初始棋盘 */
board_t game_initial_board(void);

/* 执行移动，返回新棋盘（若无法移动则返回原棋盘） */
board_t game_execute_move(int move, board_t board);

/* 在合法移动后，向棋盘添加一个随机新方块（2 或 4） */
board_t game_add_random_tile(board_t board);

/* 检查游戏是否结束（无合法移动） */
int game_is_over(board_t board);

/* 获取棋盘中的最大数值（实际值，如 32768） */
int game_max_tile_value(board_t board);

/* 计算四个移动方向的期望分数（原始未归一化），不可移动的方向分数为0 */
void game_compute_scores(board_t board, float scores[4]);

/* 对 scores[4] 进行 softmax 归一化，得到置信度（和为1） */
void game_normalize_scores(float scores[4], float conf[4]);

/* 将棋盘转换为实际数值数组（长度为16，0表示空，否则为2,4,8,...） */
void game_board_to_values(board_t board, int values[16]);

#endif