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
#include <math.h>
#include "shmlib.h"


#define MIN(a,b) ((a < b) ? (a) : (b))


#define MAX_ACCESS_LOG_ENTRIES 1000
//indicates where the next access will go in the log
static uint32_t shm_access_log_idx = 0;
//log of shm accesses(gets flushed on demand with each SHM_OP)
static sSHMAccessLog shm_access_log[MAX_ACCESS_LOG_ENTRIES] = {0};

/*
 * @brief:  Function logs SHM_OP accesses
 */
static void log_access(eRW readWrite, size_t offset, size_t size, char *info, uint32_t thread_idx) {
    if(shm_access_log_idx < MAX_ACCESS_LOG_ENTRIES) {
        struct timeval timestamp = {0};
        gettimeofday(&timestamp, NULL);
        sSHMAccessLog access_log = {
            .thread_idx = thread_idx,
            .rw = readWrite,
            .shm_offsets[TRAP_HIGH] = offset + size - 1,
            .shm_offsets[TRAP_LOW] = offset,
            .access_time_s = timestamp.tv_sec,
            .access_time_us = timestamp.tv_usec
        };
        strlcpy(access_log.info, info, 200);
        memcpy(&(shm_access_log[shm_access_log_idx]), &access_log, sizeof(sSHMAccessLog));
        shm_access_log_idx++;
    } else {
        printf("Could Not Log Access\n");
    }
}
static void flush_access_log() {
    if(0 == shm_access_log_idx) {
        return;//nothing to flush
    }
    struct timeval timestamp = {0};
    gettimeofday(&timestamp, NULL);//MS_SEC_CONVERSION)
    uint64_t current_time_s = timestamp.tv_sec;
    uint64_t current_time_us = timestamp.tv_usec;

    uint32_t shm_scan_start_idx = MIN(shm_access_log_idx - 1, MAX_ACCESS_LOG_ENTRIES - 1);
    uint32_t num_valid_shm_logs = 0;
    for(uint32_t shm_scan_idx = shm_scan_start_idx; shm_scan_idx >= 0; shm_scan_idx--) {
        uint64_t delta_s = current_time_s - shm_access_log[shm_scan_idx].access_time_s;
        uint64_t delta_us = current_time_us - shm_access_log[shm_scan_idx].access_time_us;
        delta_us = ((delta_s) * MS_SEC_CONVERSION) + delta_us;
        if(NEAR_MISS_THRESHOLD < delta_us) {
            memcpy(shm_access_log, &(shm_access_log[shm_scan_idx + 1]), sizeof(sSHMAccessLog) * num_valid_shm_logs);
            shm_access_log_idx = 0;
            break;
        } else {
            num_valid_shm_logs++;
            if(0 >= shm_scan_idx) {
                break;
            }
        }
    }
}
void reset_access_log() {
    memset(shm_access_log, 0, sizeof(sSHMAccessLog) * MAX_ACCESS_LOG_ENTRIES);
    shm_access_log_idx = 0;
}
static void print_access_log() {
    printf("Printing Access Log\n");
    for(uint32_t shm_scan_idx = 0; shm_scan_idx < shm_access_log_idx; shm_scan_idx++) {
        sSHMAccessLog *access_log = &(shm_access_log[shm_scan_idx]);
        printf("[%lu]: T:[%u] R/W:[%s], [%s]\n",access_log->access_time_s,
                                                access_log->thread_idx,
                                                access_log->rw ? "W":"R",
                                                access_log->info);
    }
}

#define MAX_RACE_ERROR_MSG_SIZE 200

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

struct HashNMHazard *nm_hazards = NULL;

struct HashNMHazard {
    char msg_key[MAX_RACE_ERROR_MSG_SIZE];
    char src[MAX_RACE_ERROR_MSG_SIZE];
    uint32_t thread_idx;
    int count;
    uint64_t avg_delay_us;
    uint32_t decay_func_factor;
    UT_hash_handle hh;
};

