```c
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
```

**Explanation:**

1.  **Problem:** The Producer-Consumer problem is a classic concurrency problem where multiple producers generate data and place it into a shared buffer, while multiple consumers retrieve and process data from the same buffer. The challenge is to ensure that producers don't try to add data to a full buffer, and consumers don't try to retrieve data from an empty buffer, and that access to the buffer is synchronized.

2.  **Solution using Semaphores and Mutex:**

    *   **Buffer:** `buffer[BUFFER_SIZE]` is the shared buffer. `in` and `out` track the next available index for producers and consumers, respectively.

    *   **Semaphores:**
        *   `sem_t empty`: Counts the number of empty slots in the buffer. Initialized to `BUFFER_SIZE` because initially, the buffer is empty. `sem_wait(&empty)` decrements the count. If the count is 0 (buffer is full), the producer blocks until a slot becomes available. `sem_post(&empty)` increments the count when a consumer consumes an item, making a slot available.
        *   `sem_t full`: Counts the number of filled slots in the buffer. Initialized to 0 because initially, the buffer is empty. `sem_wait(&full)` decrements the count. If the count is 0 (buffer is empty), the consumer blocks until an item is produced. `sem_post(&full)` increments the count when a producer produces an item.

    *   **Mutex:** `pthread_mutex_t mutex`: A mutex (mutual exclusion lock) is used to protect the critical section where the buffer is accessed. This prevents race conditions where multiple producers or consumers try to modify the buffer simultaneously, leading to data corruption. `pthread_mutex_lock(&mutex)` acquires the lock, and `pthread_mutex_unlock(&mutex)` releases it.

3.  **Producer Thread:**

    *   Produces an item.
    *   `sem_wait(&empty)`: Waits if the buffer is full.
    *   `pthread_mutex_lock(&mutex)`: Acquires the lock to access the buffer.
    *   Adds the item to the buffer.
    *   `pthread_mutex_unlock(&mutex)`: Releases the lock.
    *   `sem_post(&full)`: Signals that a new item has been added.
    *   `sleep(1)`: Simulates production time.

4.  **Consumer Thread:**

    *   `sem_wait(&full)`: Waits if the buffer is empty.
    *   `pthread_mutex_lock(&mutex)`: Acquires the lock to access the buffer.
    *   Retrieves an item from the buffer.
    *   `pthread_mutex_unlock(&mutex)`: Releases the lock.
    *   `sem_post(&empty)`: Signals that an item has been consumed.
    *   `sleep(2)`: Simulates consumption time.

5.  **`main()` Function:**

    *   Initializes semaphores and the mutex.
    *   Creates producer and consumer threads.
    *   Waits for threads to finish using `pthread_join()`.
    *   Destroys semaphores and the mutex.

**Key Improvements over simpler examples:**

*   **Multiple Producers and Consumers:** This example demonstrates the more realistic scenario of multiple producers and consumers working concurrently.
*   **Clearer Synchronization:** The use of both semaphores and a mutex provides robust synchronization, preventing both buffer overflow/underflow and race conditions.
*   **Simulated Work:** The `sleep()` calls simulate the time taken for production and consumption, making the example more realistic.
*   **Thread IDs:** The use of thread IDs makes the output easier to understand and debug.

This improved example provides a more complete and robust solution to the Producer-Consumer problem in C using pthreads, semaphores, and mutexes. Remember to compile with `-pthread` (e.g., `gcc -o producer_consumer producer_consumer.c -pthread`).
