#include "cache.h"
int count = 0;

void cache_init(cmaster * cm) {
    for (int i = 0; i < NBLOCK; i++) {
        cm->mbuf[i].is_valid = 0;
    }
    cm->lru_cnt = 0;
    pthread_rwlock_init(&cm->rwlock, NULL);
    Sem_init(&cm->cnt, 0, 1);
}

void cache_deinit(cmaster *cm) {
    for (int i = 0; i < NBLOCK; i++) {
        cm->mbuf[i].is_valid = 1;
    }
}

void cache_assert(cmaster *cm, char* content, string idx, int size) {
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
    pthread_rwlock_unlock(&cm->rwlock);
}


int cache_match(cmaster *cm, string idx) {
    pthread_rwlock_rdlock(&cm->rwlock);
    P(&cm->cnt);
    count++;
    V(&cm->cnt);
    for (int i = 0; i < NBLOCK; i++) {
           if (!strcmp(idx, cm->mbuf[i].index)) {
                cm->mbuf[i].last_time = ++(cm->lru_cnt);
                pthread_rwlock_unlock(&cm->rwlock);
                return i;
           }
    }
    pthread_rwlock_unlock(&cm->rwlock);
    return -1;
 }