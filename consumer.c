#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/wait.h>

// If not compiled with -DNUM_CONSUMER_THREADS=<number of consumer threads> option, defaults to 5
#ifndef NUM_CONSUMER_THREADS
#define NUM_CONSUMER_THREADS 5
#endif
// If not compiled with -DNUM_PRODUCER_THREADS=<number of producer threads> option, defaults to 5
#ifndef NUM_PRODUCER_THREADS
#define NUM_PRODUCER_THREADS 5
#endif

// global variable to control when to stop program
bool running = true;

// global semaphore used to sync producer threads with each other
sem_t producer_mutex;
// global semaphore used to sync consumer threads with each other
sem_t consumer_mutex;

// message struct to be sent through message passing
struct mesg_buffer
{
    long mesg_type;
    char mesg_text[100];
} message;

// signal handler to stop program
void handle_sigint(int sig)
{
    printf("Joining threads\n");
    running = false;
}

// Consumer thread function that receives messages from producer. Consuer waits if there are no messages to consume
void *create_consumer(void *mesg_id)
{
    // Identify consumer by its thread id (spid)
    int thread_id = gettid();
    int *msg_id = (int *)mesg_id;
    printf("Consumer %d\n", thread_id);
    while (running)
    {
        //Critical section of reading from message queue
        sem_wait(&consumer_mutex);
        msgrcv(*msg_id, &message, sizeof(message), 1, 0);
        printf("Consumer %d recevied: %s\n", thread_id, message.mesg_text);
        sem_post(&consumer_mutex);
        sleep(0.01);
    }
    return NULL;
}

void *create_producer(void *mesg_id)
{
    int thread_id = gettid();
    printf("Producer %d\n", thread_id);
    int *msg_id = (int *)mesg_id;

    struct timeval current_time;

    char tm_string[20];
    int i = 0;
    message.mesg_type = 1;
    while (running)
    {
        sem_wait(&producer_mutex);
        gettimeofday(&current_time, NULL);
        sprintf(tm_string, "Message %d Epoch time: %Ld", i, (long)current_time.tv_sec * 1000000 + current_time.tv_usec);
        strcpy(message.mesg_text, tm_string);
        msgsnd(*msg_id, &message, sizeof(message), 0);
        printf("Producer %d sent: %s \n", thread_id, message.mesg_text);
        i++;
        sem_post(&producer_mutex);
    }
    return NULL;
}

int main()
{
    signal(SIGINT, handle_sigint);
    int error;
    int rc;
    int key = ftok("progfile", 65);
    int msgid = msgget(key, 0666 | IPC_CREAT);

    if (NUM_CONSUMER_THREADS < 1 | NUM_PRODUCER_THREADS < 1)
        exit(1);

    if (fork())
    {
        pthread_t consumer_threads[NUM_CONSUMER_THREADS];
        sem_init(&consumer_mutex, 0, 1);
        for (int i = 0; i < NUM_CONSUMER_THREADS; i++)
        {
            error = pthread_create(&consumer_threads[i], NULL, create_consumer, (void *)&msgid);
            if (error != 0)
                printf("\nConsumer thread can't be created :[%s]", strerror(error));
        }

        while (running)
        {
            sleep(1);
        }

        for (int i = 0; i < NUM_CONSUMER_THREADS; i++)
        {
            pthread_join(consumer_threads[i], NULL);
        }
        exit(0);
    }
    else
    {
        pthread_t producer_threads[NUM_PRODUCER_THREADS];
        // Second arg is 0, therefore mutex only applies to threads, not processes
        sem_init(&producer_mutex, 0, 1);
        for (int i = 0; i < NUM_PRODUCER_THREADS; i++)
        {
            error = pthread_create(&producer_threads[i], NULL, create_producer, (void *)&msgid);

            if (error != 0)
                printf("\nProducer thread can't be created :[%s]", strerror(error));
        }

        while (running)
        {
            sleep(1);
        }

        for (int i = 0; i < NUM_PRODUCER_THREADS; i++)
        {
            pthread_join(producer_threads[i], NULL);
        }
    }

    while (wait(NULL) > 0);

    msgctl(msgid, IPC_RMID, NULL);
    sem_destroy(&consumer_mutex);
    sem_destroy(&producer_mutex);

    return 0;
}