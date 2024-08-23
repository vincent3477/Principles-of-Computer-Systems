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
#include "asgn2_helper_funcs.h"

// Step 1: Implement these funtionalities: init, listen, accept, read, and close. GET will NOT include a message body.

// The max amount of character for a message, not including the message body, must not exceed 2048 chars.
// Method (eg. GET & put) will contain at most 8 characters.
static const char *const request_ex
    = "^([A-Za-z]{1,8}) /([A-Za-z0-9.-]{2,63}) "
      "HTTP/([0-9]{1}[.][0-9]{1})\r\n([a-zA-Z0-9.-]{1,128}: [ -~]{1,128}\r\n){0,}\r\n(.*)$";
static const char *const get_content_length_ex = "^.*Content-Length: ([0-9]{1,128})";
static char *delim = "\r\n\r\n";

/*
Format of request expression with REGEX
0. THE ENTIRE EXPRESSION
1. method (get/put)
2. URI (filename.txt)
3. HTTP Version Number
4. Headers (Content-Length: 36)
5. Message Body (Optional)
*/

/*
helper funcs syntax

ssize_t write_n_bytes(int out, char buf[], size_t n);

ssize_t read_n_bytes(int in, char buf[], size_t n);

ssize_t pass_n_bytes(int src, int dst, size_t n);
*/

int get_file(int socket, int fd) {
    //return 1 if successful, else return fail.

    struct stat statbuf;
    fstat(fd, &statbuf);

    off_t num_bytes = statbuf.st_size;

    static char get_buffer[4096];
    //int total_read = 0;
    int bytes_read = read(fd, get_buffer, sizeof(get_buffer));

    //total_read += bytes_read;
    if (bytes_read == -1) {

        static char *internal_err_msg = "HTTP/1.1 505 Internal Server Error\r\nContent-Length: "
                                        "22\r\n\r\nInternal Server Error\n";
        write_n_bytes(socket, internal_err_msg, strlen(internal_err_msg));
        return 1;
    }

    char ok_msg[50];
    sprintf(ok_msg, "HTTP/1.1 200 OK \r\nContent-Length: %ld\r\n\r\n", num_bytes);
    //fprintf(stderr, "size of okmsg is %ld\n", strlen(ok_msg));

    write_n_bytes(socket, ok_msg, strlen(ok_msg));

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
    //printf("total bytes read is %d\n", total_read);
    return 0;
}

