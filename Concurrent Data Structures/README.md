# Assignment 3 directory

This directory contains source code and other files for Assignment 3

This directory contains implemations for both queue.c and rwlock.c.

In order for me to implement queue.c, I used a basic lock structure that would essentially protect all the data whenever a push or pop is currently in progress. If the queue is full or empty (obviously cannot be both), I used semaphores that would block if one of the conditions became true. For example, if a queue is full, block the pushoperation until we can pop something. The queue data structure was implemented using a doubly-linked list, where each node, except for head and tail, represents an element from the queue. The element after head is to be popped. The element before tail is the element that was just pushed into the queue.

In order for me to implement rwlock.c, I initially thought of using semaphores solely representing those as conditional variables to block readers/ writers. However, when implementing N-Way, I found out how incredibly difficult it was with semaphores only. Conditional variables (as Integers) and many semaphores were added for checking edge cases and blocking if it was necessary. After a lot of failures with attempting to get N-Way to function, I decided to reconceptialize my rwlock with a different type of thread lock, pthread mutexes and conditional variables. Implementing rwlock using semaphores is possible, but I find it that it will incredibly difficult and time consuming to debug given the scope of time. I realize how much easier it is to control the number of reader threads to release from blocking for N-way while checking for the necessary conditions.
