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

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

using namespace std;

void serve_local_file(int listen_sock, int send_sock, FILE* filename);
void send_packet(int listen_sock, int send_sock, char* packet);

int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
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

    // Open file for reading
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    // TODO: Read from file, and initiate reliable data transfer to the server
    serve_local_file(listen_sockfd, send_sockfd, fp);
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

void serve_local_file(int listen_sock, int send_sock, FILE* file) {
    char send_buffer[PAYLOAD_SIZE];
    char* payload_pointer = send_buffer + 1;
    char* seq_pointer = send_buffer;
    char rec_buffer[PAYLOAD_SIZE];

    // Initializing variables
    unsigned long sent_seq_num = 1;
    unsigned long curr_window_start = sent_seq_num;
    fseek(file, 0L, SEEK_END);
    int total_bytes = ftell(file);
    fseek(file, 0L, SEEK_SET);
        

    size_t bytes_read;
    time_t timeout_start;
    while (true){
        if (sent_seq_num < curr_window_start + WINDOW_SIZE){
            bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - 1, file);            
            // If reached end of file, no transmission
            if (bytes_read == 0){
                continue;
            }

            // Set sequence number and send packet
            *seq_pointer = sent_seq_num % 256;
            send_packet(listen_sock, send_sock, send_buffer);
            sent_seq_num += 1;
            timeout_start = time(nullptr);
        } 
        else {
            bytes_read = recv(listen_sock, rec_buffer, PAYLOAD_SIZE, MSG_DONTWAIT);
            int read_error = errno;
            // Handle processing of ACK
            if (bytes_read > 0 && read_error != EWOULDBLOCK){
                // If the ACK message isn't requesting the start of the window, then we can move window forward
                if (*rec_buffer != curr_window_start){
                    curr_window_start = *rec_buffer;
                }
                else {
                    // TODO: Any logic for handling triple ACKS, etc. (think we can rely on timeouts for initial implementation)
                }
            }
            else if (read_error == EWOULDBLOCK && bytes_read <= 0) {
                // TODO: Handle error with read
            }

            // Handle timeouts
            if (TIMEOUT + timeout_start < time(nullptr)) {
                // Storing current position
                long int current_position = ftell(file);

                // Reading conent to send
                fseek(file, curr_window_start, SEEK_SET);
                bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - 1, file);   
                fseek(file, current_position, SEEK_SET);   

                // Set sequence number and send packet
                *seq_pointer = curr_window_start % 256;
                send_packet(listen_sock, send_sock, send_buffer);
                sent_seq_num += 1;
                timeout_start = time(nullptr);
            }
        }

        // If we've finished transmitting and do not have any further ACKS to receive, finish up
        if (bytes_read == 0 && curr_window_start == sent_seq_num) {
            break;
        }
    }
}

void send_packet(int listen_sock, int send_sock, char* packet) {
}
