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
#include <time.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4 
#define MAX_CLIENTS 128

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;
// Conversion factor from microsends to seconds.
const long MICROSEC_TO_SEC = 1000000;

typedef unsigned int seq_num;
typedef enum { data, ack, nack } event_type;
//typedef struct { char str[MESSAGE_SIZE]; } packet;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    int id;              /* Store client ID for synchronization with the server */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
} client_thread_data_t;

// Define UDP frame.
typedef struct {
    event_type type; // 0 -- data, 1 -- ACK, 2 -- NACK
    seq_num seq_num; 
    seq_num ack_num; 
    int id; // Client ID
    char msg[MESSAGE_SIZE]; // Packet to send.
} frame;

typedef struct {
    struct sockaddr_in addr;
    seq_num next_expected_seq;
    time_t last_seen;
} client_state;
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
    s_addr.sin_port = htons(server_port);

    // Convert address string to 32-bit binary
    int r_val = inet_pton(AF_INET, server_ip, &s_addr.sin_addr.s_addr);
    if (r_val == 0)
	    UserErrorMessage("inet_pton() failed", "invalid address string");
    else if (r_val < 0)
	    SystemErrorMessage("inet_pton() failed.");

    // Initialize variables for sending/recieving data.
    ssize_t tot_bytes_rcvd = 0;
    ssize_t bytes_rcvd = 0;
    ssize_t bytes = 0;
    int epoll = 0;
    int next_smg_num = 0;
    int msg_num = 0; 
    //int bytes_rcvd = 0;
    int retry = 1;


    frame snd_frame; // Create frame.
    strncpy(snd_frame.msg, send_buf, MESSAGE_SIZE); // Packet to send (doesn't change).
    snd_frame.id = data->id; // Set client ID.
    while (msg_num < num_requests)
    {
 	    // Get start time.
    	t_start = gettimeofday(&start, NULL);

        // Define frame metadata before sending.
        //frame snd_frame;
        //strncpy(snd_frame.msg, send_buf, MESSAGE_SIZE); //snd_frame.msg = *send_buf;
        snd_frame.seq_num = msg_num;  
        snd_frame.ack_num = msg_num;
        snd_frame.type = 0; //Data.
        //snd_fram.id = 
        retry = 1;
	    while (retry)
        {
            // Send to server.
	        bytes = sendto(data->socket_fd, &snd_frame, sizeof(frame), 0,
                (struct sockaddr *)&s_addr, sizeof(s_addr));
	        if (bytes < 0)
		        SystemErrorMessage("send() failed.");
	        else if (bytes != sizeof(snd_frame))
		        UserErrorMessage("send()","incorrect number of bytes");
            
            // Wait for response, resend on error.
            epoll = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 1);
            if (epoll < 0)
                SystemErrorMessage("epoll_wait() failure");
            else if (epoll == 0)
                retry = 1; // Timout occurred, resend.
            else
                retry = 0;
        }
	    
    	// Read data from server.
		socklen_t addr_len = sizeof(s_addr);
        frame rcv_buf;
        
        // Bytes recieved per message.
        bytes_rcvd = 0;
        retry = 1;
        while (retry)
        {
            // Receive from the server.
            bytes_rcvd = recvfrom(data->socket_fd, &rcv_buf, sizeof(rcv_buf), 0,
                (struct sockaddr *)&s_addr, &addr_len);
		    if (bytes_rcvd < 0)
                SystemErrorMessage("recv() failure");
            // Send NACK on error in transmission.
            else if (bytes_rcvd != sizeof(rcv_buf) || rcv_buf.ack_num != msg_num || rcv_buf.type != 1)
            {
                snd_frame.ack_num = msg_num;
                snd_frame.type = 2; // NACK
                // Failed to receive message.
                bytes = sendto(data->socket_fd, &snd_frame, sizeof(frame), 0,
                    (struct sockaddr *)&s_addr, sizeof(s_addr));
                
                epoll = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 1);
                if (epoll < 0)
                    SystemErrorMessage("epoll_wait() failure");
                else if (epoll == 0)
                    retry = 1; // Timout occurred, resend.
                else
                    retry = 0;
                retry = 1;
                //continue;
            }
            else // Frame was recieved correctly.
            {
                retry = 0;
            }
        
        }
        // Save the total number of bytes (across all messages).
	    tot_bytes_rcvd += bytes_rcvd;

	    // Save end time.
	    t_end = gettimeofday(&end, NULL);
        
	    // Accumulate RTT.
	    long rtt = ((end.tv_sec - start.tv_sec) * MICROSEC_TO_SEC) + (end.tv_usec - start.tv_usec);
            if (rtt < 0)
            {
                rtt = 0; // Ensure RTT is not negative
            }
            // Update total RTT
        data->total_rtt += rtt;
        data->total_messages++;
        msg_num++; // Increment the message sequence.
        
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
	// Create a UDP socket
        thread_data[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (thread_data[i].socket_fd < 0) 
	        SystemErrorMessage("Socket creation failed");
        
        //Set socket to non-blocking
        fcntl(thread_data[i].socket_fd, F_SETFL, O_NONBLOCK);

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
        thread_data[i].id = i; // Specify client ID.
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

    // Print the number of lost messages
    long lost_pkt_cnt = num_requests * num_client_threads - total_messages;
    printf("Lost packets: %ld\n", lost_pkt_cnt);
    printf("Packets sent == Packets received?: %s\n", (total_messages == num_requests * num_client_threads) ? "true" : "false");
}


