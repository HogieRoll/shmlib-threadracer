/*
 *  File:   shmlib.h
 *  Description:    Contains #defines and prototypes for shmlib.c
 */
#ifndef SHMLIB_H
#define SHMLIB_H

#define MAX_SHM_TRAPS 1

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
    const char *file;
    const char *func;
    int line;
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
              size_t size, const char *file,
              const char *func, int line);
#define SHM_OP(rw, member, buf) \
    shm_op(rw, &buf, sizeof(buf), \
    offsetof(sSHM, member), sizeof(((sSHM *)0)->member), \
    __FILE__, __func__, __LINE__)

#endif
