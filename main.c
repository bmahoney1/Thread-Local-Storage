#include "tls.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

void* thread_function(void* arg) {
    // Test tls_create
    int tid = *(int*)arg;
    int size = 1024;
    if (tls_create(size) == -1) {
        fprintf(stderr, "Thread %d: Failed to create TLS\n", tid);
        return NULL;
    }

    // Test tls_write
    char buffer[] = "Hello, Thread!";
    if (tls_write(0, strlen(buffer) + 1, buffer) == -1) {
        fprintf(stderr, "Thread %d: Failed to write to TLS\n", tid);
        return NULL;
    }

    // Test tls_read
    char read_buffer[1024];
    if (tls_read(0, sizeof(read_buffer), read_buffer) == -1) {
        fprintf(stderr, "Thread %d: Failed to read from TLS\n", tid);
        return NULL;
    }

    printf("Thread %d: Read from TLS: %s\n", tid, read_buffer);

    // Test tls_clone
    pthread_t other_tid = (tid + 1) % 2;  // Assuming there are only two threads for simplicity
    if (tls_clone(other_tid) == -1) {
        fprintf(stderr, "Thread %d: Failed to clone TLS from Thread %d\n", tid, other_tid);
        return NULL;
    }

    // Test tls_destroy
    if (tls_destroy() == -1) {
        fprintf(stderr, "Thread %d: Failed to destroy TLS\n", tid);
        return NULL;
    }

    return NULL;
}

int main() {
    pthread_t threads[2];
    int thread_ids[2] = {0, 1};
	int i;
    // Create two threads
    for ( i = 0; i < 2; ++i) {
        if (pthread_create(&threads[i], NULL, thread_function, &thread_ids[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            return 1;
        }
    }

    // Join threads
    for ( i = 0; i < 2; ++i) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Failed to join thread %d\n", i);
            return 1;
        }
    }

    return 0;
}

