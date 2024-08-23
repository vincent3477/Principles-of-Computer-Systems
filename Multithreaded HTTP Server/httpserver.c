#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <regex.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include "asgn2_helper_funcs.h"
#include "queue.h"
#include "rwlock.h"
#define OPTIONS "t:"

//Thread Pool: there should be two thread types, a dispatcher thread that takes the request and workers threads that stall until until the DISPATCHER thread dispatches a worker thread.
//Dispatcher should push the requests into a threadsafe queue.
//a request should be blocked if there are n thread active client or if the case coherency/ atomicity needs to be maintained.
//each file will need a rwlock such that it canLockLinkedList be tracked. For each file, will need to allocate a rwlock.

pthread_mutex_t mutex1;
pthread_cond_t wakeUpThread;
queue_t *request_queue;
int num_queue_items = 0;

static const char *const request_ex
    = "^([A-Za-z]{1,8}) /([A-Za-z0-9.-]{2,63}) "
      "HTTP/([0-9]{1}[.][0-9]{1})\r\n([a-zA-Z0-9.-]{1,128}: [ -~]{1,128}\r\n){0,}\r\n(.*)$";
static const char *const request_id_finder = "^.*Request-Id: ([0-9]{1,128})";
static char *delim = "\r\n\r\n";
static const char *const get_content_length_ex = "^.*Content-Length: ([0-9]{1,128})";

void clear_socket(int64_t socket) {

    char get_buffer[1024];

    long bytes_read = read(socket, get_buffer, sizeof(get_buffer));

    while (bytes_read > 0) {

        //fprintf(stderr, "currently cleaning out %ld bytes for socket %ld\n", bytes_read, socket);

        for (int i = 0; i < 1024; i++) {
            get_buffer[i] = '\0';
        }

        bytes_read = read_n_bytes(socket, get_buffer, sizeof(get_buffer));
    }
}

int get_file(int socket, int fd) {
    //return 1 if successful, else return fail.

    struct stat statbuf;
    fstat(fd, &statbuf);

    //fprintf(stderr, "file lock gotten\n");

    char get_buffer[4096];
    for (int i = 0; i < 4096; i++) {
        get_buffer[i] = '\0';
    }
    //int total_read = 0;
    int bytes_read = read(fd, get_buffer, sizeof(get_buffer));

    //total_read += bytes_read;
    if (bytes_read == -1) {

        return 1;
    }

    off_t num_bytes = statbuf.st_size;
    char ok_msg[50];
    sprintf(ok_msg, "HTTP/1.1 200 OK \r\nContent-Length: %ld\r\n\r\n", num_bytes);
    //fprintf(stderr, "size of okmsg is %ld\n", strlen(ok_msg));

    write_n_bytes(socket, ok_msg, strlen(ok_msg));
    //fprintf(stderr, "write ok msg\n");
    int bytes_written = 0;

    while (bytes_read != 0) {
        //char * write_out = get_buffer;
        //int bytes_remaining = bytes_read;
        bytes_written = write_n_bytes(socket, get_buffer, bytes_read);

        for (int i = 0; i < 4096; i++) {
            get_buffer[i] = '\0';
        }
        if (bytes_written == -1) {

            return 1;
        }
        //bytes_remaining -= bytes_written;
        bytes_read = read_n_bytes(fd, get_buffer, sizeof(get_buffer));
        //total_read += bytes_read;
        //write_out += bytes_written;
    }

    //pass_n_bytes(fd, socket, statbuf.st_size);
    //fprintf(stderr, "total bytes read is %d\n", bytes_read);
    return 0;
}

typedef struct ThreadObject *ThreadObj;
typedef struct ThreadObject {
    pthread_t thread;
    int id;
} ThreadObject;

typedef struct Node *fileLockNode;
typedef struct Node {
    char *filename;
    rwlock_t *lock;
    fileLockNode next;
    fileLockNode prev;
} Node;

typedef struct LockLinkedList LockLinkedList;
typedef struct LockLinkedList {
    fileLockNode head;
    fileLockNode tail;
} LockLinkedList;

fileLockNode newLock(char *file) {
    fileLockNode n = malloc(sizeof(Node));
    n->lock = rwlock_new(N_WAY, 1);
    int len_fn = strlen(file);
    n->filename = (char *) malloc(sizeof(char *) * len_fn);
    strcpy(n->filename, file);
    return n;
}

