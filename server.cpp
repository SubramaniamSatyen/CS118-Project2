#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <set> 

#include "utils.h"
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
    char storage[300][MAX_PACKET_SIZE] = {0}; //random access storage for packets, out of order, index stored in pointers
    char* pointers[200] = {0};  //pointers to the corresponding message, index is sequence number
    int next_package;
    set<int> index; //currently inuse storage indexes 
    int i=0; 
    //receve msg


        while i ++ % 300 not in set continue; 

        read(storage[i])
        sequence number = storage[0];
        if !pointer[sequencenumber]{ //only do this if 
            pointer[sequence number ]= i
            set.add(i)
            do {
                write(pointers[sequencenumber])
                pointers[sequencenumber] = nullptr
                set.delete(i)
                seq = (seq+1)%200; 
            }while(pointer[seq] != nullptr)
        }
}
