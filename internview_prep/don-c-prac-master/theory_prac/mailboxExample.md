

```c
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>

#define MAILBOX_SIZE 10

// Structure to represent a mailbox
typedef struct {
    int buffer[MAILBOX_SIZE];
    int head; // Index for adding items
    int tail; // Index for removing items
    int count; // Number of items in the mailbox
    pthread_mutex_t mutex; // Mutex for protecting shared resources
    sem_t empty; // Semaphore to track empty slots
    sem_t full; // Semaphore to track filled slots
} mailbox_t;

// Function to initialize a mailbox
int mailbox_init(mailbox_t *mailbox) {
    if (pthread_mutex_init(&mailbox->mutex, NULL) != 0) {
        perror("Mutex initialization failed");
        return -1;
    }
    if (sem_init(&mailbox->empty, 0, MAILBOX_SIZE) != 0) {
        perror("Empty semaphore initialization failed");
        pthread_mutex_destroy(&mailbox->mutex);
        return -1;
    }
    if (sem_init(&mailbox->full, 0, 0) != 0) {
        perror("Full semaphore initialization failed");
        pthread_mutex_destroy(&mailbox->mutex);
        sem_destroy(&mailbox->empty);
        return -1;
    }
    mailbox->head = 0;
    mailbox->tail = 0;
    mailbox->count = 0;
    return 0;
}

// Function to send data to the mailbox
int mailbox_send(mailbox_t *mailbox, int data) {
    if (sem_wait(&mailbox->empty) != 0) { // Wait for an empty slot
        perror("Semaphore wait failed");
        return -1;
    }
    if (pthread_mutex_lock(&mailbox->mutex) != 0) { // Lock the mailbox
        perror("Mutex lock failed");
        sem_post(&mailbox->empty); // Release the semaphore if lock fails
        return -1;
    }

    mailbox->buffer[mailbox->head] = data;
    mailbox->head = (mailbox->head + 1) % MAILBOX_SIZE;
    mailbox->count++;

    pthread_mutex_unlock(&mailbox->mutex); // Unlock the mailbox
    sem_post(&mailbox->full); // Signal that a slot has been filled

    return 0;
}

// Function to receive data from the mailbox
int mailbox_receive(mailbox_t *mailbox, int *data) {
    if (sem_wait(&mailbox->full) != 0) { // Wait for a filled slot
        perror("Semaphore wait failed");
        return -1;
    }
    if (pthread_mutex_lock(&mailbox->mutex) != 0) { // Lock the mailbox
        perror("Mutex lock failed");
        sem_post(&mailbox->full); // Release the semaphore if lock fails
        return -1;
    }

    *data = mailbox->buffer[mailbox->tail];
    mailbox->tail = (mailbox->tail + 1) % MAILBOX_SIZE;
    mailbox->count--;

    pthread_mutex_unlock(&mailbox->mutex); // Unlock the mailbox
    sem_post(&mailbox->empty); // Signal that a slot has been emptied

    return 0;
}

// Function to destroy a mailbox
void mailbox_destroy(mailbox_t *mailbox) {
    pthread_mutex_destroy(&mailbox->mutex);
    sem_destroy(&mailbox->empty);
    sem_destroy(&mailbox->full);
}



// Example usage in threads:
void *sender_thread(void *arg) {
    mailbox_t *mailbox = (mailbox_t *)arg;
    for (int i = 0; i < 20; i++) {
        mailbox_send(mailbox, i);
        printf("Sent: %d\n", i);
        sleep(1); // Simulate some work
    }
    pthread_exit(NULL);
}

void *receiver_thread(void *arg) {
    mailbox_t *mailbox = (mailbox_t *)arg;
    int received_data;
    for (int i = 0; i < 20; i++) {
        mailbox_receive(mailbox, &received_data);
        printf("Received: %d\n", received_data);
        sleep(2); // Simulate some work
    }
    pthread_exit(NULL);
}

int main() {
    mailbox_t mailbox;
    if (mailbox_init(&mailbox) != 0) {
        return 1;
    }

    pthread_t sender, receiver;
    pthread_create(&sender, NULL, sender_thread, &mailbox);
    pthread_create(&receiver, NULL, receiver_thread, &mailbox);

    pthread_join(sender, NULL);
    pthread_join(receiver, NULL);

    mailbox_destroy(&mailbox);
    printf("Mailbox example finished.\n");
    return 0;
}
```

