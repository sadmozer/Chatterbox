#include <signal.h>
#include <pthread.h>
#ifndef HANDLER_H
#define HANDLER_H
void * handler_worker(void * arg);
void handler(int signum);
volatile sig_atomic_t eflag;
pthread_cond_t cond_taskq;
#endif