/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here

# Student #1: Fernando C. Tanase Mosneagu
# Student #2: Cameron C. Lira
# Student #3: Justin Elkins

*/
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;
// Conversion factor from microsends to seconds.
const long MICROSEC_TO_SEC = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
} client_thread_data_t;


/*
 * This function handles system errors, prints out the results from errno.
 */
void SystemErrorMessage(const char *msg)
{
	perror(msg);
	exit(1);
}

/*
 * This function handles non-system errors.
 *
./pa1_skeleton client 127.0.0.1 12345 4 1000000
*/
 void UserErrorMessage(const char *msg, const char *info)
{
	fputs(msg, stderr);
	fputs(": ", stderr);
	fputs(info, stderr);
	fputc('\n', stderr);
	exit(1);
}

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;

    // Create timezone struct for use in gettimeofday();
    struct timezone t_zone;
    t_zone.tz_minuteswest = 0;
    t_zone.tz_dsttime = 0;

    int t_start = 0;
    int t_end = 0;

    // Create address
    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(s_addr)); // Zero out.
    s_addr.sin_family = AF_INET; // Set family.

    // Convert address string to 32-bit binary
    int r_val = inet_pton(AF_INET, server_ip, &s_addr.sin_addr.s_addr);
    if (r_val == 0)
	    UserErrorMessage("inet_pton() failed", "invalid address string");
    else if (r_val < 0)
	    SystemErrorMessage("inet_pton() failed.");

    // Set server port.
    s_addr.sin_port = server_port;

    // Initialize variables for sending/recieving data.
    ssize_t bytes = 0;
    ssize_t bytes_rcvd = 0;
    int epoll = 0;

    for (int i = 0; i < num_requests; i++)
    {
 	    // Get start time.
    	t_start = gettimeofday(&start, NULL);

	    // Send to server.
	    bytes = send(data->socket_fd, send_buf, MESSAGE_SIZE, 0);
	    if (bytes < 0)
		    SystemErrorMessage("send() failed.");
	    else if (bytes != MESSAGE_SIZE)
		    UserErrorMessage("send()","incorrect number of bytes");

	    // Recieve from server.
	    int bytes_rcvd = 0;
	    while (bytes_rcvd < MESSAGE_SIZE)
	    {
		    // Use epoll to wait for return message.
		    epoll = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);
		    if (epoll < 0)
			    SystemErrorMessage("epoll_wait() failure");
		    // Read data from server.
		    ssize_t recv_rcvd = recv(data->socket_fd, recv_buf + bytes_rcvd, MESSAGE_SIZE - bytes_rcvd, 0);
		    if (bytes_rcvd < 0)
			    SystemErrorMessage("recv() failure");
		    // Save the number of
		    bytes_rcvd += recv_rcvd;
	    }

	    // Save end time.
	    t_end = gettimeofday(&end, NULL);

	    // Accumulate RTT.
	    long rtt = ((end.tv_sec - start.tv_sec) * MICROSEC_TO_SEC) + (end.tv_usec - start.tv_usec);
            if (rtt < 0) rtt = 0; // Ensure RTT is not negative

            // Update total RTT
            data->total_rtt += rtt;
            data->total_messages++;

    }

       return NULL;
}



/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() 
{
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;

    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
	
    for (int i = 0; i < num_client_threads; i++) 
    {
	// Create a TCP socket
        thread_data[i].socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (thread_data[i].socket_fd < 0) 
	        SystemErrorMessage("Socket creation failed");
        
        // Connect the socket to the server
        if (connect(thread_data[i].socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
	        SystemErrorMessage("Connection to server failed");
        
        // Create an epoll instance for the thread
        thread_data[i].epoll_fd = epoll_create1(0);
        if (thread_data[i].epoll_fd < 0) 
	        SystemErrorMessage("Epoll creation failed");
      
        // Register the socket file descriptor with epoll for monitoring incoming data
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = thread_data[i].socket_fd;
        if (epoll_ctl(thread_data[i].epoll_fd, EPOLL_CTL_ADD, thread_data[i].socket_fd, &event) < 0) 
	        SystemErrorMessage("Epoll control failed");

        // Initialize thread statistics
        thread_data[i].total_rtt = 0;
        thread_data[i].total_messages = 0;

        // Create a new thread for handling client communication
        pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
    }

    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0.0;

    for (int i = 0; i < num_client_threads; i++) 
    {
        // Wait for the client threads to finish
        pthread_join(threads[i], NULL);
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;

        // Close the socket and epoll file descriptors
        close(thread_data[i].socket_fd);
        close(thread_data[i].epoll_fd);
    }

        // Fix calculation of Total Request Rate
    if (total_rtt > 0) 
    {
        total_request_rate = ((float)total_messages * MICROSEC_TO_SEC) / (float)total_rtt;
    } 
    else 
    {
        total_request_rate = 0.0;
    }

    // Print average RTT and total request rate statistics
    if (total_messages > 0) 
    {
        printf("Average RTT: %lld us\n", total_rtt / total_messages);
    } 
    else 
    {
        printf("No messages were processed.\n");
    }
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) 
        SystemErrorMessage("Server socket creation failed");
 
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) 
        SystemErrorMessage("Setsockopt failed");

    fcntl(server_socket, F_SETFL, O_NONBLOCK);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) 
        SystemErrorMessage("Bind failed");

    if (listen(server_socket, SOMAXCONN) == -1) 
        SystemErrorMessage("Listen failed");

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) 
        SystemErrorMessage("Epoll create failed");

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1) 
        SystemErrorMessage("Epoll_ctl failed");

    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) 
            SystemErrorMessage("Epoll wait failed");

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_socket) {
                // Accept all pending connections
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
                    
                    if (client_socket == -1) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("Accept failed");
                        }
                        break;  // No more connections to accept
                    }

                    fcntl(client_socket, F_SETFL, O_NONBLOCK);

                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = client_socket;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev) == -1) {
                        perror("Epoll_ctl failed for client socket");
                        close(client_socket);
                        continue;
                    }
                }
            } else {
                int client_socket = events[i].data.fd;
                char buffer[MESSAGE_SIZE];
                
                // Handle data from client - proper edge-triggered handling by reading until EAGAIN
                while (1) {
                    ssize_t bytes_read = read(client_socket, buffer, MESSAGE_SIZE);
                    
                    if (bytes_read == -1) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            close(client_socket);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL);
                        }
                        break;  // Either error or no more data
                    }
                    
                    if (bytes_read == 0) {
                        close(client_socket);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL);
                        break;  // Connection closed
                    }

                    // Echo back all the data read
                    size_t bytes_to_write = bytes_read;
                    size_t bytes_written = 0;
                    while (bytes_written < bytes_to_write) {
                        ssize_t result = write(client_socket, buffer + bytes_written, bytes_to_write - bytes_written);
                        if (result == -1) {
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                close(client_socket);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, NULL);
                                break;
                            }
                            // Would block, try again later
                            break;
                        }
                        bytes_written += result;
                    }
                }
            }
        }
    }
    
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}