int main(int argc, char **argv) {

    // If the user did not enter a port number.
    if (argv[1] == NULL) {
        fprintf(stderr, "Invalid Port\n");
        return 1;
    }
    if (atoi(argv[1]) >= 1 && atoi(argv[1]) <= 65535 && argc == 2) {

        regex_t preg;

        Listener_Socket *sock = (Listener_Socket *) malloc(sizeof(Listener_Socket));

        int listen = listener_init(sock, atoi(argv[1]));

        if (listen != 0) {
            regfree(&preg);
            return 1;
        }

        //try using write.
        while (1) {
            int socket = listener_accept(sock);

            int regcompile_status = regcomp(&preg, request_ex, REG_NEWLINE | REG_EXTENDED);
            size_t n_matches = 6;
            regmatch_t string_matches[6];

            if (regcompile_status != 0) {
                static char *internal_err_msg
                    = "HTTP/1.1 505 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                      "Server Error\n";
                write_n_bytes(socket, internal_err_msg, strlen(internal_err_msg));
                close(socket);

                continue;
            }

            // Initialize the buffer and get info abotu the free space we have.
            /*static char buf[3072];
			int free_space = 100;
			int total_bytes_read = 0;
			
			int bytes_read = read(socket, buf, free_space);
			//perror("recv:");
			//int bytes_read = read_n_bytes(socket, buf, free_space);
			//free_space -= bytes_read;
			total_bytes_read += bytes_read;
			
			while(bytes_read != 0){
				printf("btyes read is %d\n", bytes_read);
				printf("stuff being passed in is %s\n", buf);
				bytes_read = read(socket, buf, free_space);
				if(bytes_read == -1){
					perror("recv:");
					printf("sometjhing went wrong\n");
					break;
		
				}
				printf("bytes read is %d\n", bytes_read);
				//free_space -= bytes_read;
				total_bytes_read += bytes_read;
			}
			*/
            static char buf[3072];
            int free_space = 3072;
            int total_bytes_read = read_until(socket, buf, free_space, delim);

            if (total_bytes_read == -1) {

                static char *internal_err_msg
                    = "HTTP/1.1 505 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                      "Server Error\n";
                write_n_bytes(socket, internal_err_msg, strlen(internal_err_msg));
                close(socket);
                continue;
            }

            int value_match = regexec(&preg, buf, n_matches, string_matches, 0);

            if (value_match != 0) {
                static char *bad_req_msg
                    = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
                write_n_bytes(socket, bad_req_msg, strlen(bad_req_msg));
                close(socket);
                continue;
            }

            //Check and make sure the version of the server is valid
            char http_version[10];
            int iter = string_matches[3].rm_so;
            while (iter < string_matches[3].rm_eo) {
                http_version[iter - string_matches[3].rm_so] = buf[iter];
                iter++;
            }
            http_version[iter - string_matches[3].rm_so] = '\0';

            static const char *const supported_version = "1.1";
            if (strcmp(http_version, supported_version) != 0) {
                static char *wrong_v_msg = "HTTP/1.1 505 Version Not Supported\r\nContent-Length: "
                                           "22\r\n\r\nVersion Not Supported\n";
                write_n_bytes(socket, wrong_v_msg, strlen(wrong_v_msg));

                close(socket);
                continue;
            }

            //Check what type request we are getting.
            static const char *const get_cmd = "GET";
            static const char *const set_cmd = "PUT";
            char function[8];
            iter = string_matches[1].rm_so;
            while (iter < string_matches[1].rm_eo) {
                function[iter - string_matches[1].rm_so] = buf[iter];
                iter++;
            }
            function[iter - string_matches[1].rm_so] = '\0';

            //Check Validity of filename and and try to open it.
            char file_name[63];
            iter = string_matches[2].rm_so;
            while (iter < string_matches[2].rm_eo) {
                file_name[iter - string_matches[2].rm_so] = buf[iter];
                iter++;
            }
            file_name[iter - string_matches[2].rm_so] = '\0';

            if (strcmp(function, get_cmd) == 0) {

                int fd = open(file_name, O_RDONLY, 0);

                struct stat statbuf;
                fstat(fd, &statbuf);

                if (fd == -1) {
                    if (errno == EACCES) {
                        char *forbidden_err_msg
                            = "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n";

                        write_n_bytes(socket, forbidden_err_msg, strlen(forbidden_err_msg));
                        close(socket);

                        continue;
                    } else {
                        char *not_found_err_msg
                            = "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n";

                        write_n_bytes(socket, not_found_err_msg, strlen(not_found_err_msg));
                        close(socket);

                        continue;
                    }

                } else {

                    if (!S_ISREG(statbuf.st_mode)) {
                        char *forbidden_err_msg
                            = "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n";

                        write_n_bytes(socket, forbidden_err_msg, strlen(forbidden_err_msg));
                        close(socket);
                        continue;
                    }

                    get_file(socket, fd);
                    close(fd);
                }

            } else if (strcmp(function, set_cmd) == 0) {

                int finish_status = 0;
                //finish_status: (0) means nothing went wrong, (1) means internal error in server, (2) means header and number of bytes in actual file do not match up.
                int file_created = 0;
                int fd = open(file_name, O_TRUNC | O_WRONLY);

                if (fd == -1) {

                    if (errno == EACCES) {
                        static char *bad_req_msg
                            = "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n";
                        write_n_bytes(socket, bad_req_msg, strlen(bad_req_msg));
                        close(socket);
                        continue;
                    } else {
                        //close(fd);
                        // Create a new file.
                        fd = open(file_name, O_TRUNC | O_WRONLY | O_CREAT, 00700);
                        file_created = 1;
                    }
                }

                char *buffer_pointer = buf;
                buffer_pointer += string_matches[5].rm_so;
                regex_t preg2;
                regcomp(&preg2, get_content_length_ex, REG_NEWLINE | REG_EXTENDED);
                size_t n_header_matches = 2;
                regmatch_t header_matches[2];
                int header_not_ok = regexec(&preg2, buf, n_header_matches, header_matches, 0);

                if (header_not_ok != 0) {

                    static char *bad_req_msg
                        = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
                    write_n_bytes(socket, bad_req_msg, strlen(bad_req_msg));
                    regfree(&preg2);
                    close(socket);
                    close(fd);
                    continue;
                }

                char bytes_specified[128];
                //printf("iter for getting cnt length is %d\n", iter);
                //printf("iter for getting cnt length end is %d\n", header_matches);
                iter = header_matches[1].rm_so;
                while (iter < header_matches[1].rm_eo) {
                    bytes_specified[iter - header_matches[1].rm_so] = buf[iter];
                    iter++;
                }
                bytes_specified[iter - header_matches[1].rm_so] = '\0';
                long numeric_bytes = atoi(bytes_specified);
                long specified_bytes = numeric_bytes;

                buf[strlen(buf)] = '\0';

                long bytes_written_to_file
                    = write(fd, buffer_pointer, total_bytes_read - string_matches[5].rm_so);
                numeric_bytes -= bytes_written_to_file;
                while (1) {

                    if (numeric_bytes <= 0) {
                        break;
                    }

                    long content_read_in = read_n_bytes(socket, buf, numeric_bytes);

                    if (content_read_in == 0 || numeric_bytes <= 0) {

                        break;
                    }

                    if (content_read_in == -1) {
                        finish_status = 1;
                        perror("read:");

                        break;
                    }

                    //char * put_write_ptr = buf;
                    //fprintf(stderr, "write %s\n", buffer_pointer);
                    bytes_written_to_file += write_n_bytes(fd, buf, numeric_bytes);
                    buf[0] = '\0';
                    numeric_bytes -= bytes_written_to_file;
                    //buffer_
                }

                if (finish_status == 1) {
                    static char *internal_err_msg
                        = "HTTP/1.1 505 Internal Server Error\r\nContent-Length: "
                          "22\r\n\r\nInternal Server Error\n";
                    write_n_bytes(socket, internal_err_msg, strlen(internal_err_msg));
                    regfree(&preg2);
                    close(socket);
                    close(fd);
                    continue;
                }
                if (bytes_written_to_file != specified_bytes) {

                    finish_status = 2;
                }

                if (finish_status == 0 && file_created == 0) {
                    static char *ok_msg = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n";
                    write_n_bytes(socket, ok_msg, strlen(ok_msg));
                    regfree(&preg2);
                    close(socket);
                    close(fd);
                    continue;
                } else if (finish_status == 0 && file_created == 1) {
                    static char *created_msg
                        = "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n";
                    write_n_bytes(socket, created_msg, strlen(created_msg));
                    regfree(&preg2);
                    close(socket);
                    close(fd);
                    continue;
                } else if (finish_status == 2) {
                    static char *bad_req_msg
                        = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
                    write_n_bytes(socket, bad_req_msg, strlen(bad_req_msg));
                    if (file_created == 1) {
                        unlink(file_name);
                    }
                    regfree(&preg2);
                    close(socket);
                    close(fd);
                    continue;
                }

            } else {
                char *not_implemented_err_msg
                    = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n";
                write_n_bytes(socket, not_implemented_err_msg, strlen(not_implemented_err_msg));
                close(socket);
                continue;
            }

            //write_n_bytes(socket, buf, 100);
            for (int i = 0; i < 3072; i++) {
                buf[i] = '\0';
            }

            close(socket);
        }
        return 0;
    } else {
        fprintf(stderr, "Invalid Port\n");
        return 1;
    }
}
