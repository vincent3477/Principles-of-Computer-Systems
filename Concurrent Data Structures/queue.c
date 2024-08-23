#include "queue.h"
#include <stdlib.h>
#include <semaphore.h>
#include <stdio.h>

sem_t buffer_empty;
sem_t buffer_full;
sem_t mutex_lock;

// allocate memory for a queue in which the function will be called queue_new.
// it will hold an most "size" elements.
// readme has to include instruct design decisions and outlines the highlevel functions, data structurs, and modules used in my code

/*Queue struct
1. Push at tail
2. Pop at head
*/
typedef struct Node *QueueNode;
typedef struct Node {
    void *element;
    QueueNode next;
    QueueNode prev;
} Node;

struct queue {
    uint64_t max_size;
    uint64_t curr_size;
    QueueNode head;
    QueueNode tail;
};

QueueNode create_node(void *new_element) {
    printf("node created\n");
    QueueNode n = malloc(sizeof(Node));
    if (n == NULL) {
        return NULL;
    }
    n->next = NULL;
    n->prev = NULL;
    n->element = new_element;
    return n;
}

void delete_node(QueueNode *qn) {
    if ((*qn) != NULL) {
        free(*qn);
        *qn = NULL;
    }
}

queue_t *queue_new(int size) {
    sem_init(&buffer_empty, 0, 0);
    sem_init(&buffer_full, 0, size);
    sem_init(&mutex_lock, 0, 1);
    queue_t *new_queue = (queue_t *) malloc(sizeof(queue_t));
    QueueNode dummy_head = create_node("dummy1");
    QueueNode dummy_tail = create_node("dummy2");
    new_queue->max_size = size;
    new_queue->curr_size = 0;
    new_queue->head = dummy_head;
    new_queue->tail = dummy_tail;
    new_queue->head->next = new_queue->tail;
    new_queue->tail->prev = new_queue->head;
    return new_queue;
}

void queue_delete(queue_t **q) {
    if ((*q) != NULL) {

        QueueNode curr = (*q)->head;

        while (curr != NULL) {
            QueueNode temp = curr;

            curr = curr->next;
            free(temp);
        }
        free(*q);
        (*q) = NULL;
    }
}

bool queue_push(queue_t *q, void *elem) {
    sem_wait(&buffer_full);
    sem_wait(&mutex_lock);
    QueueNode new_node = create_node(elem);
    if (new_node == NULL || q == NULL) {
        sem_post(&mutex_lock);
        sem_post(&buffer_full);
        return false;
    }
    new_node->next = q->tail;
    new_node->prev = q->tail->prev;
    q->tail->prev->next = new_node;
    q->tail->prev = new_node;
    sem_post(&mutex_lock);
    sem_post(&buffer_empty);
    return true;
}

bool queue_pop(queue_t *q, void **elem) {
    sem_wait(&buffer_empty);

    sem_wait(&mutex_lock);
    if (q == NULL) {
        sem_post(&mutex_lock);
        sem_post(&buffer_empty);
        return false;
    }

    *elem = q->head->next->element;

    QueueNode delete_node = q->head->next;

    q->head->next = q->head->next->next;

    q->head->next->prev = q->head;

    free(delete_node);
    delete_node = NULL;
    sem_post(&mutex_lock);
    sem_post(&buffer_full);
    return true;
}
