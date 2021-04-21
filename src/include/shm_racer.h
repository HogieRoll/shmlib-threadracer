/*
 *  File:   shm_racer.h
 *  Description:    Contains function protos and defines for shm_racer.c/consumers
 */
#include <pthread.h>
#include "shmlib.h"
#define MAX_SUPPORTED_THREADS 5

typedef enum {
    RUN_FUNC_EMPTY,
    RUN_FUNC_COUNTER_RACE,
    RUN_FUNC_COUNTER_SAFE,
    RUN_FUNC_READ_RACE,
    RUN_FUNC_RMW_RACE,
    RUN_FUNC_RMW_SAFE,
    RUN_FUNC_RMW_FIRST,
    RUN_FUNC_RMW_SECOND,
    RUN_FUNC_RMW_FAKE_SECOND,
    RUN_FUNC_MAILMAN,
    RUN_FUNC_MAILCUSTOMER,
    NUM_RUN_FUNCS
}eRunFuncs;
typedef enum {
    THREAD_SET_EMPTY_SINGLE = 0,
    THREAD_SET_EMPTY_DOUBLE,
    THREAD_SET_COUNTER_RACE_DOUBLE,
    THREAD_SET_COUNTER_SAFE_DOUBLE,
    THREAD_SET_COUNTER_MIXED,
    THREAD_SET_READ_RACE_DOUBLE,
    THREAD_SET_RMW_RACE_DOUBLE,
    THREAD_SET_RMW_SAFE_DOUBLE,
    THREAD_SET_RMW_HB_DOUBLE,
    THREAD_SET_RMW_HB_FAKE,
    THREAD_SET_MAILMAN,
    THREAD_SET_RMW_RACE_MAX,
    NUM_THREAD_SETS
}eThreadSet;
#define MAX_FUNC_STR_SIZE 80
typedef struct {
    void (*start_routine) (void *);
    char func_str[MAX_FUNC_STR_SIZE];
}sPthreadHelper;
typedef struct {
    sPthreadHelper *pth_link;
    uint32_t thd_idx;
}sThreadArgs;
typedef struct {
    pthread_t threadId;
    void *(*start_routine) (void *);
    sThreadArgs arg;
}sThreadInfo;
#define MAX_SET_STR_SIZE 80
typedef struct {
    sThreadInfo threadInfo[MAX_SUPPORTED_THREADS];
    int timeout;//specifies how long to wait for this thread set to finish
    char set_str[MAX_SET_STR_SIZE];
}sThreadSet;



