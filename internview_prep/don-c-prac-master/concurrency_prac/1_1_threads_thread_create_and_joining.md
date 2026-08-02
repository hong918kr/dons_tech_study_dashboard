```c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

// --- Question 1: Multiple Threads and Waiting ---

#define NUM_THREADS 5

void *thread_function(void *thread_id) {
    long tid = (long)thread_id;
    printf("Thread %ld started\n", tid);
    // Simulate some work
    for (int i = 0; i < 1000000; ++i) {
        // Do nothing, just waste time
    }
    printf("Thread %ld finished\n", tid);
    pthread_exit(NULL);
}

void multiple_threads_example() {
    pthread_t threads[NUM_THREADS];
    long t;
    for (t = 0; t < NUM_THREADS; t++) {
        int rc = pthread_create(&threads[t], NULL, thread_function, (void *)t);
        if (rc) {
            fprintf(stderr, "Error creating thread %ld, return code %d\n", t, rc);
            exit(EXIT_FAILURE);
        }
    }

    // Wait for all threads to finish
    for (t = 0; t < NUM_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    printf("All threads finished.\n");
}

// --- Question 2: Factorial Thread ---

typedef struct {
    int number;
    long long result;
} factorial_data;

void *factorial_thread(void *arg) {
    factorial_data *data = (factorial_data *)arg;
    data->result = 1;
    for (int i = 1; i <= data->number; ++i) {
        data->result *= i;
    }
    pthread_exit(NULL);
}

long long calculate_factorial_thread(int number) {
    pthread_t thread;
    factorial_data data;
    data.number = number;

    pthread_create(&thread, NULL, factorial_thread, (void *)&data);
    pthread_join(thread, NULL);

    return data.result;
}

// --- Question 3: Passing Arguments ---

// Example of passing multiple arguments using a struct
typedef struct {
    int arg1;
    char *arg2;
    double arg3;
} thread_args;

void *example_thread_with_args(void *arg) {
    thread_args *args = (thread_args *)arg;
    printf("Thread received: arg1=%d, arg2=%s, arg3=%f\n", args->arg1, args->arg2, args->arg3);
    pthread_exit(NULL);
}

void pass_arguments_example(){
    pthread_t thread;
    thread_args args;
    args.arg1 = 42;
    args.arg2 = "Hello from thread";
    args.arg3 = 3.14159;

    pthread_create(&thread, NULL, example_thread_with_args, (void *)&args);
    pthread_join(thread, NULL);

    printf("Argument passing example finished\n");
}

int main() {
    printf("--- Multiple Threads Example ---\n");
    multiple_threads_example();

    printf("\n--- Factorial Thread Example ---\n");
    int factorial_number = 5;
    long long factorial_result = calculate_factorial_thread(factorial_number);
    printf("Factorial of %d is %lld\n", factorial_number, factorial_result);

    printf("\n--- Argument Passing Example ---\n");
    pass_arguments_example();

    return 0;
}
```

**Explanation:**

1.  **Multiple Threads and Waiting:**
    * `pthread_create()`: Creates a new thread.
    * `pthread_join()`: Waits for a thread to finish.
    * The main thread creates `NUM_THREADS` threads and then waits for each of them to complete.
    * The thread function casts the void pointer back to a long.

2.  **Factorial Thread:**
    * A `factorial_data` struct is used to pass the number and retrieve the result.
    * The `factorial_thread` function calculates the factorial and stores it in the result member.
    * The main thread creates a thread, waits for it, and then retrieves the result.

3.  **Passing Arguments:**
    * The most common way to pass multiple arguments to a thread is to use a struct.
    * The struct can contain any number of arguments of different types.
    * The address of the struct is passed to `pthread_create()`, and the thread function casts it back to the struct type.
    * This is the preferred method, as passing single variables can be problematic when dealing with variable scope, and the thread functions lifetime.
    * The example passes an int, char*, and double.
    * Always be sure to cast the void pointer to the correct structure type.
    * Be mindful of variable lifetime. If you pass a pointer to a local variable, and that variable goes out of scope before the thread finishes, you will have a problem.
