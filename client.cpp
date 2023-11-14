#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string>
#include <iostream>
#include <map>
#include <fstream>
#include <iterator>
#include <vector>
#include <regex>

#include "utils.h"

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
    char buffer[PAYLOAD_SIZE];
    char* payload_pointer = buffer + 1;
    char* seq_pointer = buffer;

    char seq_num = 1;
    char curr_window_start = seq_num;

    size_t bytes_read;
    while (true){
        if (seq_num < curr_window_start + WINDOW_SIZE){
            // Likely will need lseek (or something of the sort, and use an iteration pointer - so we can retransmit missed data)
            bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - 1, file);
            // Set sequence number and send packet
            *seq_pointer = seq_num;
            send_packet(listen_sock, send_sock, buffer);

            seq_num += 1;
        } 
        else{
            // Handle waiting and processing of ACK/Timeout

            // Likely will need lseek (or something of the sort, and use an iteration pointer - so we can retransmit missed data)
        }

        // If final ACK, then we can wrap up
        if (false) {
            break;
        }
    }
}

void send_packet(int listen_sock, int send_sock, char* packet) {
}
