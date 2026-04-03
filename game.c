#include "game.h"
#include "platform.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define ROW_MASK 0xFFFFULL
#define COL_MASK 0x000F000F000F000FULL

#define CPROB_THRESH_BASE 0.0001f
#define CACHE_DEPTH_LIMIT 15

/* 启发式评分权重 */
static const float SCORE_LOST_PENALTY = 200000.0f;
static const float SCORE_MONOTONICITY_POWER = 4.0f;
static const float SCORE_MONOTONICITY_WEIGHT = 47.0f;
static const float SCORE_SUM_POWER = 3.5f;
static const float SCORE_SUM_WEIGHT = 11.0f;
static const float SCORE_MERGES_WEIGHT = 700.0f;
static const float SCORE_EMPTY_WEIGHT = 270.0f;

/* 预计算表 */
static uint16_t row_left_table[65536];
static uint16_t row_right_table[65536];
static uint64_t col_up_table[65536];
static uint64_t col_down_table[65536];
static float heur_score_table[65536];
static float score_table[65536];

/* 哈希表（开放地址） */
#define HASH_SIZE (1 << 20)
#define HASH_MASK (HASH_SIZE - 1)

typedef struct
{
    uint64_t key;
    uint8_t depth;
    float heuristic;
    uint8_t used;
} hash_entry_t;

static hash_entry_t *hash_table = NULL;

static unsigned hash_func(uint64_t board)
{
    uint64_t x = board;
    x ^= x >> 32;
    x ^= x >> 16;
    x ^= x >> 8;
    return (unsigned)(x & HASH_MASK);
}

static void ht_init(void)
{
    if (hash_table)
        return;
    hash_table = (hash_entry_t *)calloc(HASH_SIZE, sizeof(hash_entry_t));
}

static void ht_clear(void)
{
    if (hash_table)
        memset(hash_table, 0, HASH_SIZE * sizeof(hash_entry_t));
}

static int ht_find(uint64_t board, uint8_t curdepth, float *heuristic)
{
    unsigned idx = hash_func(board);
    unsigned original = idx;
    do
    {
        if (!hash_table[idx].used)
            return 0;
        if (hash_table[idx].key == board)
        {
            if (hash_table[idx].depth <= curdepth)
            {
                *heuristic = hash_table[idx].heuristic;
                return 1;
            }
            return 0;
        }
        idx = (idx + 1) & HASH_MASK;
    } while (idx != original);
    return 0;
}

static void ht_insert(uint64_t board, uint8_t depth, float heuristic)
{
    unsigned idx = hash_func(board);
    unsigned original = idx;
    while (hash_table[idx].used && hash_table[idx].key != board)
    {
        idx = (idx + 1) & HASH_MASK;
        if (idx == original)
            return;
    }
    hash_table[idx].key = board;
    hash_table[idx].depth = depth;
    hash_table[idx].heuristic = heuristic;
    hash_table[idx].used = 1;
}

/* 辅助函数 */
static inline uint64_t transpose(uint64_t x)
{
    uint64_t a1 = x & 0xF0F00F0FF0F00F0FULL;
    uint64_t a2 = x & 0x0000F0F00000F0F0ULL;
    uint64_t a3 = x & 0x0F0F00000F0F0000ULL;
    uint64_t a = a1 | (a2 << 12) | (a3 >> 12);
    uint64_t b1 = a & 0xFF00FF0000FF00FFULL;
    uint64_t b2 = a & 0x00FF00FF00000000ULL;
    uint64_t b3 = a & 0x00000000FF00FF00ULL;
    return b1 | (b2 >> 24) | (b3 << 24);
}

static inline int count_empty(uint64_t x)
{
    x |= (x >> 2) & 0x3333333333333333ULL;
    x |= (x >> 1);
    x = ~x & 0x1111111111111111ULL;
    x += x >> 32;
    x += x >> 16;
    x += x >> 8;
    x += x >> 4;
    return x & 0xf;
}

static inline uint64_t unpack_col(uint16_t row)
{
    uint64_t tmp = row;
    return (tmp | (tmp << 12) | (tmp << 24) | (tmp << 36)) & COL_MASK;
}

static inline uint16_t reverse_row(uint16_t row)
{
    return (row >> 12) | ((row >> 4) & 0x00F0) | ((row << 4) & 0x0F00) | (row << 12);
}

static inline uint64_t execute_move_0(uint64_t board)
{
    uint64_t ret = board;
    uint64_t t = transpose(board);
    ret ^= col_up_table[(t >> 0) & ROW_MASK] << 0;
    ret ^= col_up_table[(t >> 16) & ROW_MASK] << 4;
    ret ^= col_up_table[(t >> 32) & ROW_MASK] << 8;
    ret ^= col_up_table[(t >> 48) & ROW_MASK] << 12;
    return ret;
}

static inline uint64_t execute_move_1(uint64_t board)
{
    uint64_t ret = board;
    uint64_t t = transpose(board);
    ret ^= col_down_table[(t >> 0) & ROW_MASK] << 0;
    ret ^= col_down_table[(t >> 16) & ROW_MASK] << 4;
    ret ^= col_down_table[(t >> 32) & ROW_MASK] << 8;
    ret ^= col_down_table[(t >> 48) & ROW_MASK] << 12;
    return ret;
}

