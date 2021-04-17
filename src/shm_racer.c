/*
 *  File:   shm_racer.c
 *  Description:    Contains main function that executes various thread/race patterns
 *                  Attempts to find race conditions that exist between threads 
 */
//#include "threadset.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include "shmlib.h"
#include "shm_racer.h"

pthread_mutex_t shm_access_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int running_threads = 0;

void helloWorld(void *args) {
    printf("Hello World\n");
}
void setValue(void *args) {
    int tmp = 0;
    SHM_OP(SHM_READ, cnt_data.cnt_x, tmp);
    for(int i = 0; i < 30; i++) {
        tmp = tmp + 1;
        SHM_OP(SHM_WRITE, cnt_data.cnt_x, tmp);
    }
}
void setValue_safe(void *args) {
    pthread_mutex_lock(&shm_access_mutex);
    setValue(NULL);
    pthread_mutex_unlock(&shm_access_mutex);
}

static inline void Xcrement_thread_count(bool increment) {
    pthread_mutex_lock(&running_mutex);
    running_threads+=(increment)?1:-1;
    pthread_mutex_unlock(&running_mutex);
}

void *pthread_helper(void *args) {
    sPthreadHelper *pth_helper = (sPthreadHelper *) args;
    pth_helper->start_routine(NULL);
    Xcrement_thread_count(false);
    return NULL;
}
static sPthreadHelper pthread_helper_table[NUM_RUN_FUNCS] = {
    [RUN_FUNC_HELLO_WORLD] = {.start_routine = &helloWorld},
    [RUN_FUNC_COUNTER_RACE] = {.start_routine = &setValue},
    [RUN_FUNC_COUNTER_SAFE] = {.start_routine = &setValue_safe}
};
static sThreadSet threadset_table[NUM_THREAD_SETS] = {
    [THREAD_SET_HELLO_WORLD_SINGLE] = {
        .threadInfo[0] = {
            .start_routine = &pthread_helper,
            .arg = &(pthread_helper_table[RUN_FUNC_HELLO_WORLD])
        },
        .timeout = 30
    },
    [THREAD_SET_HELLO_WORLD_DOUBLE] = {
        .threadInfo[0] = {
            .start_routine = &pthread_helper,
            .arg = &(pthread_helper_table[RUN_FUNC_HELLO_WORLD])
        },
        .threadInfo[1] = {
            .start_routine = &pthread_helper,
            .arg = &(pthread_helper_table[RUN_FUNC_HELLO_WORLD])
        },
        .timeout = 30
    },
    [THREAD_SET_COUNTER_RACE_DOUBLE] = {
        .threadInfo[0] = {
            .start_routine = &pthread_helper,
            .arg = &(pthread_helper_table[RUN_FUNC_COUNTER_RACE])
        },
        .threadInfo[1] = {
            .start_routine = &pthread_helper,
            .arg = &(pthread_helper_table[RUN_FUNC_COUNTER_RACE])
        },
        .timeout = 60
    },
    [THREAD_SET_COUNTER_SAFE_DOUBLE] = {
        .threadInfo[0] = {
            .start_routine = &pthread_helper,
            .arg = &(pthread_helper_table[RUN_FUNC_COUNTER_SAFE])
        },
        .threadInfo[1] = {
            .start_routine = &pthread_helper,
            .arg = &(pthread_helper_table[RUN_FUNC_COUNTER_SAFE])
        },
        .timeout = 60
    }
};
static bool cancel_thd_set(sThreadSet *thd_set) {
    for(int thd_idx = 0; thd_idx < MAX_SUPPORTED_THREADS; thd_idx++) {
        sThreadInfo *thd_info = &(thd_set->threadInfo[thd_idx]);
        int err = 0;
        if(0 == thd_info->threadId) {
            continue;
        } else if(0 != (err = pthread_cancel(thd_info->threadId))) {
            printf("Error Cancelling Pthread: err: %d\n",err);
            if(ESRCH == err) {
                Xcrement_thread_count(false);
                continue;
            } else {
                return false;
            }
        } else {
            printf("Cancelled Pthread\n");
            Xcrement_thread_count(false);
        }
    }
    return true;
}
static void init_locks() {
    pthread_mutex_init(&shm_access_mutex, NULL);
    pthread_mutex_init(&running_mutex, NULL);
}
static void spawn_thread_set(sThreadSet *thd_set) {
    clearall_traps();
    clearall_loghashes();
    for(int thd_idx = 0; thd_idx < MAX_SUPPORTED_THREADS; thd_idx++) {
        sThreadInfo *thd_info = &(thd_set->threadInfo[thd_idx]);
        thd_info->threadId = 0;
        if(NULL != thd_info->start_routine) {
            Xcrement_thread_count(true);
            int err = pthread_create(&(thd_info->threadId), NULL, thd_info->start_routine, thd_info->arg);
            if(0 != err) {
                printf("Error Creating Pthread: %d\n", err);
                Xcrement_thread_count(false);
            } else {
                //printf("Successfully created thread\n");
            }
        } else {
            printf("Empty Function\n");
        }
    }
}
typedef enum {
    WAIT_THREAD_SET_OK = 0,
    WAIT_THREAD_SET_TIMEOUT,
    WAIT_THREAD_SET_UNJOINED,
    NUM_WAIT_THREAD_SET_RC
}eWaitThreadSetRC;
static eWaitThreadSetRC wait_thread_set(sThreadSet *thd_set, uint32_t *runtime_counter) {
    *runtime_counter = 0;
    while(0 < running_threads) {
        sleep(1);
        (*runtime_counter)++;
        if(*runtime_counter >= thd_set->timeout) {
            printf("Timeout reached\n");
            if(false == cancel_thd_set(thd_set)) {
                printf("Could not cancel all threads in thread set, exit!\n");
                return WAIT_THREAD_SET_UNJOINED;
            } else {
                return WAIT_THREAD_SET_TIMEOUT;
            }
        }
    }
    return WAIT_THREAD_SET_OK;
}
int main() {
    init_locks();
    for(eThreadSet thd_set_idx = 0; thd_set_idx < NUM_THREAD_SETS; thd_set_idx++) {
        sThreadSet *thd_set = &(threadset_table[thd_set_idx]);
        eWaitThreadSetRC wait_status = WAIT_THREAD_SET_OK;
        uint32_t runtime_counter = 0;
        spawn_thread_set(thd_set);
        if(WAIT_THREAD_SET_OK == (wait_status = wait_thread_set(thd_set, &runtime_counter))) {
            printf("SETCOMPLETE:%d:%d\n",thd_set_idx,runtime_counter);
        } else if(WAIT_THREAD_SET_TIMEOUT == wait_status) {
            printf("SETCOMPLETE_TO:%d:%d\n",thd_set_idx,runtime_counter);
        } else {
            printf("FAILURE_WAIT\n");
            return -1;
        }
    }
}
