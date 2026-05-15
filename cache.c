#include "cache.h"

void cache_init(cmaster * cm) {
    for (int i = 0; i < NBLOCK; i++) {
        cm->mbuf[i].is_valid = 0;
        cm->mbuf[i].size = 0;
        cm->mbuf[i].last_time = 0;
        cm->mbuf[i].index[0] = '\0';
    }
    cm->lru_cnt = 0;
    pthread_rwlock_init(&cm->rwlock, NULL);
}

void cache_deinit(cmaster *cm) {
    pthread_rwlock_destroy(&cm->rwlock);
}

void cache_put(cmaster *cm, const char *content, const char *idx, int size) {
    if (size > MAX_OBJECT_SIZE) {
        return;
    }

    pthread_rwlock_wrlock(&cm->rwlock);
    int target = -1;
    long MIN_TIME = 0x7FFFFFFFFFFFFFFF;
    for (int i = 0; i < NBLOCK; i++) {
        if (cm->mbuf[i].is_valid == 0) {
            target = i;
            break;
        }
        if (cm->mbuf[i].last_time < MIN_TIME) {
            MIN_TIME = cm->mbuf[i].last_time;
            target = i;
        }
    }
    cblock *b = &cm->mbuf[target];
    b->is_valid = 1;
    b->last_time = ++(cm->lru_cnt);
    b->size = size;
    memcpy(b->buf, content, size);
    snprintf(b->index, MAXLINE, "%s", idx);
    pthread_rwlock_unlock(&cm->rwlock);
}


int cache_get(cmaster *cm, const char *idx, char *content, int *size) {
    /*
     * We take the write lock because a cache hit updates the LRU timestamp.
     * The object is copied out while the lock is held, so callers never read
     * a block that another thread may evict at the same time.
     */
    pthread_rwlock_wrlock(&cm->rwlock);
    for (int i = 0; i < NBLOCK; i++) {
        if (cm->mbuf[i].is_valid && !strcmp(idx, cm->mbuf[i].index)) {
            cm->mbuf[i].last_time = ++(cm->lru_cnt);
            *size = cm->mbuf[i].size;
            memcpy(content, cm->mbuf[i].buf, cm->mbuf[i].size);
            pthread_rwlock_unlock(&cm->rwlock);
            return 1;
        }
    }
    pthread_rwlock_unlock(&cm->rwlock);
    return 0;
 }