struct HashNMHazard *find_nm_hazards(char *msg_key) {
    struct HashNMHazard *nm_hazard = NULL;
    HASH_FIND_STR(nm_hazards, msg_key, nm_hazard);
    return nm_hazard;
};

void add_nm_hazard(char *msg_key_inp, uint64_t delay, char *src, uint32_t thread_idx) {
    struct HashNMHazard *nm_hazard = malloc(sizeof(struct HashNMHazard));
    nm_hazard->count = 1;
    nm_hazard->avg_delay_us = delay;
    nm_hazard->thread_idx = thread_idx;
    strlcpy(nm_hazard->src, src, MAX_RACE_ERROR_MSG_SIZE);
    strlcpy(nm_hazard->msg_key, msg_key_inp, MAX_RACE_ERROR_MSG_SIZE);
    HASH_ADD_STR(nm_hazards, msg_key, nm_hazard);
}

void del_nm_hazard(char *msg_key) {
    struct HashNMHazard *nm_hazard = NULL;
    if(NULL != (nm_hazard = find_nm_hazards(msg_key))) {
        HASH_DEL(nm_hazards, nm_hazard);
        free(nm_hazard);
    }
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
static void trap_violation_logger(eRaceViolationType race_violation_type, sThreadTrapInfo *thd_trap_info, char *info, uint32_t thread_idx) {
    char msg_key[MAX_RACE_ERROR_MSG_SIZE] = {0};
    struct HashLoggedHazard *logged_hazard = NULL;

    snprintf(msg_key, MAX_RACE_ERROR_MSG_SIZE,
             "[%u]: %s %s [%u]: %s",
             thread_idx, info,
             race_violation_str[race_violation_type],
             thd_trap_info->thread_idx, thd_trap_info->info);
    if(NULL == (logged_hazard = find_logged_hazards(msg_key))) {
        add_logged_hazard(msg_key);
        printf("TRAP %s\n",msg_key);
        del_nm_hazard(msg_key);
    } else {
        logged_hazard->logged_count++;
    }
}
static void find_trap_violations(eRW readWrite, size_t offset, size_t size, char *info, uint32_t thread_idx) {
    for(int thd_idx = 0; thd_idx < MAX_SHM_TRAPS; thd_idx++) {
        sThreadTrapInfo *thd_trap_info = &(shm_trapper.thread_trap_info[thd_idx]);
        if((0 != thd_trap_info->thread_idx)
        && (thread_idx != thd_trap_info->thread_idx)
        && isWithinTrapOffset(thd_trap_info, offset, size)) {
            switch(thd_trap_info->trap_type) {
                case TRAP_TYPE_READ: {
                    if(SHM_READ == readWrite) {
                        //RAR is not a race hazard
                    } else {
                        trap_violation_logger(RACE_VIOLATION_WAR, thd_trap_info, info, thread_idx);
                    }
                    break;
                }
                case TRAP_TYPE_WRITE: {
                    if(SHM_READ == readWrite) {
                        trap_violation_logger(RACE_VIOLATION_RAW, thd_trap_info, info, thread_idx);
                    } else {
                        trap_violation_logger(RACE_VIOLATION_WAW, thd_trap_info, info, thread_idx);
                    }
                    break;
                }
                case TRAP_TYPE_NONE:
                default:;
            }
        }
    }
}
static bool isWithinNMOffset(sSHMAccessLog *shm_access_log, size_t offset, size_t size) {
    if((offset > shm_access_log->shm_offsets[TRAP_HIGH])
    || (offset + (size - 1) < shm_access_log->shm_offsets[TRAP_LOW])) {
        return false;
    } else {
        return true;
    }
}
static void nm_violation_logger(eRaceViolationType race_violation_type, sSHMAccessLog *shm_access_log, char *info, uint32_t thread_idx) {
    char msg_key[MAX_RACE_ERROR_MSG_SIZE] = {0};
    struct HashLoggedHazard *logged_hazard = NULL;

    snprintf(msg_key, MAX_RACE_ERROR_MSG_SIZE,
             "[%u]: %s %s [%u]: %s",
             thread_idx, info,
             race_violation_str[race_violation_type],
             shm_access_log->thread_idx, shm_access_log->info);
    if(NULL == (logged_hazard = find_logged_hazards(msg_key))) {
        //printf("NM: %s\n",msg_key);
        struct HashNMHazard *nm_hazard = NULL;
        struct timeval timestamp = {0};
        gettimeofday(&timestamp, NULL);
        uint64_t current_time_s = timestamp.tv_sec;
        uint64_t current_time_us = timestamp.tv_usec;
        uint64_t access_time_s = shm_access_log->access_time_s;
        uint64_t access_time_us = shm_access_log->access_time_us;
        uint64_t delta_s = current_time_s - access_time_s;
        uint64_t delta_us = current_time_us - access_time_us;
        delta_us = ((delta_s) * MS_SEC_CONVERSION) + delta_us;

        if(NULL == (nm_hazard = find_nm_hazards(msg_key))) {
            add_nm_hazard(msg_key, delta_us, shm_access_log->info, shm_access_log->thread_idx);
            nm_hazard = find_nm_hazards(msg_key);
            nm_hazard->decay_func_factor = 0;
        } else {
            nm_hazard->avg_delay_us += ((int64_t)(delta_us - nm_hazard->avg_delay_us))/(nm_hazard->count + 1);
            nm_hazard->count = nm_hazard->count + 1;
            //reset decay function
            nm_hazard->decay_func_factor = 0;
        }
    }
}
static void find_near_misses(eRW readWrite, size_t offset, size_t size, char *info, uint32_t thread_idx) {
    //cycle through shm access log, checking for operations that could be a violation
    //cross reference with trap violation log to see if these issues have already been detected...
    //if they have then they don't register them as near misses
    for(uint32_t shm_scan_idx = 0; shm_scan_idx <= shm_access_log_idx; shm_scan_idx++) {
        //determine if these two logs reference overlapping memory
        if((0 != thread_idx) && (0 != shm_access_log[shm_scan_idx].thread_idx)
        && (thread_idx != shm_access_log[shm_scan_idx].thread_idx)
        && (isWithinNMOffset(&shm_access_log[shm_scan_idx], offset, size))) {
            switch(shm_access_log[shm_scan_idx].rw) {
                case SHM_READ: {
                    if(SHM_READ == readWrite) {
                        //RAR
                    } else {
                        nm_violation_logger(RACE_VIOLATION_WAR, &shm_access_log[shm_scan_idx], info, thread_idx);
                    }
                    break;
                }
                case SHM_WRITE: {
                    if(SHM_READ == readWrite) {
                        nm_violation_logger(RACE_VIOLATION_RAW, &shm_access_log[shm_scan_idx], info, thread_idx);
                    } else {
                        nm_violation_logger(RACE_VIOLATION_WAW, &shm_access_log[shm_scan_idx], info, thread_idx);
                    }
                    break;
                }
                default:;
            }
        }
    }
}

/*
 * @brief: Sets a trap, returns the amount of time the trap will be
 */
static int set_trap(eRW readWrite, size_t offset, size_t size, char *info, uint32_t thread_idx) {
    for(int thd_idx = 0; thd_idx < MAX_SHM_TRAPS; thd_idx++) {
        sThreadTrapInfo *thd_trap_info = &(shm_trapper.thread_trap_info[thd_idx]);
        if(0 == thd_trap_info->thread_idx) {
            struct HashNMHazard *current_nm_hazard, *tmp;
            thd_trap_info->thread_idx = thread_idx;//1 index the thread numbers for reporting
            thd_trap_info->trap_type = readWrite + 1;
            thd_trap_info->trap_offsets[TRAP_HIGH] = offset + size - 1;
            thd_trap_info->trap_offsets[TRAP_LOW] = offset;
            thd_trap_info->info = info;
            uint64_t avg_delay = 0;
            uint32_t nm_count = 0;
            //perform a search of near miss table using the src string as the query
            //average together all avg delays
            //use the lightweight avg formula to do this average, for overflow reasons
            HASH_ITER(hh, nm_hazards, current_nm_hazard, tmp) {
                if(0 == strcmp(thd_trap_info->info, current_nm_hazard->src)
                && (thread_idx == current_nm_hazard->thread_idx)) {
                    //printf("Found NM Hazard[%s], for [%s]\n",current_nm_hazard->src, info);
                    uint64_t current_avg_delay_us = current_nm_hazard->avg_delay_us;
                    current_avg_delay_us = (current_avg_delay_us)/(pow((double)3, current_nm_hazard->decay_func_factor)); 
                    int64_t delay_diff = ((int64_t)(current_avg_delay_us - avg_delay) / (nm_count + 1));
                    avg_delay = (avg_delay + delay_diff);
                    current_nm_hazard->decay_func_factor++;
                    nm_count++;
                }
            }
            //printf("Avg Delay: %lu\n",avg_delay);
            return DEFAULT_DELAY;//(nm_count > 0) ? avg_delay : DEFAULT_DELAY;
        }
    }
    return 0;
}
static void clear_traps(uint32_t thread_idx) {
    for(int thd_idx = 0; thd_idx < MAX_SHM_TRAPS; thd_idx++) {
        sThreadTrapInfo *thd_trap_info = &(shm_trapper.thread_trap_info[thd_idx]);
        if(thread_idx == thd_trap_info->thread_idx) {
            memset(thd_trap_info, 0, sizeof(sThreadTrapInfo));
        }
    }
}

void clearall_traps() {
    memset(&shm_trapper, 0, sizeof(sSHMTrapper));
}
void clearall_loghashes() {
    HASH_CLEAR(hh, logged_hazards);
    HASH_CLEAR(hh, nm_hazards);
}
void reset_nm_decays() {
    if(!nm_hazards) {
        return;
    } else {
        struct HashNMHazard *current_nm_hazard, *tmp;
        HASH_ITER(hh, nm_hazards, current_nm_hazard, tmp) {
            current_nm_hazard->decay_func_factor = 0;
        }
    }
}
//TODO: Have shm_op take a thread context variable
//TODO: pthread_self() is not scalable for analysis across runs
eSHMRC shm_op(eRW readWrite, void *buf,
              size_t buf_size, size_t offset,
              size_t size, char *info, uint32_t thd_idx) {
    if(!info || !buf) {
        return SHM_RC_FAIL;
    } else if(0 == buf_size || 0 == size) {
        return SHM_RC_FAIL;
    } else {
        int sleep_time = 0;
        thd_idx++;//increment the thd_idx by 1 for SHM
        pthread_mutex_lock(&shm_mutex);
        find_trap_violations(readWrite, offset, size, info, thd_idx);
        flush_access_log();
        find_near_misses(readWrite, offset, size, info, thd_idx);
        log_access(readWrite, offset, size, info, thd_idx);
        //print_access_log();
        if(SHM_READ == readWrite) {//true is read
            memcpy(buf, (void *) &(shm_master) + offset, MIN(size, buf_size));
        } else if(SHM_WRITE == readWrite) {//false is write
            memcpy((void *) &(shm_master) + offset, buf, MIN(size, buf_size));
        }
        sleep_time = set_trap(readWrite, offset, size, info, thd_idx);
        pthread_mutex_unlock(&shm_mutex);

        if(sleep_time) {
            //printf("Sleeping %d\n",sleep_time);
            usleep(sleep_time);
        }

        pthread_mutex_lock(&shm_mutex);
        clear_traps(thd_idx);
        pthread_mutex_unlock(&shm_mutex);
        return SHM_RC_OK;
    }
}
