/*
 *  File:   shmlib.h
 *  Description:    Contains #defines and prototypes for shmlib.c
 */
#ifndef SHMLIB_H
#define SHMLIB_H

#include <bsd/string.h>

#define MAX_SHM_TRAPS 2

#define NEAR_MISS_THRESHOLD 5

#include <stdbool.h>
#include <pthread.h>

typedef enum {
    SHM_READ = 0,
    SHM_WRITE
}eRW;
typedef enum {
    TRAP_TYPE_NONE = 0,
    TRAP_TYPE_READ,
    TRAP_TYPE_WRITE,
    NUM_TRAP_TYPES
}eTrapType;
typedef enum {
    TRAP_LOW = 0,
    TRAP_HIGH,
    NUM_TRAP_RANGES
}eTrapRange;
typedef struct {
    pthread_t threadId;
    size_t trap_offsets[NUM_TRAP_RANGES];
    eTrapType trap_type;
    char *info;
}sThreadTrapInfo;
typedef struct {
    sThreadTrapInfo thread_trap_info[MAX_SHM_TRAPS];
}sSHMTrapper;
typedef enum {
    SHM_RC_OK = 0,
    SHM_RC_FAIL,
    NUM_SHM_RC
}eSHMRC;
#define CNT_ARR_SIZE 10
typedef struct {
    int cnt_x;
    int cnt_arr[CNT_ARR_SIZE];
}sCounters;
typedef struct {
    sCounters cnt_data;
}sSHM;
eSHMRC shm_op(eRW readWrite, void *buf,
              size_t buf_size, size_t offset,
              size_t size, char *info);

#define INFO_STR_MAX_SIZE 75
#define MAX_LINE_NUM_DIGITS 10

static char *get_info_str(char *info_str, const char* file, const char* func, int line) {
    char line_str[MAX_LINE_NUM_DIGITS] = {0};
    strlcat(info_str, file, INFO_STR_MAX_SIZE);
    strlcat(info_str, ":", INFO_STR_MAX_SIZE);
    strlcat(info_str, func, INFO_STR_MAX_SIZE);
    strlcat(info_str, ":", INFO_STR_MAX_SIZE);
    snprintf(line_str, MAX_LINE_NUM_DIGITS, "%d", line);
    strlcat(info_str, line_str, INFO_STR_MAX_SIZE);
    return info_str;
}

#define SHM_OP(rw, member, buf) { \
    char info_str[INFO_STR_MAX_SIZE] = {0}; \
    shm_op(rw, &buf, sizeof(buf), \
    offsetof(sSHM, member), sizeof(((sSHM *)0)->member), \
    get_info_str(info_str, __FILE__, __func__, __LINE__)); \
}

void clearall_traps();
void clearall_loghashes();

#endif
