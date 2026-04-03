# 2048g - 2048 AI Training Data Generator

[中文](./README.md) | English

## Introduction

`2048g` is a high-performance 2048 game data generation tool designed to provide training data for AI models. It uses an expectimax search algorithm combined with bitboard representation, capable of evaluating millions of positions per second. The program continuously generates game episodes, saves only those that reach a specified maximum tile (e.g., 32768), and outputs the board state and AI move confidence for each step as a CSV file.

## Compilation

### Using make

```bash
make
```

### Manual compilation

```bash
# Windows
gcc -O3 -Wall -Wextra -std=c99 -D_GNU_SOURCE=1 -o 2048g 2048g.c game.c platform.c -lm
# Linux
gcc -O3 -Wall -Wextra -std=c99 -fPIC -D_GNU_SOURCE -o 2048g 2048g.c game.c platform.c -lm
```

## Usage

```bash
./2048g [options]
```

### Options

| Option            | Description                                               | Default         |
| ----------------- | --------------------------------------------------------- | --------------- |
| `--fork N`        | Start N concurrent worker processes                      | 1               |
| `--save_dir PATH` | Directory to save CSV files                               | `game_records/` |
| `--min_tile N`    | Minimum tile value required to save the game (e.g., 32768) | 32768           |
| `--max_records N` | Maximum number of CSV files to keep in the directory     | 64              |
| `--stop N`        | Stop after N steps (0 = natural end)                     | 0               |
| `--target_tile N` | Stop immediately when a tile of this value appears       | 0 (disabled)    |
| `--help`          | Show this help message                                   |                 |

> Save priority: `--target_tile` and `--stop` compete as stopping conditions, but both override `--min_tile`.

### Examples

```bash
# Start 16 processes, save only when 32768 is reached, keep at most 64 files
2048g --fork 16 --save_dir game_records/ --min_tile 32768 --max_records 64 --stop 0 --target_tile 0

# Start 8 processes, save when 2048 is reached, keep at most 8 files, stop each game after 2000 steps
2048g --fork 8 --min_tile 2048 --max_records 8 --stop 2000
```

## Output CSV Format

CSV files have **no header row**. Each line corresponds to one step in the game, with the following format:

```
step,cell0,cell1,...,cell15,conf_up,conf_down,conf_left,conf_right
```

- **step**: Incremental step number starting from 0.
- **cell0~cell15**: Row-major order, values are actual tile numbers (2,4,8,...), 0 means empty.
- **confidence**: Softmax-normalized probabilities for the four directions (up, down, left, right), summing to 1. If a move is illegal, its raw score is 0, but after normalization it may still receive a small probability.
- Typically one confidence is close to 1 and the other three are close to 0; occasionally two values may be around 0.5.

Example:
```
0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.234,0.123,0.456,0.187
```
Interpretation: step 0, cell1 = 2 (others empty), up confidence 0.234, down 0.123, left 0.456, right 0.187.

## Stopping the Program

Press `Ctrl+C` to safely stop all worker processes.

## Integration with Training Scripts

You can write a separate training script that periodically scans the `--save_dir` directory, reads CSV files, and then deletes or moves them to a backup location for continued training. `2048g` automatically manages the number of files and will not exceed `--max_records`.
