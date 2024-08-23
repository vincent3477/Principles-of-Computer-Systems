# Principles-of-Computer-Systems
This repository shows my understanding of the bare bones of designing a computer system. I implemented an HTTP server, which simulates a client/ server module, that takes in requests in a certain format allowing users to either store or obtain a message from a remote server. I also implemented a bounded buffer allowing users to input or read content with multithreading. In order to maintain coherency, I implemented reader and writer locks, both of which are types of threads. For example, a reader thread cannot read (e.g. is suspended) while a writer thread is writing to a buffer, and vice versa. I then put both of these concepts I have learned from implementing an http server and r/w locks to make a multithreaded http server, in which is a server that allows multiple reads and multiple writes while maintaining coherency.
