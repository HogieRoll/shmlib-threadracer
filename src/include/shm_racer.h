/*
 *  File:   shm_racer.h
 *  Description:    Contains function protos and defines for shm_racer.c/consumers
 */
#include <pthread.h>
#include "shmlib.h"
#define MAX_SUPPORTED_THREADS 2

typedef enum {
    RUN_FUNC_HELLO_WORLD,
    RUN_FUNC_COUNTER_RACE,
    RUN_FUNC_COUNTER_SAFE,
    NUM_RUN_FUNCS
}eRunFuncs;
typedef enum {
    THREAD_SET_HELLO_WORLD_SINGLE = 0,
    THREAD_SET_HELLO_WORLD_DOUBLE,
    THREAD_SET_COUNTER_RACE_DOUBLE,
    THREAD_SET_COUNTER_SAFE_DOUBLE,
    NUM_THREAD_SETS
}eThreadSet;
typedef struct {
    pthread_t threadId;
    void *(*start_routine) (void *);
    void *arg;
}sThreadInfo;
typedef struct {
    sThreadInfo threadInfo[MAX_SUPPORTED_THREADS];
    int timeout;//specifies how long to wait for this thread set to finish
}sThreadSet;
typedef struct {
    void (*start_routine) (void *);
}sPthreadHelper;


