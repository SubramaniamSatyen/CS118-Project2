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

#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))

using namespace std;

void serve_local_file(int listen_sock, int send_sock, FILE* file, sockaddr_in send_addr, sockaddr_in rec_addr);
void serve_local_file2(int listen_sock, int send_sock, FILE* filename);

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
    serve_local_file(listen_sockfd, send_sockfd, fp, server_addr_to, client_addr);
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

void serve_local_file(int listen_sock, int send_sock, FILE* file, sockaddr_in send_addr, sockaddr_in rec_addr) {
    char send_buffer[PAYLOAD_SIZE];
    char* payload_pointer = send_buffer + 1;
    unsigned char* seq_pointer = (unsigned char*)send_buffer;
    unsigned char rec_buffer[PAYLOAD_SIZE];
    struct pollfd fds[1];
    fds[0].fd = listen_sock;
    fds[0].events = POLLIN;
    int max_window = RECIEVE_WINDOW_SIZE;


    socklen_t sock_addr_len = sizeof(rec_addr);

    // Initializing variables
    unsigned long sent_seq_num = 1;
    unsigned long curr_window_start = sent_seq_num;
    fseek(file, 0L, SEEK_END);
    int total_bytes = ftell(file); //unsigned int
    fseek(file, 0L, SEEK_SET);
        

    size_t bytes_read, ack_bytes_read;
    time_t timeout_start;
    while (true){
        if (sent_seq_num < curr_window_start + WINDOW_SIZE){
            bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - HEADER_SIZE, file);   
            // If reached end of file, no transmission
            if (bytes_read > 0){
                // Set sequence number and send packet
                if (LOGGING_ENABLED) { 
                    printf("CLIENT LOGGING: SEND | Sending seq #: %u, %u bytes \n", sent_seq_num, (bytes_read + HEADER_SIZE));
                }
                *seq_pointer = sent_seq_num % max_window;
                sendto(send_sock, send_buffer, bytes_read + HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                sent_seq_num += 1;
                timeout_start = time(nullptr);
            }
        } 
        
        int listen_results = poll(fds, 1, 100);
        if (listen_results > 0 && fds[0].revents & POLLIN){
            ack_bytes_read = recvfrom(listen_sock, rec_buffer, PAYLOAD_SIZE, MSG_DONTWAIT, (struct sockaddr*)&rec_addr, &sock_addr_len);
            int read_error = errno;
            // Handle processing of ACK
            if (ack_bytes_read > 0 && read_error != EWOULDBLOCK){
                if (LOGGING_ENABLED) { 
                    printf("CLIENT LOGGING: ACKED | received ack for seq #: %u, %u bytes \n", *rec_buffer, ack_bytes_read);
                }
                // If the ACK message isn't requesting the start of the window, then we can move window forward
                if (*rec_buffer != curr_window_start){
                    curr_window_start = *rec_buffer;
                }
                else {
                    // TODO: Any logic for handling triple ACKS, etc. (think we can rely on timeouts for initial implementation)
                }
            }
            else if (read_error == EWOULDBLOCK && ack_bytes_read <= 0) {
                // TODO: Handle error with read
            }
        }

        // Handle timeouts
        if (difftime(TIMEOUT + timeout_start, time(nullptr)) < 0) {
            // Storing current position
            long int current_position = ftell(file);

            // Reading conent to send
            fseek(file, curr_window_start - 1, SEEK_SET);
            bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - HEADER_SIZE, file);   
            fseek(file, current_position, SEEK_SET);   

            // Set sequence number and send packet
            if (LOGGING_ENABLED) {
                printf("CLIENT LOGGING: TIMEOUT | Resending seq #: %u, %u bytes \n", curr_window_start, (bytes_read + HEADER_SIZE));
            }
            *seq_pointer = curr_window_start % max_window;
            sendto(send_sock, send_buffer, bytes_read + HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
            timeout_start = time(nullptr);
        }

        // If we've finished transmitting and do not have any further ACKS to receive, finish up
        if (bytes_read == 0 && curr_window_start == sent_seq_num) {
            break;
        }
    }

    // Sending notifications to close connection
    int attempts = 0;
    *seq_pointer = CLOSE_PACKET_NUM;

    if (LOGGING_ENABLED) {
        printf("CLIENT LOGGING: CLOSING REQ | Requesting closing #: %u, %u bytes \n", *seq_pointer, 1);
    }
    sendto(send_sock, send_buffer, 1, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
    attempts += 1;
    timeout_start = time(nullptr);
    while (true) {
        // Resend closing message
        if (difftime(TIMEOUT + timeout_start, time(nullptr)) < 0){
            sendto(send_sock, send_buffer, HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
            timeout_start = time(nullptr);
            if (LOGGING_ENABLED) {
                printf("CLIENT LOGGING: CLOSING REQ | Requesting closing #: %u, %u bytes \n", *seq_pointer, 1);
            }
            attempts += 1;
            if (attempts > 5){
                break;
            }
        }
        
        int listen_results = poll(fds, 1, 100);
        if (listen_results > 0 && fds[0].revents & POLLIN){
            ack_bytes_read = recvfrom(listen_sock, rec_buffer, PAYLOAD_SIZE, MSG_DONTWAIT, (struct sockaddr*)&rec_addr, &sock_addr_len);
            int read_error = errno;
            // Handle processing of ACK
            if (ack_bytes_read > 0 && read_error != EWOULDBLOCK){
                if (LOGGING_ENABLED) {
                    printf("CLIENT LOGGING: CLOSING REQ RECEIVED | Requesting closing #: %u\n", *rec_buffer);
                }
                if (*rec_buffer == CLOSE_PACKET_NUM){
                    if (LOGGING_ENABLED) { 
                        char curr_seq_num = *rec_buffer;
                        printf("CLIENT LOGGING: TERMINATING ACK | closing connection");
                    }
                    break;
                }
            }
        }
    }
}

void serve_local_file2(int listen_sock, int send_sock, FILE* filename){
    unsigned int start = 0, end = 0, cwnd = 1, ssh = START_SSTHRESH; 
    //start is the packet expected to be acked
    //end is the packet most recently sent
    //cwnd is window size: send packets till end > start + cwnd
    //ssh is for exp start

    unsigned int cwnd_frac = 0; // to store fraction of cwnd during congestion avoidance
    time_t last_sent_time = time(NULL); 
    
    //128 max window size,  range for seq / ack , 255 for end ? 
    // for turn off ? 
    
    //buffer stuff
    char full_buffer[MAX_PACKET_SIZE];
    char* seq_num = full_buffer; 
    char* message_start = full_buffer + HEADER_SIZE; 
    size_t message_size; 

    //ack
    //reuse message buffer
    char ack; 
    char last_ack = 0;
    int last_ack_cnt = 0; 
    bool timedout = false; 
    bool packet_lost = true; 


    while (true){
        //listen for packet
        message_size = recv(listen_sock, full_buffer, 1, MSG_DONTWAIT);
        if(message_size > 0){ //if we have received an ack
            //depends on message size
            ack = full_buffer[0];

            if(ack > start){//if not duplicate ack
                //fix the condition to work properly with wrapping
                //working on ack sending next expected byte
                int diff = ack - start;                 
                start += diff;  

                last_ack = ack; 
                
                if (cwnd <= ssh){//slow start
                    cwnd += diff;
                }else{//congestion avoidance
                    cwnd_frac+= diff;
                    if (cwnd_frac >= cwnd){
                        cwnd_frac %= cwnd;
                        cwnd++; 
                    }
                }

                timedout = false;
                packet_lost = false; 

            }else if(ack == start) {//if duplicate ack
                //change to work with the new ack/start
                
                if(++last_ack_cnt == 3){
                    ssh = max(2, int(cwnd/2));
                    cwnd = ssh + 3;
                    packet_lost=true; 
                }
                if(last_ack_cnt > 3){
                    cwnd++;
                }
            }
        }
        
        //if we can send more packets
        if(timedout){

        // }else if (packet_lost){ // implement in the listen section
            
        }
        else if (end <= start + cwnd){
            
            //slow start
            if(cwnd <= ssh){

            }else{

            }

        }

        if (end <= start + cwnd){
        }
        //if timeout
        //difftime https://cplusplus.com/reference/ctime/difftime/
        //returns double of time in seconds, current timeout is 2 seconds
        timedout = difftime(last_sent_time, time(NULL)) > TIMEOUT; 

        
    }
}