void run_server()
{
    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket == -1)
        SystemErrorMessage("Server socket creation failed");

    int opt = 1;
    int recv_buf_size = 1024;
    if (setsockopt(server_socket, SOL_SOCKET, SO_RCVBUF, &opt, recv_buf_size))
        SystemErrorMessage("Setsockopt failed");

    fcntl(server_socket, F_SETFL, O_NONBLOCK);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        SystemErrorMessage("Bind failed");

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
        SystemErrorMessage("Epoll create failed");

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) == -1)
        SystemErrorMessage("Epoll_ctl failed");

    struct epoll_event events[MAX_EVENTS];

    // Initialize client tracking array
    client_state clients[MAX_CLIENTS] = {0};
    int num_clients = 0;
    
    frame rcv_frame;
    frame snd_frame;
     
    while (1)
    {
        // Wait only 1 ms for incoming data
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1);

        if (nfds == -1)
            SystemErrorMessage("Epoll wait failed");

        if (nfds > 0) {  // Only process if we have events
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
              
            // Receive datagram with client address as frame structure
            ssize_t bytes_read = recvfrom(server_socket, &rcv_frame, sizeof(rcv_frame), 0,
                (struct sockaddr*)&client_addr, &client_len);
                    
            if (bytes_read < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    perror("recvfrom() error");
                continue; // No more data or error
            }
            
            // Process based on frame type
            if (bytes_read == sizeof(frame)) {
                // Find or create client state
                int client_idx = -1;
                for (int i = 0; i < num_clients; i++) {
                    if (client_addr.sin_addr.s_addr == clients[i].addr.sin_addr.s_addr && 
                        client_addr.sin_port == clients[i].addr.sin_port) {
                        client_idx = i;
                        break;
                    }
                }
                
                // If client not found, add new client
                if (client_idx == -1 && num_clients < MAX_CLIENTS) {
                    client_idx = num_clients++;
                    memcpy(&clients[client_idx].addr, &client_addr, client_len);
                    clients[client_idx].next_expected_seq = 0; // Start expecting seq 0
                }
                
                // Update last seen timestamp
                if (client_idx >= 0) {
                    clients[client_idx].last_seen = time(NULL);
                    
                    // Prepare response frame
                    memset(&snd_frame, 0, sizeof(snd_frame));
                    memcpy(snd_frame.msg, rcv_frame.msg, MESSAGE_SIZE);
                    
                    // Process based on frame type
                    switch (rcv_frame.type) {
                        case data:
                            // Check if sequence number matches expected
                            if (rcv_frame.seq_num == clients[client_idx].next_expected_seq) {
                                // Valid sequence number - send ACK
                                snd_frame.type = ack;
                                snd_frame.ack_num = rcv_frame.seq_num;
                                // Increment expected sequence number
                                clients[client_idx].next_expected_seq++;
                            } else {
                                // Invalid sequence - send NACK
                                snd_frame.type = nack;
                                snd_frame.ack_num = clients[client_idx].next_expected_seq - 1;
                            }
                            break;
                            
                        case nack:
                            // Client indicating it didn't receive our last response
                            // Just echo back their NACK to acknowledge receipt
                            snd_frame.type = ack;
                            snd_frame.ack_num = rcv_frame.ack_num;
                            break;
                            
                        default:
                            // Unexpected frame type - respond with NACK
                            snd_frame.type = nack;
                            snd_frame.ack_num = clients[client_idx].next_expected_seq - 1;
                    }
                    
                    // Send response frame to client
                    ssize_t result = sendto(server_socket, &snd_frame, sizeof(snd_frame), 0,
                        (struct sockaddr*)&client_addr, client_len);
                        
                    if (result < 0)
                        perror("sendto() error");
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "server") == 0)
    {
        if (argc > 2)
            server_ip = argv[2];
        if (argc > 3)
            server_port = atoi(argv[3]);

        run_server();
    }
    else if (argc > 1 && strcmp(argv[1], "client") == 0)
    {
        if (argc > 2)
            server_ip = argv[2];
        if (argc > 3)
            server_port = atoi(argv[3]);
        if (argc > 4)
            num_client_threads = atoi(argv[4]);
        if (argc > 5)
            num_requests = atoi(argv[5]);

        run_client();
    }
    else
    {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}
