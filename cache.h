#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>
#include "csapp.h"

#define MAX_OBJECT_SIZE 102400
#define NBLOCK 10
#define NTHREADS 128 
typedef char string[MAXLINE];


typedef struct {
    char buf[MAX_OBJECT_SIZE];
    string index;
    int size;
    int is_valid; // 0:invalid  1:valid
    long last_time;
} cblock;

typedef struct {
    cblock mbuf[NBLOCK];
    long lru_cnt;
    pthread_rwlock_t rwlock;    
} cmaster;


void cache_init(cmaster *); 

void cache_deinit(cmaster *);

void cache_put(cmaster *, const char *, const char *, int);

int cache_get(cmaster *, const char *, char *, int *);

#endif
