/*
 *  File:   shm_racer.c
 *  Description:    Contains main function that executes various thread/race patterns
 *                  Attempts to find race conditions that exist between threads 
 */
//#include "threadset.h"
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include "shm_racer.h"

static int value = 32;

void helloWorld(void *args) {
    printf("Hello World\n");
}
void setValue(void *args) {
    int tmp = value;
    value = tmp + 1;    
}
volatile int running_threads = 0;
pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    [RUN_FUNC_COUNTER_RACE] = {.start_routine = &setValue}
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
        .timeout = 30
    }
};
static bool cancel_thd_set(sThreadSet *thd_set) {
    for(int thd_idx = 0; thd_idx < MAX_SUPPORTED_THREADS; thd_idx++) {
        sThreadInfo *thd_info = &(thd_set->threadInfo[thd_idx]);
        int err = 0;
        if(0 == thd_info->threadId) {
            continue;
        } else if(0 != pthread_cancel(thd_info->threadId)) {
            printf("Error Cancelling Pthread: %d\n",err);
            return false;
        } else {
            printf("Cancelled Pthread\n");
            Xcrement_thread_count(false);
        }
    }
    return true;
}
int main() {
    for(eThreadSet thd_set_idx = 0; thd_set_idx < NUM_THREAD_SETS; thd_set_idx++) {
        sThreadSet *thd_set = &(threadset_table[thd_set_idx]);
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
                    printf("Successfully created thread\n");
                }
            } else {
                printf("Empty Function\n");
            }
        }
        int sleep_counter = 0;
        while(0 < running_threads) {
            sleep(1);
            sleep_counter++;
            if(sleep_counter >= thd_set->timeout) {
                printf("Timeout reached\n");
                if(false == cancel_thd_set(thd_set)) {
                    printf("Could not cancel all threads in thread set, exit!\n");
                    return -1;
                } else {
                    break;
                }
            }
        }
    }
}
