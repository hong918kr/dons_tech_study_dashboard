```c
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
    struct timeval triggerTime;
    void (*callback)(void*);
    void* callback_arg;
} Alarm;

pthread_mutex_t alarmMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarmCond = PTHREAD_COND_INITIALIZER;
Alarm currentAlarm;
int alarmSet = 0;
int alarmThreadRunning = 1;

struct timeval getCurrentTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv;
}

void* alarmThread(void* arg) {
    while (alarmThreadRunning) {
        pthread_mutex_lock(&alarmMutex);
        if (!alarmSet) {
            pthread_cond_wait(&alarmCond, &alarmMutex);
        }

        struct timeval now = getCurrentTime();
        if (now.tv_sec >= currentAlarm.triggerTime.tv_sec && now.tv_usec >= currentAlarm.triggerTime.tv_usec) {
            alarmSet = 0;
            pthread_mutex_unlock(&alarmMutex);
            currentAlarm.callback(currentAlarm.callback_arg);
        } else {
            pthread_mutex_unlock(&alarmMutex);
            usleep(100000); // Check every 100ms
        }
    }
    return NULL;
}

void setAlarm(int seconds, void (*callback)(void*), void* arg) {
    pthread_mutex_lock(&alarmMutex);
    currentAlarm.triggerTime.tv_sec = time(NULL) + seconds;
    currentAlarm.triggerTime.tv_usec = 0;
    currentAlarm.callback = callback;
    currentAlarm.callback_arg = arg;
    alarmSet = 1;
    pthread_cond_signal(&alarmCond);
    pthread_mutex_unlock(&alarmMutex);
}

void myCallback(void* arg) {
    int alarm_id = *(int*)arg;
    printf("Alarm %d triggered!\n", alarm_id);
}

int main() {
    pthread_t thread;
    pthread_create(&thread, NULL, alarmThread, NULL);

    int alarm_id1 = 1;
    setAlarm(2, myCallback, &alarm_id1);

    sleep(5);

    pthread_mutex_lock(&alarmMutex);
    alarmThreadRunning = 0;
    pthread_cond_signal(&alarmCond);
    pthread_mutex_unlock(&alarmMutex);
    pthread_join(thread, NULL);

    return 0;
}
```

Key simplifications:

*   **Single Alarm:** Instead of a queue, it handles only one alarm at a time. This drastically reduces complexity.
*   **Simplified Thread Logic:** The thread now simply checks if an alarm is set and if its time has arrived. If not, it sleeps for a short period.
*   **No Dynamic Memory Allocation:** No more `malloc` or `free`, further simplifying the code.
*   **Removed Timeval Comparison Function:** Direct comparison of `timeval` members is used for simplicity.
*   **Polling Instead of Timed Wait:** The `usleep` function is used for simplicity instead of the more complex `pthread_cond_timedwait`. This is less efficient but simpler to understand.

This version is significantly shorter and easier to grasp, suitable for a quick understanding of the core concepts. However, it sacrifices the ability to handle multiple concurrent alarms and uses a less efficient polling method. If you need to handle multiple alarms or require precise timing, you should refer to the more complete version.