LockLinkedList *newLockList() {
    LockLinkedList *l = (LockLinkedList *) malloc(sizeof(LockLinkedList));
    if (l == NULL) {
        return NULL;
    }
    fileLockNode dummyHead = newLock("DummyFile1161011151161");
    fileLockNode dummyTail = newLock("DummyFile1161011151162");
    l->head = dummyHead;
    l->tail = dummyTail;
    l->head->next = l->tail;
    l->tail->prev = l->head;
    return l;
}

fileLockNode addFilenameLock(LockLinkedList *l, char *filename) {

    fileLockNode new_node = newLock(filename);
    if (new_node == NULL || l == NULL) {
        return false;
    }
    new_node->next = l->tail;
    new_node->prev = l->tail->prev;
    l->tail->prev->next = new_node;
    l->tail->prev = new_node;
    return new_node;
}

LockLinkedList *fileLockTracker;

void *client_thread() {

    while (1) {

        //while (num_queue_items == 0) {
        //    pthread_cond_wait(&wakeUpThread, &mutex1);
        //}
        int64_t socket = 0;
        //fprintf(stderr, "go back to beginning\n");
        queue_pop(request_queue, (void **) &socket);
        // fprintf(stderr, "socket value inside thread is %ld\n", socket);
        //fprintf(stderr, "popped something\n");
        num_queue_items -= 1;

        regex_t preg;

        int regcompile_status = regcomp(&preg, request_ex, REG_NEWLINE | REG_EXTENDED);
        if (regcompile_status != 0) {
            continue;
        }
        size_t n_matches = 6;
        regmatch_t string_matches[6];

        char buf[3072];
        int buf_iter = 0;
        while (buf_iter < 3072) {
            buf[buf_iter] = '\0';
            buf_iter++;
        }

        int free_space = 3072;
        int total_bytes_read = read_until(socket, buf, free_space, delim);
        buf[total_bytes_read] = '\0';
        //fprintf(stderr, "read until already for socket %ld?\n",socket);
        //fprintf(stderr, "%s\n", buf);
        if (total_bytes_read == -1) {

            close(socket);
            continue;
        }

        int value_match = regexec(&preg, buf, n_matches, string_matches, 0);

        //fprintf(stderr, "value match\n");

        if (value_match != 0) {
            clear_socket(socket);
            close(socket);
            continue;
        }

        static const char *const get_cmd = "GET";
        static const char *const set_cmd = "PUT";
        char function[8];
        int iter = string_matches[1].rm_so;
        while (iter < string_matches[1].rm_eo) {
            function[iter - string_matches[1].rm_so] = buf[iter];
            iter++;
        }
        function[iter - string_matches[1].rm_so] = '\0';

        if (strcmp(function, get_cmd) == 0) {
            //fprintf(stderr, "command is get for socket %ld\n", socket);
            regex_t preg2;
            regcomp(&preg2, request_id_finder, REG_NEWLINE | REG_EXTENDED);
            size_t n_header_matches = 2;
            regmatch_t header_matches[2];
            int id_not_found = regexec(&preg2, buf, n_header_matches, header_matches, 0);
            char id[128];
            if (id_not_found == 0) {
                //In the case the requestID was found, the have some variable that will represent it.
                iter = header_matches[1].rm_so;
                while (iter < header_matches[1].rm_eo) {
                    id[iter - header_matches[1].rm_so] = buf[iter];
                    iter++;
                }
                id[iter - header_matches[1].rm_so] = '\0';
            } else {
                id[0] = '0';
                id[1] = '\0';
            }
            //rwlock_t * file_operation_lock = rwlock_new(N_WAY, 1);

            //fprintf(stderr, "doen parsing the rqid for soccket %ld\n", socket);
            char file_name[63];
            iter = string_matches[2].rm_so;
            while (iter < string_matches[2].rm_eo) {
                file_name[iter - string_matches[2].rm_so] = buf[iter];
                iter++;
            }
            file_name[iter - string_matches[2].rm_so] = '\0';

            //Now we need to obtain the rwlock before proceeding. First, we have to check if the lock for this exact file already exists.
            pthread_mutex_lock(&mutex1);
            fileLockNode lockFinder = fileLockTracker->head;
            int lock_exists = 0;
            while (lockFinder != fileLockTracker->tail) {
                if (strcmp(lockFinder->filename, file_name) == 0) {
                    lock_exists = 1;
                    break;
                }
                lockFinder = lockFinder->next;
            }
            if (!lock_exists) {
                lockFinder = addFilenameLock(fileLockTracker, file_name);
            }

            if (lockFinder == NULL) {
                pthread_mutex_unlock(&mutex1);
                clear_socket(socket);
                close(socket);
                continue;
            }

            pthread_mutex_unlock(&mutex1);
            //fprintf(stderr, "try to get the read lock socket %ld\n", socket);
            //Obtain the rw lock

            reader_lock(lockFinder->lock);
            //fprintf(stderr, "got the read lock for socket %ld\n", socket);
            int fd = open(file_name, O_RDONLY, 0);

            if (fd == -1) {
                fprintf(stderr, "GET,%s,404,%s\n", file_name, id);
                reader_unlock(lockFinder->lock);
                clear_socket(socket);
                close(socket);
                continue;
            }
            fprintf(stderr, "GET,%s,200,%s\n", file_name, id);

            int get_stat = get_file(socket, fd);
            //fprintf(stderr, "done getting for socket %ld\n", socket);
            close(fd);
            reader_unlock(lockFinder->lock);
            //char bufy[50];
            //size_t more = read_n_bytes(socket, bufy, 50);
            //fprintf(stderr, "any more bytes read? %ld\n", more);

            if (get_stat == 1) {
                clear_socket(socket);
                close(socket);

                continue;
            } else {

                //fprintf(stderr,"close socket %ld\n",socket);
                clear_socket(socket);
                close(socket);
                //fprintf(stderr, "socket closed\n");
                continue;
            }
        } else if (strcmp(function, set_cmd) == 0) {
            //fprintf(stderr, "command is set for socket %ld\n", socket);
            regex_t preg2;
            regcomp(&preg2, request_id_finder, REG_NEWLINE | REG_EXTENDED);
            size_t n_header_matches = 2;
            regmatch_t header_matches[2];
            int id_not_found = regexec(&preg2, buf, n_header_matches, header_matches, 0);
            char id[128];
            if (id_not_found == 0) {
                //In the case the requestID was found, the have some variable that will represent it.
                //fprintf(stderr, "id was found\n");
                iter = header_matches[1].rm_so;
                while (iter < header_matches[1].rm_eo) {
                    id[iter - header_matches[1].rm_so] = buf[iter];
                    iter++;
                }
                id[iter - header_matches[1].rm_so] = '\0';
            } else {
                id[0] = '0';
                id[1] = '\0';
            }
            //rwlock_t * file_operation_lock = rwlock_new(N_WAY, 1);

            //fprintf(stderr, "done parsing id for socket %ld\n",socket);
            char file_name[63];
            iter = string_matches[2].rm_so;
            while (iter < string_matches[2].rm_eo) {
                file_name[iter - string_matches[2].rm_so] = buf[iter];
                iter++;
            }
            file_name[iter - string_matches[2].rm_so] = '\0';

            //Now we need to obtain the rwlock before proceeding. First, we have to check if the lock for this exact file already exists.
            pthread_mutex_lock(&mutex1);
            fileLockNode lockFinder = fileLockTracker->head;
            int lock_exists = 0;
            while (lockFinder != fileLockTracker->tail) {
                if (strcmp(lockFinder->filename, file_name) == 0) {
                    lock_exists = 1;
                    break;
                }
                lockFinder = lockFinder->next;
            }
            if (!lock_exists) {
                lockFinder = addFilenameLock(fileLockTracker, file_name);
            }
            //fprintf(stderr, "done looking for locks id\n");
            if (lockFinder == NULL) {

                pthread_mutex_unlock(&mutex1);
                clear_socket(socket);
                close(socket);
                continue;
            }

            pthread_mutex_unlock(&mutex1);
            writer_lock(lockFinder->lock);
            //fprintf(stderr, "Socket %ld has the writer lock. No other sockets may get a lock at this time.\n", socket);

            int file_created = 0;
            int fd = open(file_name, O_TRUNC | O_WRONLY);

            if (fd == -1) {

                if (errno == EACCES) {
                    //fprintf(stderr,"error here for socket %ld\n", socket);
                    writer_unlock(lockFinder->lock);
                    clear_socket(socket);
                    close(socket);
                    continue;
                } else {
                    //close(fd);
                    // Create a new file.
                    fd = open(file_name, O_TRUNC | O_WRONLY | O_CREAT, 00700);
                    file_created = 1;
                }
            }

            if (file_created == 1) {
                fprintf(stderr, "PUT,%s,201,%s\n", file_name, id);
            } else {
                fprintf(stderr, "PUT,%s,200,%s\n", file_name, id);
            }

            char *buffer_pointer = buf;
            buffer_pointer += string_matches[5].rm_so;
            regex_t preg3;
            regcomp(&preg3, get_content_length_ex, REG_NEWLINE | REG_EXTENDED);
            size_t n_header_matches1 = 2;
            regmatch_t header_matches1[2];
            int header_not_ok = regexec(&preg3, buf, n_header_matches1, header_matches1, 0);

            if (header_not_ok != 0) {
                //fprintf(stderr,"header not ok for socket %ld\n",socket);
                regfree(&preg3);
                close(fd);
                writer_unlock(lockFinder->lock);

                clear_socket(socket);

                //fprintf(stderr, "HEADER NOT OK\n");
                close(socket);

                continue;
            }

            //fprintf(stderr, "done parsing header\n");
            char bytes_specified[128];
            //printf("iter for getting cnt length is %d\n", iter);
            //printf("iter for getting cnt length end is %d\n", header_matches);
            iter = header_matches1[1].rm_so;
            while (iter < header_matches1[1].rm_eo) {
                bytes_specified[iter - header_matches1[1].rm_so] = buf[iter];
                iter++;
            }
            bytes_specified[iter - header_matches1[1].rm_so] = '\0';
            long numeric_bytes = atoi(bytes_specified);
            long specified_bytes = numeric_bytes;
            //fprintf(stderr, "specified bytes is %ld\n", specified_bytes);
            long written_bytes
                = write_n_bytes(fd, buffer_pointer, total_bytes_read - string_matches[5].rm_so);
            //fprintf(stderr, "done write for id %s as we have write %s\n", id, buffer_pointer);

            specified_bytes -= written_bytes;

            long bytes_written = pass_n_bytes(socket, fd, specified_bytes);
            //fprintf(stderr, "specified bytes is %ld, but number bytes passed is %ld for socket %ld\n", specified_bytes,bytes_written,socket);
            if (file_created == 0) {
                static char *internal_err_msg = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n";
                write_n_bytes(socket, internal_err_msg, strlen(internal_err_msg));

            } else {
                static char *internal_err_msg
                    = "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n";
                write_n_bytes(socket, internal_err_msg, strlen(internal_err_msg));
            }

            //fprintf(stderr, "Socket %ld has now realeased writer lock. Nice!\n", socket);

            close(fd);
            writer_unlock(lockFinder->lock);

            //char bufy[50];
            //size_t more = read_n_bytes(socket, bufy, 50);
            //fprintf(stderr, "any more bytes read TO WRITE? %ld\n", more);

            clear_socket(socket);
            if (bytes_written < 0) {
                //fprintf(stderr, "close writer socket\n");
                close(socket);
                continue;
            } else {
                if (file_created == 1) {
                    //fprintf(stderr, "PUT,%s,201,%s\n",file_name,id);
                    //fprintf(stderr, "close writer socket\n");
                    close(socket);
                    continue;
                } else {
                    //fprintf(stderr, "PUT,%s,200,%s\n",file_name,id);
                    //fprintf(stderr, "close writer socket\n");
                    close(socket);

                    //fprintf(stderr, "closed socket\n");
                    continue;
                }
            }

        } else {
            close(socket);
            continue;
        }
    }
}

