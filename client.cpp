#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <string>
#include <iostream>
#include <map>
#include <fstream>
#include <iterator>
#include <vector>
#include <regex>
#include "utils.h"
#include <ctime>
#include <poll.h>
#include <algorithm>

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

using namespace std;

void serve_local_file(int listen_sock, int send_sock, FILE* file, sockaddr_in send_addr, sockaddr_in rec_addr);

int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to;
    socklen_t addr_size = sizeof(server_addr_to);

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Setting default send address
    if(connect(send_sockfd, (struct sockaddr *)&server_addr_to, sizeof(server_addr_to)) < 0) 
    { 
        printf("\n Error : Connect Failed \n"); 
        exit(0); 
    } 

    // Open file for reading
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    // TODO: Read from file, and initiate reliable data transfer to the server
    serve_local_file(listen_sockfd, send_sockfd, fp, server_addr_to, client_addr);
    // serve_local_file3(listen_sockfd,send_sockfd,fp);

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

void serve_local_file(int listen_sock, int send_sock, FILE* file, sockaddr_in send_addr, sockaddr_in rec_addr) {
    // Buffer variables (send and receive)
    char send_buffer[PAYLOAD_SIZE];
    char* payload_pointer = send_buffer + 1;
    unsigned char* seq_pointer = (unsigned char*)send_buffer;
    unsigned char rec_buffer[PAYLOAD_SIZE];
    socklen_t sock_addr_len = sizeof(rec_addr);
    size_t bytes_read = -1, ack_bytes_read;

    // Polling variables
    struct pollfd fds[1];
    fds[0].fd = listen_sock;
    fds[0].events = POLLIN;

    fseek(file, 0L, SEEK_END);
    int total_bytes = ftell(file); //unsigned int
    fseek(file, 0L, SEEK_SET);

    // Window size variables
    unsigned int max_window = RECIEVE_WINDOW_SIZE;
    unsigned long sent_seq_num = 1;
    unsigned long curr_window_start = sent_seq_num;
    unsigned long cwnd = 1, cwnd_frac = 0, ssh = START_SSTHRESH, dup_ack_count = 0;
    struct timeval timeout_start, curr, add, cmp;
    add.tv_sec = (int)TIMEOUT;
    add.tv_usec = (TIMEOUT - (int)TIMEOUT) * 1000000;

    struct timeval tval_logging;

    while (true){
        if (sent_seq_num < curr_window_start + cwnd){
            bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - HEADER_SIZE, file);   
            // If reached end of file, no transmission
            if (bytes_read > 0){
                // Set sequence number and send packet
                if (LOGGING_ENABLED) {
                    gettimeofday(&tval_logging, NULL);
                    printf("%ld.%06ld | CLIENT: SEND NEW PACKET  | Sending seq #: %u, %u bytes\n", tval_logging.tv_sec, tval_logging.tv_usec, sent_seq_num, (bytes_read + HEADER_SIZE));
                    printf("%ld.%06ld | CLIENT: WINDOW INFO      | window size is %u, starting at %u\n", tval_logging.tv_sec, tval_logging.tv_usec, cwnd, curr_window_start);
                }
                *seq_pointer = sent_seq_num % max_window;
                sendto(send_sock, send_buffer, bytes_read + HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                sent_seq_num += 1;
                gettimeofday(&timeout_start, NULL);
            }
        } 
        
        int listen_results = poll(fds, 1, 10);
        if (listen_results > 0 && fds[0].revents & POLLIN){
            ack_bytes_read = recvfrom(listen_sock, rec_buffer, PAYLOAD_SIZE, MSG_DONTWAIT, (struct sockaddr*)&rec_addr, &sock_addr_len);
            int read_error = errno;
            // Handle processing of ACK
            if (ack_bytes_read > 0 && read_error != EWOULDBLOCK){
                // If the ACK message isn't requesting the start of the window, then we can move window forward
                if (*rec_buffer != (curr_window_start % max_window)){
                    if (LOGGING_ENABLED) { 
                        gettimeofday(&tval_logging, NULL);
                        printf("%ld.%06ld | CLIENT: ACKED PACKET     | received ack for seq #: %u, %u bytes\n", tval_logging.tv_sec, tval_logging.tv_usec, *rec_buffer, ack_bytes_read);
                    }
                    for (unsigned long i = curr_window_start; i <= curr_window_start + MAX_WINDOW_SIZE; i++){
                        if (i % max_window == *rec_buffer){
                            unsigned int packet_diff = i - curr_window_start;
                            curr_window_start = i;

                            // Handle exiting out of fast recovery
                            if (dup_ack_count >= (unsigned long)DUP_ACK_LIMIT){
                                cwnd = ssh;
                                if (LOGGING_ENABLED) { 
                                    gettimeofday(&tval_logging, NULL);
                                    printf("%ld.%06ld | CLIENT: FAST RETRANSMIT  | Updating window size to %u, starting at %u\n", tval_logging.tv_sec, tval_logging.tv_usec, cwnd, curr_window_start);
                                }
                            }
                            // Update window size if in slow start
                            else if (cwnd <= ssh && cwnd < MAX_WINDOW_SIZE){
                                cwnd += packet_diff;
                                if (LOGGING_ENABLED) { 
                                    gettimeofday(&tval_logging, NULL);
                                    printf("%ld.%06ld | CLIENT: SLOW START       | Updating window size to %u, starting at %u\n", tval_logging.tv_sec, tval_logging.tv_usec, cwnd, curr_window_start);
                                }
                            }
                            // Update window size if in congestion avoidance
                            else if (cwnd < MAX_WINDOW_SIZE){
                                cwnd_frac += packet_diff;
                                if (cwnd_frac >= cwnd){
                                    cwnd_frac %= cwnd;
                                    cwnd++; 
                                }
                                if (LOGGING_ENABLED) { 
                                    gettimeofday(&tval_logging, NULL);
                                    printf("%ld.%06ld | CLIENT: CONGESTION AVOID | Updating window size to %u, starting at %u\n", tval_logging.tv_sec, tval_logging.tv_usec, cwnd, curr_window_start);
                                }
                            }
                            dup_ack_count = 0;
                            break;
                        }
                    }
                }
                else {
                    dup_ack_count += 1;
                    if (LOGGING_ENABLED) {
                        gettimeofday(&tval_logging, NULL);
                        printf("%ld.%06ld | CLIENT: DUPLICATE ACK    | received ack for seq #: %u, %u bytes\n", tval_logging.tv_sec, tval_logging.tv_usec, *rec_buffer, ack_bytes_read);
                    }
                    // Entering fast retransmit 
                    if (dup_ack_count + (unsigned long)1 == (unsigned long)DUP_ACK_LIMIT){
                        long int current_position = ftell(file);

                        // Reading conent to send
                        fseek(file, (curr_window_start - 1) * (PAYLOAD_SIZE - HEADER_SIZE), SEEK_SET);
                        bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - HEADER_SIZE, file);   
                        fseek(file, current_position, SEEK_SET);   

                        // Set sequence number and send packet
                        if (LOGGING_ENABLED) {
                            gettimeofday(&tval_logging, NULL);
                            printf("%ld.%06ld | CLIENT: FAST RETRANSMIT  | Resending seq #: %u, %u bytes\n", tval_logging.tv_sec, tval_logging.tv_usec, curr_window_start, (bytes_read + HEADER_SIZE));
                        }
                        *seq_pointer = curr_window_start % max_window;
                        sendto(send_sock, send_buffer, bytes_read + HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                        gettimeofday(&timeout_start, NULL);

                        // Updating window size
                        ssh = max(cwnd / 2, (unsigned long)2);
                        cwnd = ssh + 3;
                        if (LOGGING_ENABLED) {
                            gettimeofday(&tval_logging, NULL);
                            printf("%ld.%06ld | CLIENT: FAST RETRANSMIT  | Updating window size to %u, starting at %u\n", tval_logging.tv_sec, tval_logging.tv_usec, cwnd, curr_window_start);
                        }
                    }
                    // Fast recovery for duplicate ack
                    else if (dup_ack_count > DUP_ACK_LIMIT){
                        cwnd += 1;
                        if (LOGGING_ENABLED) {
                            gettimeofday(&tval_logging, NULL);
                            printf("%ld.%06ld | CLIENT: FAST RECOVERY    | Updating window size to %u, starting at %u\n", tval_logging.tv_sec, tval_logging.tv_usec, cwnd, curr_window_start);
                        }
                    }
                }
            }
        }

        gettimeofday(&curr, NULL);
        timeradd(&timeout_start, &add, &cmp);

        // Handle timeouts
        if (timercmp(&cmp, &curr, <)) {
            // Storing current position
            long int current_position = ftell(file);

            // Reading conent to send
            fseek(file, (curr_window_start - 1) * (PAYLOAD_SIZE - HEADER_SIZE), SEEK_SET);
            bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - HEADER_SIZE, file);   
            fseek(file, current_position, SEEK_SET);   
            // sent_seq_num = curr_window_start + 1;

            // Set sequence number and send packet
            if (LOGGING_ENABLED) {
                gettimeofday(&tval_logging, NULL);
                printf("%ld.%06ld | CLIENT: PACKET TIMEOUT   | Resending seq #: %u, %u bytes \n", tval_logging.tv_sec, tval_logging.tv_usec, curr_window_start, (bytes_read + HEADER_SIZE));
            }
            *seq_pointer = curr_window_start % max_window;
            sendto(send_sock, send_buffer, bytes_read + HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
            gettimeofday(&timeout_start, NULL);

            // Update window size for timeout
            ssh = max(cwnd / 2, (unsigned long)2);
            cwnd = 1;
            if (LOGGING_ENABLED){
                gettimeofday(&tval_logging, NULL);
                printf("%ld.%06ld | CLIENT: WINDOW INFO      | Upating window size to %u, starting at %u \n", tval_logging.tv_sec, tval_logging.tv_usec, cwnd, curr_window_start);
            }
        }

        // If we've finished transmitting and do not have any further ACKS to receive, finish up
        if (bytes_read == 0 && curr_window_start == sent_seq_num) {
            break;
        }
    }

    // Sending notifications to close connection
    *seq_pointer = CLOSE_PACKET_NUM;

    if (LOGGING_ENABLED) {
        gettimeofday(&tval_logging, NULL);
        printf("%ld.%06ld | CLIENT: CLOSING REQ      | Requesting closing #: %u, %u bytes \n", tval_logging.tv_sec, tval_logging.tv_usec, *seq_pointer, 1);
    }
    sendto(send_sock, send_buffer, 1, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
    gettimeofday(&timeout_start, NULL);
    while (true) {
        gettimeofday(&tval_logging, NULL);

        // Resend closing message
        gettimeofday(&curr, NULL);
        timeradd(&timeout_start, &add, &cmp);
        // Handle timeouts
        if (timercmp(&cmp, &curr, <)) {
            sendto(send_sock, send_buffer, HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
            gettimeofday(&timeout_start, NULL);
            if (LOGGING_ENABLED) {
                gettimeofday(&tval_logging, NULL);
                printf("%ld.%06ld | CLIENT: CLOSING REQ      | Requesting closing #: %u, %u bytes \n", tval_logging.tv_sec, tval_logging.tv_usec, *seq_pointer, 1);
            }
        }
        
        int listen_results = poll(fds, 1, 10);
        if (listen_results > 0 && fds[0].revents & POLLIN){
            ack_bytes_read = recvfrom(listen_sock, rec_buffer, PAYLOAD_SIZE, MSG_DONTWAIT, (struct sockaddr*)&rec_addr, &sock_addr_len);
            int read_error = errno;
            // Handle processing of ACK
            if (ack_bytes_read > 0 && read_error != EWOULDBLOCK){
                if (*rec_buffer == CLOSE_PACKET_NUM){
                    if (LOGGING_ENABLED) { 
                        gettimeofday(&tval_logging, NULL);
                        printf("%ld.%06ld | CLIENT: TERMINATING ACK  | closing connection", tval_logging.tv_sec, tval_logging.tv_usec);
                        
                    }
                    break;
                }
            }
        }
    }
}
