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
#include <uthash.h>
#include "shmlib.h"


#define MIN(a,b) ((a < b) ? (a) : (b))
#define MAX_RACE_ERROR_MSG_SIZE 100

struct HashLoggedHazard *logged_hazards = NULL;

struct HashLoggedHazard{
    char msg_key[MAX_RACE_ERROR_MSG_SIZE];
    int logged_count;
    UT_hash_handle hh;
};

struct HashLoggedHazard *find_logged_hazards(char *msg_key) {
    struct HashLoggedHazard *logged_hazard = NULL;
    HASH_FIND_STR(logged_hazards, msg_key, logged_hazard);
    return logged_hazard;  
}
//NOTE: There is a small memory leak here
//When the uthash is destroyed, the hash structure itself is destroyed
//but no the data itself
//TODO: Push the logged_hazard allocation to a stack, then pop and dealloc when done
void add_logged_hazard(char *msg_key_inp) {
    struct HashLoggedHazard *logged_hazard = malloc(sizeof(struct HashLoggedHazard));
    logged_hazard->logged_count = 1;
    strlcpy(logged_hazard->msg_key, msg_key_inp, MAX_RACE_ERROR_MSG_SIZE);
    HASH_ADD_STR(logged_hazards, msg_key, logged_hazard);
}
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
typedef enum {
    RACE_VIOLATION_WAR = 0,
    RACE_VIOLATION_RAW,
    RACE_VIOLATION_WAW,
    NUM_RACE_VIOLATION_TYPES
}eRaceViolationType;
const char *race_violation_str[NUM_RACE_VIOLATION_TYPES] = {
    [RACE_VIOLATION_WAR] = "WAR",
    [RACE_VIOLATION_RAW] = "RAW",
    [RACE_VIOLATION_WAW] = "WAW"
};
static void trap_violation_logger(eRaceViolationType race_violation_type, sThreadTrapInfo *thd_trap_info, char *info) {
    char msg_key[MAX_RACE_ERROR_MSG_SIZE] = {0};
    struct HashLoggedHazard *logged_hazard = NULL;

    snprintf(msg_key, MAX_RACE_ERROR_MSG_SIZE,
             "[%ld]: %s %s [%ld]: %s",
             pthread_self(), info,
             race_violation_str[race_violation_type],
             thd_trap_info->threadId, thd_trap_info->info);
    if(NULL == (logged_hazard = find_logged_hazards(msg_key))) {
        add_logged_hazard(msg_key);
        printf("%s\n",msg_key);
    } else {
        logged_hazard->logged_count++;
    }
}
static void find_trap_violations(eRW readWrite, size_t offset,
                                 size_t size, char *info) {
    for(int thd_idx = 0; thd_idx < MAX_SHM_TRAPS; thd_idx++) {
        sThreadTrapInfo *thd_trap_info = &(shm_trapper.thread_trap_info[thd_idx]);
        if((0 != thd_trap_info->threadId)
        && (pthread_self() != thd_trap_info->threadId)
        && isWithinTrapOffset(thd_trap_info, offset, size)) {
            switch(thd_trap_info->trap_type) {
                case TRAP_TYPE_READ: {
                    if(SHM_READ == readWrite) {
                        //RAR is not a race hazard
                    } else {
                        trap_violation_logger(RACE_VIOLATION_WAR, thd_trap_info, info);
                    }
                    break;
                }
                case TRAP_TYPE_WRITE: {
                    if(SHM_READ == readWrite) {
                        trap_violation_logger(RACE_VIOLATION_RAW, thd_trap_info, info);
                    } else {
                        trap_violation_logger(RACE_VIOLATION_WAW, thd_trap_info, info);
                    }
                    break;
                }
                case TRAP_TYPE_NONE:
                default:;
            }
        }
    }
}
static void find_near_misses(eRW readWrite, size_t offset,
                             size_t size, char *info) {
}
/*
 * @brief: Sets a trap, returns the amount of time the trap will be
 */
static int set_trap(eRW readWrite, size_t offset,
                    size_t size, char *info) {
    //TODO: determine trap delay
    //have we seen this trap before? (same thread, same memory offsets)
    //are there any potential violations left from other threads?
    //are there any potential violations left when considering happens before relationships?
    //are there any near misses that should motivate a delay here?
    //or are those near misses already exposed as faults/happens before?
    for(int thd_idx = 0; thd_idx < MAX_SHM_TRAPS; thd_idx++) {
        sThreadTrapInfo *thd_trap_info = &(shm_trapper.thread_trap_info[thd_idx]);
        if(0 == thd_trap_info->threadId) {
            thd_trap_info->threadId = pthread_self();
            thd_trap_info->trap_type = readWrite + 1;
            thd_trap_info->trap_offsets[TRAP_HIGH] = offset + size - 1;
            thd_trap_info->trap_offsets[TRAP_LOW] = offset;
            thd_trap_info->info = info;
            return DEFAULT_DELAY;
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

void clearall_traps() {
    memset(&shm_trapper, 0, sizeof(sSHMTrapper));
}
void clearall_loghashes() {
    HASH_CLEAR(hh, logged_hazards);
}
eSHMRC shm_op(eRW readWrite, void *buf,
              size_t buf_size, size_t offset,
              size_t size, char *info) {
    if(!info) {
        return SHM_RC_FAIL;
    } else if(0 == buf_size || 0 == size) {
        return SHM_RC_FAIL;
    } else {
        int sleep_time = 0;
        pthread_mutex_lock(&shm_mutex);
        //near miss is from perspective of SHM_OP triggering a trap
        //for near miss do a hash table of shared memory regions that were previously accessed
        //within each entry have file/func/line thread meta-data
        //TODO: log shm access time, thread, file/func/line(for use with near miss calculation)
        find_trap_violations(readWrite, offset, size, info);
        //TODO: find_near_misses(readWrite, offset, size, file, func, line);
        if(SHM_READ == readWrite) {//true is read
            memcpy(buf, (void *) &(shm_master) + offset, MIN(size, buf_size));
        } else if(SHM_WRITE == readWrite) {//false is write
            memcpy((void *) &(shm_master) + offset, buf, MIN(size, buf_size));
        }
        sleep_time = set_trap(readWrite, offset, size, info);
        pthread_mutex_unlock(&shm_mutex);

        if(sleep_time) {
            usleep(sleep_time);
        }

        pthread_mutex_lock(&shm_mutex);
        clear_traps();
        pthread_mutex_unlock(&shm_mutex);
        return SHM_RC_OK;
    }
}