static inline uint64_t execute_move_2(uint64_t board)
{
    uint64_t ret = board;
    ret ^= (uint64_t)row_left_table[(board >> 0) & ROW_MASK] << 0;
    ret ^= (uint64_t)row_left_table[(board >> 16) & ROW_MASK] << 16;
    ret ^= (uint64_t)row_left_table[(board >> 32) & ROW_MASK] << 32;
    ret ^= (uint64_t)row_left_table[(board >> 48) & ROW_MASK] << 48;
    return ret;
}

static inline uint64_t execute_move_3(uint64_t board)
{
    uint64_t ret = board;
    ret ^= (uint64_t)row_right_table[(board >> 0) & ROW_MASK] << 0;
    ret ^= (uint64_t)row_right_table[(board >> 16) & ROW_MASK] << 16;
    ret ^= (uint64_t)row_right_table[(board >> 32) & ROW_MASK] << 32;
    ret ^= (uint64_t)row_right_table[(board >> 48) & ROW_MASK] << 48;
    return ret;
}

board_t game_execute_move(int move, uint64_t board)
{
    switch (move)
    {
    case 0:
        return execute_move_0(board);
    case 1:
        return execute_move_1(board);
    case 2:
        return execute_move_2(board);
    case 3:
        return execute_move_3(board);
    default:
        return ~0ULL;
    }
}

static inline int get_max_rank(uint64_t board)
{
    int maxr = 0;
    while (board)
    {
        int r = board & 0xf;
        if (r > maxr)
            maxr = r;
        board >>= 4;
    }
    return maxr;
}

int game_max_tile_value(uint64_t board)
{
    int rank = get_max_rank(board);
    return (rank == 0) ? 0 : (1 << rank);
}

static inline int count_distinct_tiles(uint64_t board)
{
    uint16_t bitset = 0;
    while (board)
    {
        bitset |= 1 << (board & 0xf);
        board >>= 4;
    }
    bitset >>= 1;
    int count = 0;
    while (bitset)
    {
        bitset &= bitset - 1;
        count++;
    }
    return count;
}

static inline float score_helper(uint64_t board, const float *table)
{
    return table[(board >> 0) & ROW_MASK] + table[(board >> 16) & ROW_MASK] + table[(board >> 32) & ROW_MASK] + table[(board >> 48) & ROW_MASK];
}

static inline float score_heur_board(uint64_t board)
{
    return score_helper(board, heur_score_table) + score_helper(transpose(board), heur_score_table);
}

/* 搜索状态 */
typedef struct
{
    int maxdepth;
    int curdepth;
    int cachehits;
    unsigned long moves_evaled;
    int depth_limit;
} eval_state_t;

static float score_tilechoose_node(eval_state_t *state, uint64_t board, float cprob);
static float score_move_node(eval_state_t *state, uint64_t board, float cprob);

static float score_tilechoose_node(eval_state_t *state, uint64_t board, float cprob)
{
    if (cprob < CPROB_THRESH_BASE || state->curdepth >= state->depth_limit)
    {
        if (state->curdepth > state->maxdepth)
            state->maxdepth = state->curdepth;
        return score_heur_board(board);
    }

    if (state->curdepth < CACHE_DEPTH_LIMIT)
    {
        float heur;
        if (ht_find(board, state->curdepth, &heur))
        {
            state->cachehits++;
            return heur;
        }
    }

    int num_open = count_empty(board);
    cprob /= num_open;

    float res = 0.0f;
    uint64_t tmp = board;
    uint64_t tile_2 = 1;
    while (tile_2)
    {
        if ((tmp & 0xf) == 0)
        {
            res += score_move_node(state, board | tile_2, cprob * 0.9f) * 0.9f;
            res += score_move_node(state, board | (tile_2 << 1), cprob * 0.1f) * 0.1f;
        }
        tmp >>= 4;
        tile_2 <<= 4;
    }
    res = res / num_open;

    if (state->curdepth < CACHE_DEPTH_LIMIT)
    {
        ht_insert(board, (uint8_t)state->curdepth, res);
    }

    return res;
}

static float score_move_node(eval_state_t *state, uint64_t board, float cprob)
{
    float best = 0.0f;
    state->curdepth++;
    for (int move = 0; move < 4; ++move)
    {
        uint64_t newboard = game_execute_move(move, board);
        state->moves_evaled++;
        if (board != newboard)
        {
            float s = score_tilechoose_node(state, newboard, cprob);
            if (s > best)
                best = s;
        }
    }
    state->curdepth--;
    return best;
}

static float score_move(uint64_t board, int move, eval_state_t *state)
{
    uint64_t newboard = game_execute_move(move, board);
    if (board == newboard)
        return 0.0f;
    return score_tilechoose_node(state, newboard, 1.0f) + 1e-6f;
}

