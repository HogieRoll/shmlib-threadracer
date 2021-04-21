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
#include <stdlib.h>
#include <errno.h>
#include <semaphore.h>
#include "shmlib.h"
#include "shm_racer.h"

pthread_mutex_t shm_access_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t x_to_y_mutex;

volatile int running_threads = 0;

void empty(void *args) {
    printf("Here\n");
}
void readValue(void *args) {
    int tmp = 0;
    SHM_OP(SHM_READ, cnt_data.cnt_x, tmp);
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
    printf("Acquired Mutex\n");
    setValue(args);
    pthread_mutex_unlock(&shm_access_mutex);
}

void readModifyWrite(void *args) {
    uint8_t tmp = 0;
    SHM_OP(SHM_READ, a, tmp);
    tmp |= 0x20;
    SHM_OP(SHM_WRITE, a, tmp);
}

void readModifyWrite_first(void *args) {
    readModifyWrite(args);
    sem_post(&x_to_y_mutex);
}
void readModifyWrite_second(void *args) {
    sem_wait(&x_to_y_mutex);
    readModifyWrite(args);
}
void readModifyWrite_foolhb(void *args) {
    sleep(2);
    readModifyWrite(args);
}

void readModifyWrite_safe(void *args) {
    pthread_mutex_lock(&shm_access_mutex);
    readModifyWrite(args);
    pthread_mutex_unlock(&shm_access_mutex);
}

void mailman(void *args) {
	for(uint32_t time_sec = 0; time_sec < 100; time_sec++) {
	    if(0 == time_sec % 33) {
	        //put mail into a mailbox
	        uint8_t num_p = 0;
	        SHM_OP(SHM_READ, num_parcels, num_p);
	        num_p++;
	        SHM_OP(SHM_WRITE, num_parcels, num_p);
	    }
	    //sleep(1);
	}
}
void mail_customer(void *args) {
	for(uint32_t time_sec = 0; time_sec < 100; time_sec++) {
	    if(25 == time_sec || 75 == time_sec) {
	        uint8_t num_p = 0;
	        SHM_OP(SHM_READ, num_parcels, num_p);
	        num_p = 0;//they pick up all mail from their mailbox
	        SHM_OP(SHM_WRITE, num_parcels, num_p);
	    }
	    //sleep(1);
	}
}

static inline void Xcrement_thread_count(bool increment) {
    pthread_mutex_lock(&running_mutex);
    running_threads+=(increment)?1:-1;
    pthread_mutex_unlock(&running_mutex);
}

void *pthread_helper(void *args) {
    printf("In Pthread_helper\n");
    sThreadArgs *th_args = (sThreadArgs *) args; 
    sPthreadHelper *pth_helper = (sPthreadHelper *) th_args->pth_link;
    pth_helper->start_routine((void *)&(th_args->thd_idx));
    Xcrement_thread_count(false);
    return NULL;
}
#define PTHREAD_HELPER_BUILDER(func_name, start_func)\
    [func_name] = {.start_routine=&start_func, .func_str=#func_name}

static sPthreadHelper pthread_helper_table[NUM_RUN_FUNCS] = {
    PTHREAD_HELPER_BUILDER(RUN_FUNC_EMPTY, empty),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_COUNTER_RACE, setValue),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_COUNTER_SAFE, setValue_safe),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_READ_RACE, readValue),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_RMW_RACE, readModifyWrite),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_RMW_SAFE, readModifyWrite_safe),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_RMW_FIRST, readModifyWrite_first),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_RMW_SECOND, readModifyWrite_second),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_RMW_FAKE_SECOND, readModifyWrite_foolhb),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_MAILMAN, mailman),
    PTHREAD_HELPER_BUILDER(RUN_FUNC_MAILCUSTOMER, mail_customer)
};

#define THREAD_FUNC_BUILDER(thread_idx, func_type)\
    .threadInfo[thread_idx] = { \
        .start_routine = &pthread_helper,\
        .arg = { \
            .pth_link = &(pthread_helper_table[func_type]),\
            .thd_idx = thread_idx\
        }\
    }
