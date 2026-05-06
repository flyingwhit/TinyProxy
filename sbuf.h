#include <stdio.h>
#include "csapp.h"

typedef struct {
    int *buffer;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

void sbuf_init(sbuf_t *, int);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *, int);
int sbuf_remove(sbuf_t *);



