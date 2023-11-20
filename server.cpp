#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <set> 

#include "utils.h"

#define STORAGE_SIZE 300
using namespace std; 

void rec_file2(int listen_sock, int send_sock, FILE* filename);
int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0;
    int recv_len;
    struct packet ack_pkt;

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

    rec_file2(listen_sockfd,  send_sockfd, fp);

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
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
    unsigned char next_packet = 0; //start at 0
    unsigned char* next_packet_addr= &next_packet; 

    
    while(true){
        //find next avilable index in storage to read message into
        while(occupied_storage[storage_index]){
            storage_index = (storage_index+1)%STORAGE_SIZE;
        }

        //read in file to storage
        bytes_processed = recv(listen_sock, storage[storage_index], MAX_PACKET_SIZE, 0);
            //Q : should we bother with error checking ? 
        //if(bytes_processed == -1){perror("bad read");close(listen_sock);exit(1); }

        //connect new storage to record if it is a new message
        in_packet_number = storage[storage_index][0];                                           //msg is 1 byte header of seq number
        if(stored_records[in_packet_number].msg_pointer == nullptr){                            // if have not recieved packet, build record
            occupied_storage[storage_index] = true;
            stored_records[in_packet_number].msg_pointer = storage[storage_index] + HEADER_SIZE; 
            stored_records[in_packet_number].index = storage_index;
            stored_records[in_packet_number].length = bytes_processed - HEADER_SIZE;                      

        }//no need for else, will correctly run next sections

        //read to file (if correct)
        while(stored_records[next_packet].msg_pointer != nullptr){
            storage_index = stored_records[next_packet].index;
            bytes_processed = fwrite(stored_records[next_packet].msg_pointer, 1, stored_records[next_packet].length, filename);
            //if(bytes_processed != stored_records[next_packet].length){perror("bad write");close(listen_sock);exit(1); }
            //if fwrite returns value not equal to .length, fail (not checking) 

            //mark everything as empty
            occupied_storage[storage_index] = false;
            stored_records[next_packet].msg_pointer = nullptr;

            //go to next packet
            next_packet = (next_packet+1)%RECIEVE_WINDOW_SIZE; 
        }

        //send ack message for next expected packet
        bytes_processed = send(send_sock, next_packet_addr, HEADER_SIZE, 0);
        //if(bytes_processed == -1){perror("bad ack");close(listen_sock);exit(1); }
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