#define THREAD_SET_BUILDER(thread_set_idx, time_out)\
    [thread_set_idx] = {\
        .timeout = time_out,\
        .set_str = #thread_set_idx,

static sThreadSet threadset_table[NUM_THREAD_SETS] = {
    THREAD_SET_BUILDER(THREAD_SET_EMPTY_SINGLE, 30)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_EMPTY),
    },
    THREAD_SET_BUILDER(THREAD_SET_EMPTY_DOUBLE, 30)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_EMPTY),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_EMPTY)
    },
    /*THREAD_SET_BUILDER(THREAD_SET_COUNTER_RACE_DOUBLE, 200)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_COUNTER_RACE),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_COUNTER_RACE)
    },
    THREAD_SET_BUILDER(THREAD_SET_COUNTER_SAFE_DOUBLE, 500)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_COUNTER_SAFE),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_COUNTER_SAFE)
    },
    THREAD_SET_BUILDER(THREAD_SET_COUNTER_MIXED, 500)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_COUNTER_SAFE),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_COUNTER_RACE)
    },
    THREAD_SET_BUILDER(THREAD_SET_READ_RACE_DOUBLE, 60)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_READ_RACE),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_READ_RACE)
    },*/
    THREAD_SET_BUILDER(THREAD_SET_RMW_RACE_DOUBLE, 20)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_RMW_RACE),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_RMW_RACE)
    },
    THREAD_SET_BUILDER(THREAD_SET_RMW_SAFE_DOUBLE, 20)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_RMW_SAFE),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_RMW_SAFE)
    },
    THREAD_SET_BUILDER(THREAD_SET_RMW_HB_DOUBLE, 20)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_RMW_FIRST),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_RMW_SECOND)
    },
    THREAD_SET_BUILDER(THREAD_SET_RMW_HB_FAKE, 20)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_RMW_FIRST),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_RMW_FAKE_SECOND)
    }/*,
    THREAD_SET_BUILDER(THREAD_SET_MAILMAN, 100)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_MAILMAN),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_MAILCUSTOMER)
    },
    THREAD_SET_BUILDER(THREAD_SET_RMW_RACE_MAX, 100)
        THREAD_FUNC_BUILDER(0, RUN_FUNC_RMW_RACE),
        THREAD_FUNC_BUILDER(1, RUN_FUNC_RMW_RACE),
        THREAD_FUNC_BUILDER(2, RUN_FUNC_RMW_RACE),
        THREAD_FUNC_BUILDER(3, RUN_FUNC_RMW_RACE),
        THREAD_FUNC_BUILDER(4, RUN_FUNC_RMW_RACE)
    }*/
};
const char * get_thread_set_str(eThreadSet thd_set_idx) {
    return threadset_table[thd_set_idx].set_str;
}
static bool cancel_thd_set(sThreadSet *thd_set) {
    for(int thd_idx = 0; thd_idx < MAX_SUPPORTED_THREADS; thd_idx++) {
        sThreadInfo *thd_info = &(thd_set->threadInfo[thd_idx]);
        int err = 0;
        if(0 == thd_info->threadId) {
            continue;
        } else if(0 != (err = pthread_cancel(thd_info->threadId))) {
            //printf("Error Cancelling Pthread: err: %d\n",err);
            if(ESRCH == err) {
                Xcrement_thread_count(false);
                continue;
            } else {
                return false;
            }
        } else {
            //printf("Cancelled Pthread\n");
            Xcrement_thread_count(false);
        }
    }
    return true;
}
static void init_locks() {
    pthread_mutex_init(&shm_access_mutex, NULL);
    pthread_mutex_init(&running_mutex, NULL);
    sem_init(&x_to_y_mutex, 0 , 0);
}
static void spawn_thread_set(sThreadSet *thd_set) {
    clearall_traps();
    clearall_loghashes();
    reset_access_log();
    for(int thd_idx = 0; thd_idx < MAX_SUPPORTED_THREADS; thd_idx++) {
        sThreadInfo *thd_info = &(thd_set->threadInfo[thd_idx]);
        thd_info->threadId = 0;
        if(NULL != thd_info->start_routine) {
            Xcrement_thread_count(true);
            int err = pthread_create(&(thd_info->threadId), NULL, thd_info->start_routine, (void *) &(thd_info->arg));
            if(0 != err) {
                printf("Error Creating Pthread: %d\n", err);
                Xcrement_thread_count(false);
            }
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
            printf("SETCOMPLETE:%s:%d\n",get_thread_set_str(thd_set_idx),runtime_counter);
        } else if(WAIT_THREAD_SET_TIMEOUT == wait_status) {
            printf("SETCOMPLETE_TO:%s:%d\n",get_thread_set_str(thd_set_idx),runtime_counter);
        } else {
            printf("FAILURE_WAIT\n");
            return -1;
        }
    }
}
