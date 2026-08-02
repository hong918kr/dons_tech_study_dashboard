#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h> // For sleep

#define BUFFER_SIZE 5
#define NUM_PRODUCERS 2
#define NUM_CONSUMERS 2

int buffer[BUFFER_SIZE];
int in = 0;
int out = 0;

sem_t empty; // Counts empty slots in the buffer
sem_t full;  // Counts filled slots in the buffer
pthread_mutex_t mutex; // Protects access to the buffer

void *producer(void *arg) {
    int producer_id = *(int *)arg;
    int item;

    for (int i = 0; i < 10; i++) { // Produce 10 items
        item = rand() % 100; // Produce a random item

        sem_wait(&empty); // Decrement empty count. Wait if buffer is full.
        pthread_mutex_lock(&mutex); // Acquire lock to protect buffer

        buffer[in] = item;
        printf("Producer %d produced item %d at index %d\n", producer_id, item, in);
        in = (in + 1) % BUFFER_SIZE;

        pthread_mutex_unlock(&mutex); // Release lock
        sem_post(&full); // Increment full count

        sleep(1); // Simulate production time
    }
    return NULL;
}

void *consumer(void *arg) {
    int consumer_id = *(int *)arg;
    int item;

    for (int i = 0; i < 10; i++) { // Consume 10 items
        sem_wait(&full); // Decrement full count. Wait if buffer is empty.
        pthread_mutex_lock(&mutex); // Acquire lock

        item = buffer[out];
        printf("Consumer %d consumed item %d from index %d\n", consumer_id, item, out);
        out = (out + 1) % BUFFER_SIZE;

        pthread_mutex_unlock(&mutex); // Release lock
        sem_post(&empty); // Increment empty count

        sleep(2); // Simulate consumption time
    }
    return NULL;
}

int main() {
    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];
    int producer_ids[NUM_PRODUCERS];
    int consumer_ids[NUM_CONSUMERS];

    // Initialize semaphores and mutex
    sem_init(&empty, 0, BUFFER_SIZE); // Initially, all slots are empty
    sem_init(&full, 0, 0);          // Initially, no slots are full
    pthread_mutex_init(&mutex, NULL);

    // Create producer threads
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_ids[i] = i;
        pthread_create(&producers[i], NULL, producer, &producer_ids[i]);
    }

    // Create consumer threads
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        consumer_ids[i] = i;
        pthread_create(&consumers[i], NULL, consumer, &consumer_ids[i]);
    }

    // Join threads (wait for them to finish)
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }

    // Destroy semaphores and mutex
    sem_destroy(&empty);
    sem_destroy(&full);
    pthread_mutex_destroy(&mutex);

    return 0;
}