#include "rwlock.h"
#include <stdlib.h>
#include <stdint.h>
#include <semaphore.h>
#include <stdio.h>
#include <pthread.h>

/*
If there is a situation where, multiple threads call reader_lock and/ or writer_lock at the same time then we will need some priority conditions
An N-Way rw_lock should allow "n" number of readers to proceed between each writer. This should happen if there are an infinite number of readers and writers are attempting to acquire the lock
There the priority goes to READERS, thene the reading should proceed first while there is a conflict.

There can be many readers, and there MUST BE ONLY ONE writer.

Busy-Waiting is now allowed.


*/

struct rwlock {

    PRIORITY priority;
    int reader_nblock_activated;
    int write_block_activated;
    uint32_t n_readers; // set by parameter for N-Ways.
    uint32_t active_readers; // How many active readers do we have right now
    uint32_t
        active_writers; // How many writers do we have right now (THERE HAS TO BE EXACTLY 1 AT ALL TIMES!)
    uint32_t pending_readers; // How many readers are pending or being blocked right now
    uint32_t pending_writers; // How many writer are pending right now
    int writers_turn;
    int writer_executed_at_most_once_per;
    uint32_t read_lock_counter;

    pthread_cond_t can_proceed_to_critical;
    pthread_cond_t noreadprioritylock;
    pthread_cond_t readcango;
    pthread_cond_t writecango;
    pthread_mutex_t mutex1;
};

rwlock_t *rwlock_new(PRIORITY p, uint32_t n) {
    rwlock_t *newrwlock = (rwlock_t *) malloc(sizeof(rwlock_t));
    newrwlock->reader_nblock_activated = 0;
    newrwlock->priority = p;
    newrwlock->n_readers = n;
    newrwlock->writer_executed_at_most_once_per = 0;
    newrwlock->active_readers = 0;
    newrwlock->active_writers = 0;
    newrwlock->pending_readers = 0;
    newrwlock->pending_writers = 0;

    newrwlock->writers_turn = 0;
    newrwlock->read_lock_counter = 0;
    pthread_cond_init(&newrwlock->can_proceed_to_critical, NULL);
    pthread_cond_init(&newrwlock->noreadprioritylock, NULL);
    pthread_cond_init(&newrwlock->readcango, NULL);
    pthread_cond_init(&newrwlock->writecango, NULL);
    pthread_mutex_init(&newrwlock->mutex1, NULL);
    return newrwlock;
}

void rwlock_delete(rwlock_t **rw) {
    if (*rw) {
        free(*rw);
        *rw = NULL;
    }
}

// This function will lock the shared ressrouces and allow multiple readers to access the critical section simultaneously. Writers would not be allowed to access the critical section when this is activated.
// This function may be blocked from proceeding if called at the same time as the writer_lock.
void reader_lock(rwlock_t *rw) {
    // if N-Way is set, then we can only have up to N readers locked in, if there there is a Nth + 1th, reader, then we need to suspended that thread.

    // wait if there is a writer locked in.
    // if not, add a reader and make sure a write must not happen

    pthread_mutex_lock(&rw->mutex1);

    printf("\n\n * start of reader_lock * \n\n");
    printf("n_readers is %d\n", rw->n_readers);
    rw->pending_readers++;
    printf("num active readers is %d\n", rw->active_readers);
    printf("Num pending readers is %d\n", rw->pending_readers);
    printf("Num pending writers is %d\n", rw->pending_writers);
    printf("rw read lock counter is %d\n", rw->read_lock_counter);
    //while(rw->priority == N_WAY && (rw->active_readers == rw->n_readers || rw->reader_nblock_activated)){
    //	pthread_cond_wait(&rw->can_proceed_to_critical, &rw->mutex1);
    //}
    while ((rw->priority == WRITERS && rw->pending_writers > 0) || rw->active_writers > 0
           || (rw->priority == N_WAY
               && (rw->read_lock_counter >= rw->n_readers || rw->writers_turn == 1)
               && rw->pending_writers > 0)) {
        printf("suspend the read\n");
        pthread_cond_wait(&rw->noreadprioritylock, &rw->mutex1);
    }

    rw->pending_readers--;
    rw->active_readers++;
    rw->read_lock_counter++;

    printf("num active readers is %d\n", rw->active_readers);
    printf("Num pending readers is %d\n", rw->pending_readers);
    printf("Num pending writers is %d\n", rw->pending_writers);
    printf("\n\n * end of reader_lock when everything was locked * \n\n\n\n\n");
    pthread_mutex_unlock(&rw->mutex1);
}

