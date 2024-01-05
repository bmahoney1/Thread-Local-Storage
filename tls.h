#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>



int tls_create(unsigned int size);

int tls_write(unsigned int offset, unsigned int length, char *buffer);

int tls_read(unsigned int offset, unsigned int length, char *buffer);

int tls_destroy();

int tls_clone(pthread_t tid);
