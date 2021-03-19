/*
 *  File:   shm_racer.h
 *  Description:    Contains function protos and defines for shm_racer.c/consumers
 */
#include <pthread.h>
#define MAX_SUPPORTED_THREADS 2

typedef enum {
    RUN_FUNC_HELLO_WORLD,
    RUN_FUNC_COUNTER_RACE,
    NUM_RUN_FUNCS
}eRunFuncs;
typedef enum {
    THREAD_SET_HELLO_WORLD_SINGLE,
    THREAD_SET_HELLO_WORLD_DOUBLE,
    THREAD_SET_COUNTER_RACE_DOUBLE,
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