// This function will release the lock.
void reader_unlock(rwlock_t *rw) {
    printf("\n\n * start of reader_unlock * \n\n");
    pthread_mutex_lock(&rw->mutex1);
    rw->active_readers--;
    if (rw->priority == READERS && rw->active_readers == 0 && rw->pending_readers == 0) {
        pthread_cond_signal(&rw->writecango);
        printf("writing signaled to go with %d writers waiting\n", rw->pending_writers);
    }
    printf("num active readers %d\n", rw->active_readers);
    printf("num pending readers %d\n", rw->pending_readers);
    if (rw->priority == WRITERS && rw->active_readers == 0) {
        pthread_cond_signal(&rw->writecango);
    }
    if (rw->priority == N_WAY && rw->active_readers == 0) {
        rw->writers_turn = 1;
        //rw->read_lock_counter = 0;

        pthread_cond_signal(&rw->writecango);
    }
    pthread_mutex_unlock(&rw->mutex1);
    printf("\n\n * end of reader_unlock * \n\n");
}

// This function will lock the shared resources and allow ONLY a single writer to access a crtical section at a time. No readers, or any other writers are allowed when this lock is activated!
// This function may be blocked form proceeding if it is called at the same time as the reader_lock.
void writer_lock(rwlock_t *rw) {
    // lock the writer, this means nothing can happen at this time except for this one writing who can perform the task.

    pthread_mutex_lock(&rw->mutex1);
    printf("\n\n * start of writer_lock * \n\n");
    rw->pending_writers++;
    printf("num active writers before obtaining lock: %d\n", rw->active_writers);
    printf("num pending writers %d\n", rw->pending_writers);
    printf("num pending readers %d\n", rw->pending_readers);

    if (rw->priority == N_WAY && (rw->active_writers > 0 || !rw->writers_turn)) {

        pthread_cond_wait(&rw->writecango, &rw->mutex1);
    }

    while ((rw->priority == READERS && rw->pending_readers > 0) || rw->active_readers > 0
           || rw->active_writers > 0) {
        pthread_cond_wait(&rw->writecango, &rw->mutex1);
    }

    //while(rw->active_readers > 0|| rw->active_writers > 0){
    //	printf("suspend the writer for now\n");
    //	pthread_cond_wait(&rw->writecango, &rw->mutex1);
    //}

    rw->pending_writers--;
    rw->active_writers++;
    printf("num active writers active obtaining lock: %d\n", rw->active_writers);
    printf("\n\n * end of writer_lock assuming we were able to activate the lock* \n\n\n\n\n");
    pthread_mutex_unlock(&rw->mutex1);
}

/** @brief release rw for writing--you can assume that the thread
 * releasing the lock has *already* acquired it for writing.
 *
 */
void writer_unlock(rwlock_t *rw) {

    pthread_mutex_lock(&rw->mutex1);

    rw->writer_executed_at_most_once_per++;

    //simply release the lock

    printf("\n\n * start of writer_unlock * \n\n");

    rw->active_writers--;
    printf("Number of active writers: %d and Number of pending writers: %d\n", rw->active_writers,
        rw->pending_writers);
    // If there are no more pending writers, all all the readers to go
    if (rw->priority == WRITERS && rw->active_writers == 0) {

        if (rw->pending_writers == 0) {
            pthread_cond_broadcast(&rw->noreadprioritylock);
        }

        else {
            pthread_cond_signal(&rw->writecango);
        }
    }
    if (rw->priority == READERS && rw->active_writers == 0) {
        if (rw->pending_readers > 0) {
            pthread_cond_broadcast(&rw->noreadprioritylock);
        } else {
            pthread_cond_signal(&rw->writecango);
        }
    }
    if (rw->priority == N_WAY && rw->active_writers == 0) {
        uint32_t i = 0;
        while (i < rw->n_readers && i < rw->pending_readers) {
            rw->writers_turn = 0;
            rw->read_lock_counter = 0;
            pthread_cond_signal(&rw->noreadprioritylock);

            i++;
        }
        if (i == 0) {

            pthread_cond_signal(&rw->writecango);
        }
        if (rw->pending_writers == 0) {
            printf("read broadcast\n");
            pthread_cond_broadcast(&rw->noreadprioritylock);
        }
    }

    printf("\n\n * end of writer_unlock if we have released the lock* \n\n\n\n\n");
    pthread_mutex_unlock(&rw->mutex1);
}
