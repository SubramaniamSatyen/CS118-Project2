#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <set> 
#include <ctime>
#include <sys/time.h>
#include <poll.h>
#include <algorithm>
#include "utils.h"

#define STORAGE_SIZE 300
using namespace std; 

void rec_file2(int listen_sock, int send_sock, FILE* filename);
void rec_file(int listen_sock, int send_sock, FILE* filename, sockaddr_in send_addr, sockaddr_in rec_addr);

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_to;

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");

    // TODO: Receive file from the client and save it as output.txt
    rec_file(listen_sockfd,  send_sockfd, fp, client_addr_to, server_addr);
    // rec_file2(listen_sockfd,  send_sockfd, fp);

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

void rec_file(int listen_sock, int send_sock, FILE* filename, sockaddr_in send_addr, sockaddr_in rec_addr){
    char storage[STORAGE_SIZE][MAX_PACKET_SIZE];
    struct record {
        char* packet;      //pointer for convienence and easy way to check if valid
        int length;  //length of message (packet size - 1)
    };
    unsigned long seq_num_to_write = 1;
    socklen_t sock_addr_len = sizeof(rec_addr);
    int max_window = RECIEVE_WINDOW_SIZE;

    record stored_records[RECIEVE_WINDOW_SIZE] = {nullptr, 0}; //struct stores pointer in storage and the length of the read in file
    int next_free_buffer = 0, bytes_read = 0, bytes_written = 0, bytes_acked = 0;

    // Polling variables
    struct pollfd fds[1];
    fds[0].fd = listen_sock;
    fds[0].events = POLLIN;

    struct timeval tval_logging;
    struct timeval timeout_start, curr, add, cmp;
    add.tv_sec = (int)(TIMEOUT * CLOSE_MULTI);
    add.tv_usec = (TIMEOUT * CLOSE_MULTI) - (int)(TIMEOUT * CLOSE_MULTI);

    while (true){
        // Read next message 
        bytes_read = recvfrom(listen_sock, storage[next_free_buffer], MAX_PACKET_SIZE, 0, (struct sockaddr*)&rec_addr, &sock_addr_len);

        // If message recieved and haven't recieved the seq_num yet, then store it, and move to next read buffer
        if (bytes_read > 0){
            unsigned char curr_seq_num = *(storage[next_free_buffer]);
            if (LOGGING_ENABLED) { 
                printf("SERVER LOGGING: RECEIVED | received seq #: %u, read %u bytes\n", curr_seq_num, bytes_read);
            }

            if (curr_seq_num == CLOSE_PACKET_NUM) {
                // Send initial ACK
                unsigned char ack_num = CLOSE_PACKET_NUM;
                bytes_acked = sendto(send_sock, &ack_num, 1, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                if (LOGGING_ENABLED) { 
                    gettimeofday(&tval_logging, NULL);
                    printf("%ld.%06ld | SERVER LOGGING: CLOSE ACK | sent closing ack #: %u, read %u bytes\n", tval_logging.tv_sec, tval_logging.tv_usec, ack_num, bytes_acked);
                }
                gettimeofday(&timeout_start, NULL);
                // Closing state
                while (true){
                    // If haven't heard back from client in a couple timeouts, close server
                    gettimeofday(&curr, NULL);
                    timeradd(&timeout_start, &add, &cmp);
                    // Handle timeouts
                    if (timercmp(&cmp, &curr, <)) {
                        if (LOGGING_ENABLED) { 
                            gettimeofday(&tval_logging, NULL);
                            printf("%ld.%06ld | SERVER LOGGING: CLOSE CON | \n", tval_logging.tv_sec, tval_logging.tv_usec);
                        }
                        return;

                    // Poll for client retransmit of close request (meaning didn't get ACK) and resend ACK
                    } else {
                        int listen_results = poll(fds, 1, 10);
                        if (listen_results > 0 && fds[0].revents & POLLIN){
                            bytes_read = recvfrom(listen_sock, storage[next_free_buffer], MAX_PACKET_SIZE, 0, (struct sockaddr*)&rec_addr, &sock_addr_len);
                            curr_seq_num = *(storage[next_free_buffer]);
                            if (curr_seq_num == CLOSE_PACKET_NUM) {
                                if (LOGGING_ENABLED) { 
                                    gettimeofday(&tval_logging, NULL);
                                    printf("%ld.%06ld | SERVER LOGGING: CLOSE ACK | sent closing ack #: %u, read %u bytes\n", tval_logging.tv_sec, tval_logging.tv_usec, ack_num, bytes_acked);
                                }
                                bytes_acked = sendto(send_sock, &ack_num, 1, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                                gettimeofday(&timeout_start, NULL);
                            }
                        }
                    }
                }
            }
            // If packet not able to fit in receiver window, then don't buffer it (ex: duplicate of already written packet)
            bool bufferable_seq_num = false;
            for (unsigned long i = seq_num_to_write; i < seq_num_to_write + MAX_WINDOW_SIZE; i++){
                if (i % max_window == curr_seq_num) {
                    bufferable_seq_num = true;
                    break;
                }
            }

            if (stored_records[curr_seq_num].packet == nullptr && bufferable_seq_num) {
                stored_records[curr_seq_num] = { storage[next_free_buffer] + HEADER_SIZE, bytes_read - HEADER_SIZE };
                next_free_buffer = (next_free_buffer + 1) % STORAGE_SIZE;
            }
            // If received duplicate packet in window update packet info
            // else if (stored_records[curr_seq_num].packet != nullptr && bufferable_seq_num){
            //     printf("SERVER LOGGING: DUPLICATE PACKET | replacing buffered %u bytes from seq #: %u \n", bytes_read, curr_seq_num);
            //     memcpy(stored_records[curr_seq_num].packet, storage[next_free_buffer] + HEADER_SIZE, bytes_read - HEADER_SIZE);
            //     stored_records[curr_seq_num].length = bytes_read - HEADER_SIZE;
            // }
        }

        // Write out any buffers that can be written to file (in order from next expected)
        while (stored_records[seq_num_to_write].packet != nullptr && stored_records[seq_num_to_write].length != 0) {
            // TODO: Remove block below (to write seq num before each packet)
            // char temp[100] = "\n\nsequence num: ";
            // char str[10 + sizeof(char)]; 
            // sprintf(str, "%ld", seq_num_to_write); 
            // strcat(temp, str);
            // strcat(temp, "\n\n");
            // bytes_written = fwrite(temp, 1, strlen(temp), filename);


            bytes_written = fwrite(stored_records[seq_num_to_write].packet, 1, stored_records[seq_num_to_write].length, filename);
            if (LOGGING_ENABLED) { 
                printf("SERVER LOGGING: WRITE | writing %u bytes from seq #: %u \n", bytes_written, seq_num_to_write);
            }

            // TODO: Handle case of bytes_written = 0 or not equal length...
            stored_records[seq_num_to_write] = { nullptr, 0 };
            seq_num_to_write = (seq_num_to_write + 1) % max_window;
        }

        // Send ACK for latest received message
        short ack_num = seq_num_to_write % max_window;
        bytes_acked = sendto(send_sock, &ack_num, 1, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
        if (LOGGING_ENABLED) { 
            printf("SERVER LOGGING: SENT ACK | sent ack for seq #: %u, %u bytes \n", ack_num, bytes_acked);
        }
    }
}

void rec_file2(int listen_sock, int send_sock, FILE* filename){
    //storing packets
    char storage[STORAGE_SIZE][MAX_PACKET_SIZE]; //random access storage for packets, out of order, index stored in stored_records
    struct record{
        char* msg_pointer;      //pointer for convienence and easy way to check if valid
        unsigned short index;   //index in storage
        unsigned short length;  //length of message (packet size - 1)

    } stored_records[RECIEVE_WINDOW_SIZE] = {nullptr, 0, 0}; //struct stores pointer in storage and the length of the read in file
    bool occupied_storage[STORAGE_SIZE] = {false}; //to keep track of what indexes are in use, true means in use
    unsigned short storage_index=0; 
    //unsigned char stored_records[RECIEVE_WINDOW_SIZE] = {0}; //index is sequence number, value is index in storage 
    
    //for reading in packets
    int bytes_processed;
    unsigned char in_packet_number;    

    //for writing into file, sending ack
    unsigned char next_packet = 0; //start at 0, next packet expected
    unsigned char* next_packet_addr= &next_packet; 

    
    while(true){
        //find next avilable index in storage to read message into
        while(occupied_storage[storage_index]){
            storage_index = (storage_index+1)%STORAGE_SIZE;
        }

        //read in file to storage
        bytes_processed = recv(listen_sock, storage[storage_index], MAX_PACKET_SIZE, 0);
        if(bytes_processed == -1){perror("bad read");close(listen_sock);exit(1); }

        //connect new storage to record if it is a new message
        in_packet_number = storage[storage_index][0];                                           //msg is 1 byte header of seq number
        if(stored_records[in_packet_number].msg_pointer == nullptr){                            // if have not recieved packet, build record
            occupied_storage[storage_index] = true;
            stored_records[in_packet_number].msg_pointer = storage[storage_index] + HEADER_SIZE; 
            stored_records[in_packet_number].index = storage_index;
            stored_records[in_packet_number].length = bytes_processed - HEADER_SIZE;     
            if(LOGGING_ENABLED){
                printf("recieved packet %u, length %u\n", in_packet_number, bytes_processed);
            }                 

        }//no need for else, will correctly run next sections

        //read to file (if correct)
        while(stored_records[next_packet].msg_pointer != nullptr){
            storage_index = stored_records[next_packet].index;
            bytes_processed = fwrite(stored_records[next_packet].msg_pointer, 1, stored_records[next_packet].length, filename);
            if(bytes_processed != stored_records[next_packet].length){perror("bad write");close(listen_sock);exit(1); }
            //if fwrite returns value not equal to .length, fail  

            //mark everything as empty
            occupied_storage[storage_index] = false;
            stored_records[next_packet].msg_pointer = nullptr;

            //go to next packet
            next_packet = (next_packet+1)%RECIEVE_WINDOW_SIZE; 
        }

        //send ack message for next expected packet
        bytes_processed = send(send_sock, next_packet_addr, HEADER_SIZE, 0);
        if(LOGGING_ENABLED){
            printf("sent ack %u\n", next_packet_addr);
        }
        if(bytes_processed == -1){perror("bad ack");close(listen_sock);exit(1); }
    }
}


    // while i ++ % 300 not in set continue; 

    // read(storage[i])
    // sequence number = storage[0];
    // if !pointer[sequencenumber]{ //only do this if 
    //     pointer[sequence number ]= i
    //     set.add(i)
    //     do {
    //         write(pointers[sequencenumber])
    //         pointers[sequencenumber] = nullptr
    //         set.delete(i)
    //         seq = (seq+1)%200; 
    //     }while(pointer[seq] != nullptr)
    // }
