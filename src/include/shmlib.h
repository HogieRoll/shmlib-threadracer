/*
 *  File:   shmlib.h
 *  Description:    Contains #defines and prototypes for shmlib.c
 */
#ifndef SHMLIB_H
#define SHMLIB_H

#include <bsd/string.h>
#include <sys/time.h>

#define MAX_SHM_TRAPS 5

#define MS_SEC_CONVERSION 1000000
#define NEAR_MISS_THRESHOLD 5 * MS_SEC_CONVERSION//useconds
#define DEFAULT_DELAY 0//useconds

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
    uint32_t thread_idx;
    size_t trap_offsets[NUM_TRAP_RANGES];
    eTrapType trap_type;
    char *info;
}sThreadTrapInfo;

typedef struct {
    uint32_t thread_idx;
    size_t shm_offsets[NUM_TRAP_RANGES];
    eRW rw;
    char info[200];
    uint64_t access_time_s;//time in seconds
    uint64_t access_time_us;//time in useconds
}sSHMAccessLog;

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
    uint8_t a;
    uint8_t num_parcels;
}sSHM;
eSHMRC shm_op(eRW readWrite, void *buf,
              size_t buf_size, size_t offset,
              size_t size, char *info, uint32_t thd_idx);

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
    get_info_str(info_str, __FILE__, __func__, __LINE__), \
    *((uint32_t *)args)); \
}

void clearall_traps();
void clearall_loghashes();
void reset_access_log();

#endif
