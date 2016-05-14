#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h> 
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>


void sfwrite(pthread_mutex_t *lock, FILE* stream, char *fmt, ...);