int main(int argc, char **argv) {
    int opt = 0;
    int num_threads = 4;
    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
        switch (opt) {
        case 't': num_threads = atoi(optarg); break;
        default: return 1;
        }
    }
    printf("the number of threads entered in is %d\n", num_threads);
    printf("the number of arguements is %d\n", argc);

    request_queue = queue_new(num_threads);

    //pthread_cond_init(&wakeUpThread, NULL);
    //pthread_mutex_init(&mutex1, NULL);
    printf("init mutex\n");
    ThreadObj clientThreads[num_threads];

    Listener_Socket *sock = (Listener_Socket *) malloc(sizeof(Listener_Socket));

    //printf("the port being passed in is %d\n", atoi(argv[argc-1]));

    int listen = listener_init(sock, atoi(argv[argc - 1]));
    //int64_t socket = listener_accept(sock);
    //printf("the socket is %ld\n",socket);

    if (listen != 0) {
        return 1;
    }

    fileLockTracker = newLockList();
    printf("make filelock tracker\n");
    for (int i = 0; i < num_threads; i++) {
        clientThreads[i] = malloc(sizeof(ThreadObject));
        clientThreads[i]->id = i;
        pthread_create(&clientThreads[i]->thread, NULL, client_thread, NULL);
    }

    while (1) {

        //fprintf(stderr, "enter while loop\n");
        int64_t socket = listener_accept(sock);
        //fprintf(stderr, "socket value is %ld\n", socket);
        //pthread_mutex_lock(&mutex1);
        queue_push(request_queue, (void *) socket); // Critical Variable
        //fprintf(stderr, "about to queue push\n");
        //num_queue_items += 1; //Critical Variable

        //pthread_cond_signal(&wakeUpThread);

        //pthread_mutex_unlock(&mutex1);
    }
}
