/*
 *  File:   shmlib.c
 *  Description:    Contains shmlib accessor/mutator functions
 *                  This shm lib is special in that it can increase
 *                  probabilities of race conditions
 */
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "shmlib.h"


#define MIN(a,b) ((a < b) ? (a) : (b))

volatile sSHM shm_master = {0};
static sSHMTrapper shm_trapper = {0};

//static uint8_t shm_usage_bitmap[sizeof(sSHM)] = {0};

pthread_mutex_t shm_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool isWithinTrapOffset(sThreadTrapInfo *thd_trap_info, size_t offset, size_t size) {
    if((offset > thd_trap_info->trap_offsets[TRAP_HIGH])
    || (offset + (size - 1) < thd_trap_info->trap_offsets[TRAP_LOW])) {
        return false;
    } else {
        return true;
    }
}
static void find_trap_violations(eRW readWrite, size_t offset,
                                 size_t size, const char *file,
                                 const char *func, int line) {
    for(int thd_idx = 0; thd_idx < MAX_SHM_TRAPS; thd_idx++) {
        sThreadTrapInfo *thd_trap_info = &(shm_trapper.thread_trap_info[thd_idx]);
        if((0 != thd_trap_info->threadId)
        && (pthread_self() != thd_trap_info->threadId)
        && isWithinTrapOffset(thd_trap_info, offset, size)) {
            switch(thd_trap_info->trap_type) {
                case TRAP_TYPE_READ: {
                    if(SHM_READ == readWrite) {
                        //printf("READ AFTER READ IS FINE!\n");
                    } else {
                        printf("[%ld]: %s:%s:%d w.a.r. [%ld]: %s:%s:%d\n",
                               pthread_self(), file, func, line,
                               thd_trap_info->threadId,
                               thd_trap_info->file,
                               thd_trap_info->func,
                               thd_trap_info->line);
                    }
                    break;
                }
                case TRAP_TYPE_WRITE: {
                    if(SHM_READ == readWrite) {
                        printf("[%ld]: %s:%s:%d r.a.w. [%ld]: %s:%s:%d\n",
                               pthread_self(), file, func, line,
                               thd_trap_info->threadId,
                               thd_trap_info->file,
                               thd_trap_info->func,
                               thd_trap_info->line);
                    } else {
                        printf("[%ld]: %s:%s:%d w.a.w. [%ld]: %s:%s:%d\n",
                               pthread_self(), file, func, line,
                               thd_trap_info->threadId,
                               thd_trap_info->file,
                               thd_trap_info->func,
                               thd_trap_info->line);
                    }
                    break;
                }
                case TRAP_TYPE_NONE:
                default:;
            }
        }
    }
}
/*
 * @brief: Sets a trap, returns the amount of time the trap will be
 */
static int set_trap(eRW readWrite, size_t offset,
                    size_t size, const char *file,
                    const char *func, int line) {
    for(int thd_idx = 0; thd_idx < MAX_SHM_TRAPS; thd_idx++) {
        sThreadTrapInfo *thd_trap_info = &(shm_trapper.thread_trap_info[thd_idx]);
        if(0 == thd_trap_info->threadId) {
            thd_trap_info->threadId = pthread_self();
            thd_trap_info->trap_type = readWrite + 1;
            thd_trap_info->trap_offsets[TRAP_HIGH] = offset + size - 1;
            thd_trap_info->trap_offsets[TRAP_LOW] = offset;
            thd_trap_info->file = file;
            thd_trap_info->func = func;
            thd_trap_info->line = line;
            return 1;
        }
    }
    return 0;
}
static void clear_traps() {
    for(int thd_idx = 0; thd_idx < MAX_SHM_TRAPS; thd_idx++) {
        sThreadTrapInfo *thd_trap_info = &(shm_trapper.thread_trap_info[thd_idx]);
        if(pthread_self() == thd_trap_info->threadId) {
            memset(thd_trap_info, 0, sizeof(sThreadTrapInfo));
        }
    }
}
eSHMRC shm_op(eRW readWrite, void *buf,
              size_t buf_size, size_t offset,
              size_t size, const char *file,
              const char *func, int line) {
    if(!buf || !file || !func) {
        return SHM_RC_FAIL;
    } else if(0 == buf_size || 0 == size) {
        return SHM_RC_FAIL;
    } else {
        int sleep_time = 0;
        pthread_mutex_lock(&shm_mutex);
        //printf("[%ld]: %s:%s:%d\n", pthread_self(), file, func, line);
        find_trap_violations(readWrite, offset, size, file, func, line);
        if(SHM_READ == readWrite) {//true is read
            memcpy(buf, (void *) &(shm_master) + offset, MIN(size, buf_size));
        } else if(SHM_WRITE == readWrite) {//false is write
            memcpy((void *) &(shm_master) + offset, buf, MIN(size, buf_size));
        }
        sleep_time = set_trap(readWrite, offset, size, file, func, line);
        pthread_mutex_unlock(&shm_mutex);

        sleep(sleep_time);
        //printf("[%ld]: After Sleep :%d\n",pthread_self(), sleep_time);

        pthread_mutex_lock(&shm_mutex);
        clear_traps();
        pthread_mutex_unlock(&shm_mutex);
        return SHM_RC_OK;
    }
}