void game_compute_scores(uint64_t board, float scores[4])
{
    eval_state_t state;
    memset(&state, 0, sizeof(state));
    state.depth_limit = count_distinct_tiles(board) - 2;
    if (state.depth_limit < 3)
        state.depth_limit = 3;

    ht_init();
    ht_clear();

    for (int move = 0; move < 4; ++move)
    {
        scores[move] = score_move(board, move, &state);
    }
}

void game_normalize_scores(float scores[4], float conf[4])
{
    float max_score = scores[0];
    for (int i = 1; i < 4; ++i)
        if (scores[i] > max_score)
            max_score = scores[i];
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        float e = expf(scores[i] - max_score);
        conf[i] = e;
        sum += e;
    }
    if (sum > 0.0f)
    {
        for (int i = 0; i < 4; ++i)
            conf[i] /= sum;
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            conf[i] = 0.25f;
    }
}

void game_board_to_values(uint64_t board, int values[16])
{
    for (int i = 0; i < 16; ++i)
    {
        int rank = (board >> (4 * i)) & 0xf;
        values[i] = (rank == 0) ? 0 : (1 << rank);
    }
}

int game_is_over(uint64_t board)
{
    for (int move = 0; move < 4; ++move)
    {
        if (game_execute_move(move, board) != board)
            return 0;
    }
    return 1;
}

static int draw_tile_rank(void)
{
    return (unif_random(10) < 9) ? 1 : 2;
}

static uint64_t insert_tile_rand(uint64_t board, int rank)
{
    int index = unif_random(count_empty(board));
    uint64_t tmp = board;
    uint64_t tile = (uint64_t)rank;
    while (1)
    {
        while ((tmp & 0xf) != 0)
        {
            tmp >>= 4;
            tile <<= 4;
        }
        if (index == 0)
            break;
        --index;
        tmp >>= 4;
        tile <<= 4;
    }
    return board | tile;
}

board_t game_add_random_tile(board_t board)
{
    if (count_empty(board) == 0)
        return board;
    int rank = draw_tile_rank();
    return insert_tile_rand(board, rank);
}

uint64_t game_initial_board(void)
{
    uint64_t board = draw_tile_rank() << (4 * unif_random(16));
    return insert_tile_rand(board, draw_tile_rank());
}

void game_init(void)
{
    for (unsigned row = 0; row < 65536; ++row)
    {
        unsigned line[4] = {
            (row >> 0) & 0xf,
            (row >> 4) & 0xf,
            (row >> 8) & 0xf,
            (row >> 12) & 0xf};

        float score = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            int rank = line[i];
            if (rank >= 2)
                score += (rank - 1) * (1 << rank);
        }
        score_table[row] = score;

        float sum = 0.0f;
        int empty = 0, merges = 0, prev = 0, counter = 0;
        for (int i = 0; i < 4; ++i)
        {
            int rank = line[i];
            sum += powf((float)rank, SCORE_SUM_POWER);
            if (rank == 0)
            {
                empty++;
            }
            else
            {
                if (prev == rank)
                    counter++;
                else if (counter > 0)
                {
                    merges += 1 + counter;
                    counter = 0;
                }
                prev = rank;
            }
        }
        if (counter > 0)
            merges += 1 + counter;

        float mono_left = 0.0f, mono_right = 0.0f;
        for (int i = 1; i < 4; ++i)
        {
            if (line[i - 1] > line[i])
            {
                mono_left += powf((float)line[i - 1], SCORE_MONOTONICITY_POWER) - powf((float)line[i], SCORE_MONOTONICITY_POWER);
            }
            else
            {
                mono_right += powf((float)line[i], SCORE_MONOTONICITY_POWER) - powf((float)line[i - 1], SCORE_MONOTONICITY_POWER);
            }
        }
        float mono = mono_left < mono_right ? mono_left : mono_right;
        heur_score_table[row] = SCORE_LOST_PENALTY + SCORE_EMPTY_WEIGHT * empty + SCORE_MERGES_WEIGHT * merges - SCORE_MONOTONICITY_WEIGHT * mono - SCORE_SUM_WEIGHT * sum;

        unsigned tmp_line[4] = {line[0], line[1], line[2], line[3]};
        for (int i = 0; i < 3; ++i)
        {
            int j;
            for (j = i + 1; j < 4; ++j)
                if (tmp_line[j] != 0)
                    break;
            if (j == 4)
                break;
            if (tmp_line[i] == 0)
            {
                tmp_line[i] = tmp_line[j];
                tmp_line[j] = 0;
                i--;
            }
            else if (tmp_line[i] == tmp_line[j])
            {
                if (tmp_line[i] != 0xf)
                    tmp_line[i]++;
                tmp_line[j] = 0;
            }
        }
        uint16_t result = (tmp_line[0] << 0) | (tmp_line[1] << 4) | (tmp_line[2] << 8) | (tmp_line[3] << 12);
        uint16_t rev_result = reverse_row(result);
        unsigned rev_row = reverse_row(row);

        row_left_table[row] = row ^ result;
        row_right_table[rev_row] = rev_row ^ rev_result;
        col_up_table[row] = unpack_col(row) ^ unpack_col(result);
        col_down_table[rev_row] = unpack_col(rev_row) ^ unpack_col(rev_result);
    }
}