**Explanation and Implementation Details:**

1. **Mailbox Structure (`mailbox_t`):**
   - `buffer`: An array to hold the data being sent.
   - `head`: Index to where the next item will be *added*.
   - `tail`: Index to where the next item will be *removed*.
   - `count`: The current number of items in the mailbox.
   - `mutex`: A mutex (mutual exclusion lock) to protect the shared data (buffer, head, tail, count) from race conditions when multiple threads access the mailbox concurrently.
   - `empty`: A semaphore that counts the number of *empty* slots in the buffer. Initialized to `MAILBOX_SIZE`.
   - `full`: A semaphore that counts the number of *filled* slots in the buffer. Initialized to 0.

2. **Initialization (`mailbox_init`):**
   - Initializes the mutex and the two semaphores.  It's critical to initialize the semaphores correctly. `empty` starts at the buffer size because the mailbox is initially empty. `full` starts at 0 because it's initially empty.
   - Sets `head`, `tail`, and `count` to their initial values.

3. **Sending Data (`mailbox_send`):**
   - `sem_wait(&mailbox->empty)`: The sender *waits* on the `empty` semaphore. If there are no empty slots, the sender will block until a slot becomes available. This prevents the sender from overrunning the buffer.
   - `pthread_mutex_lock(&mailbox->mutex)`: The sender acquires the mutex to ensure exclusive access to the shared data.
   - The data is placed in the buffer at the `head` index.
   - `head` is updated (wrapped around using the modulo operator `%` to implement a circular buffer).
   - `count` is incremented.
   - `pthread_mutex_unlock(&mailbox->mutex)`: The mutex is released.
   - `sem_post(&mailbox->full)`: The sender *posts* to the `full` semaphore, signaling that a slot has been filled.  This might unblock a waiting receiver.

4. **Receiving Data (`mailbox_receive`):**
   - `sem_wait(&mailbox->full)`: The receiver *waits* on the `full` semaphore. If the mailbox is empty, the receiver will block until data is available.
   - `pthread_mutex_lock(&mailbox->mutex)`: The receiver acquires the mutex.
   - The data is retrieved from the buffer at the `tail` index.
   - `tail` is updated.
   - `count` is decremented.
   - `pthread_mutex_unlock(&mailbox->mutex)`: The mutex is released.
   - `sem_post(&mailbox->empty)`: The receiver *posts* to the `empty` semaphore, signaling that a slot has become empty. This might unblock a waiting sender.

5. **Destruction (`mailbox_destroy`):**
   - Destroys the mutex and the semaphores when the mailbox is no longer needed.  This is important to prevent resource leaks.

6. **Example Usage:**
   - The `main` function creates a mailbox and two threads: a sender and a receiver.
   - The sender thread sends data to the mailbox.
   - The receiver thread receives data from the mailbox.
   - The `sleep()` calls are just to simulate some work being done by the threads.

**Key Concepts:**

* **Mutual Exclusion (Mutex):** The mutex is essential to prevent race conditions when multiple threads access the shared mailbox data. Only one thread can hold the mutex at a time.
* **Semaphores:** Semaphores are used for synchronization. The `empty` semaphore controls access to empty slots, and the `full` semaphore controls access to filled slots. This prevents buffer overflows and underflows. They also provide a blocking mechanism.
* **Circular Buffer:** The `head` and `tail` indices wrap around the buffer using the modulo operator, creating a circular buffer