#include <syscall.h>
#include "tests/main.h"
#include "tests/lib.h"
#include "lib/user/threadpool.h"
#include "lib/user/mm.h"
#include <stdio.h>
#include <string.h>

#define NUM_THREADS 32
static int N = 14; // max 18

char mymemory[256 * 1024 * 1024]; // set chunk of memory
pthread_mutex_t mem_lock;

#define MAX_N (18)
#define WORD_BITS (sizeof(long) * 8)
#define MAX_LONGS ((MAX_N + WORD_BITS - 1) / WORD_BITS * MAX_N)

static int max_parallel_depth = 6;
static int valid_solutions[] = {0, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200,
                                73712, 365596, 2279184, 14772512, 95815104, 666090624};

struct board {
    long bits[MAX_LONGS];
};

struct board_state {
    struct board board;
    int N;
    int row;
}; 

static bool is_queen(struct board* board, int x, int y, int N) {
    if (x < 0 || x >= N || y < 0 || y >= N) {
        return false;
    }
    long long idx = x * N + y;
    return (board->bits[idx / WORD_BITS] & (1UL << (idx % WORD_BITS))) ==
        (1UL << (idx % WORD_BITS));
}

static void set_queen(struct board* board, int x, int y, int N) {
    int idx = x * N + y;
    board->bits[idx / WORD_BITS] |= (1UL << (idx % WORD_BITS));
}
static void unset_queen(struct board* board, int x, int y, int N) {
    int idx = x * N + y;
    board->bits[idx / WORD_BITS] &= ~(1UL << (idx % WORD_BITS));
}

static int solved(struct board* board, int N) {
    int queens = 0;
    int x, y, k;
    for (x = 0; x < N; x++) {
        for (y = 0; y < N; y++) {
            if (is_queen(board, x, y, N)) {
                queens++;
                for (k = 1; k < N; k++) {
                    if (is_queen(board, x + k, y, N)
                        || is_queen(board, x, y + k, N)
                        || is_queen(board, x + k, y + k, N)
                        || is_queen(board, x + k, y - k, N)) {
                        return -1;
                    }
                }
            }
        }
    }
    return queens;
}


static void* backtrack(struct thread_pool* pool, void* _state) {
    int i;
    struct board_state* state = (struct board_state*)_state;
    if (state->N == state->row && solved(&state->board, state->N) == state->N) {
        //print_board(&state->board, state->N);
        return (void*)1;
    }
    else if (state->row == state->N) {
        return (void*)0;
    }
    else if (solved(&state->board, state->N) == -1) {
        return (void*)0;
    }
    if (state->row < max_parallel_depth) {
        struct board_state* boards = _mm_malloc(sizeof(struct board_state) * state->N, mem_lock);
        memset(boards, 0, sizeof(struct board_state) * state->N);
        struct future** futures = _mm_malloc(sizeof(struct future*) * state->N - 1, mem_lock);
        memset(futures, 0, sizeof(struct future*) * (state->N - 1));
        long slns = 0;
        for (i = 0; i < state->N; i++) {
            boards[i].N = state->N;
            boards[i].row = state->row + 1;
            memcpy(&boards[i].board, &state->board, sizeof(struct board));
            set_queen(&boards[i].board, state->row, i, state->N);
            if (i != state->N - 1) {
                futures[i] = thread_pool_submit(pool, backtrack, &boards[i]);
            }
        }
        slns += (long)backtrack(pool, &boards[state->N - 1]);
        for (i = 0; i < state->N - 1; i++) {
            slns += (long)future_get(futures[i]);
            future_free(futures[i]);
        }
        _mm_free(futures, mem_lock);
        _mm_free(boards, mem_lock);
        return (void*)slns;
    }
    else {
        long slns = 0;
        state->row++;
        for (i = 0; i < state->N; i++) {
            set_queen(&state->board, state->row - 1, i, state->N);
            slns += (long)backtrack(pool, state);
            unset_queen(&state->board, state->row - 1, i, state->N);
        }
        state->row--;
        return (void*)slns;
    }
}

static void benchmark(int N, int threads) {
    printf("Solving N = %d\n", N);
    struct board_state state;
    memset(&state.board, 0, sizeof(struct board));
    state.N = N;
    state.row = 0;

    struct thread_pool* pool = thread_pool_new(threads);
    
    struct future* fut = thread_pool_submit(pool, backtrack, &state);
    long slns = (long)future_get(fut);

    future_free(fut);
    thread_pool_shutdown_and_destroy(pool);

    printf("Solutions: %d\n", (int)slns);
    if (slns == valid_solutions[N]) {
        printf("Solution ok.\n");
    }
    else { 
        printf("Solution bad.\n");
    }
}

void
test_main (void) 
{
  mm_init(mymemory,  256 * 1024 * 1024);
  pthread_mutex_init(&mem_lock);


benchmark(N, NUM_THREADS);

  pthread_mutex_destroy(&mem_lock);
}