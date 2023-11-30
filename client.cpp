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
//void serve_local_file2(int listen_sock, int send_sock, FILE* filename);
void serve_local_file3(int listen_sock, int send_sock, FILE* filename);

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
    //serve_local_file(listen_sockfd, send_sockfd, fp, server_addr_to, client_addr);
    serve_local_file3(listen_sockfd,send_sockfd,fp);

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
    size_t bytes_read, ack_bytes_read;

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
    time_t timeout_start;
    unsigned int cwnd = 1, cwnd_frac = 0, ssh = START_SSTHRESH, dup_ack_count = 0;

    time_t logging_time;

    while (true){
        if (sent_seq_num < curr_window_start + cwnd){
            bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - HEADER_SIZE, file);   
            // If reached end of file, no transmission
            if (bytes_read > 0){
                // Set sequence number and send packet
                if (LOGGING_ENABLED) { 
                    printf("%ld | CLIENT: SEND NEW PACKET  | Sending seq #: %u, %u bytes\n", time(NULL), sent_seq_num, (bytes_read + HEADER_SIZE));
                    printf("%ld | CLIENT: WINDOW INFO      | window size is %u, starting at %u\n", time(NULL), cwnd, curr_window_start);
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
                // If the ACK message isn't requesting the start of the window, then we can move window forward
                if (*rec_buffer != (curr_window_start % max_window)){
                    if (LOGGING_ENABLED) { 
                        printf("%ld | CLIENT: ACKED PACKET     | received ack for seq #: %u, %u bytes\n", time(NULL), *rec_buffer, ack_bytes_read);
                    }
                    for (unsigned long i = curr_window_start; i <= curr_window_start + MAX_WINDOW_SIZE; i++){
                        if (i % max_window == *rec_buffer){
                            unsigned int packet_diff = i - curr_window_start;
                            curr_window_start = i;

                            // Handle exiting out of fast recovery
                            if (dup_ack_count >= DUP_ACK_LIMIT){
                                cwnd = ssh;
                                if (LOGGING_ENABLED) { 
                                    printf("%ld | CLIENT: FAST RETRANSMIT  | Updating window size to %u, starting at %u\n", time(NULL), cwnd, curr_window_start);
                                }
                            }
                            // Update window size if in slow start
                            else if (cwnd <= ssh && cwnd < MAX_WINDOW_SIZE){
                                cwnd += packet_diff;
                                if (LOGGING_ENABLED) { 
                                    printf("%ld | CLIENT: SLOW START       | Updating window size to %u, starting at %u\n", time(NULL), cwnd, curr_window_start);
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
                                    printf("%ld | CLIENT: CONGESTION AVOID | Updating window size to %u, starting at %u\n", time(NULL), cwnd, curr_window_start);
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
                        printf("%ld | CLIENT: DUPLICATE ACK    | received ack for seq #: %u, %u bytes\n", time(NULL), *rec_buffer, ack_bytes_read);
                    }
                    // Entering fast retransmit 
                    if (dup_ack_count == DUP_ACK_LIMIT){
                        long int current_position = ftell(file);

                        // Reading conent to send
                        fseek(file, (curr_window_start - 1) * (PAYLOAD_SIZE - HEADER_SIZE), SEEK_SET);
                        bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - HEADER_SIZE, file);   
                        fseek(file, current_position, SEEK_SET);   

                        // Set sequence number and send packet
                        if (LOGGING_ENABLED) {
                            printf("%ld | CLIENT: FAST RETRANSMIT  | Resending seq #: %u, %u bytes\n", time(NULL), curr_window_start, (bytes_read + HEADER_SIZE));
                        }
                        *seq_pointer = curr_window_start % max_window;
                        sendto(send_sock, send_buffer, bytes_read + HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
                        timeout_start = time(nullptr);

                        // Updating window size
                        ssh = max(cwnd / 2, (unsigned int)2);
                        cwnd += ssh + 3;
                        if (LOGGING_ENABLED) {
                            printf("%ld | CLIENT: FAST RETRANSMIT  | Updating window size to %u, starting at %u\n", time(NULL), cwnd, curr_window_start);
                        }
                    }
                    // Fast recovery for duplicate ack
                    else if (dup_ack_count > DUP_ACK_LIMIT){
                        cwnd += 1;
                        if (LOGGING_ENABLED) {
                            printf("%ld | CLIENT: FAST RECOVERY    | Updating window size to %u, starting at %u\n", time(NULL), cwnd, curr_window_start);
                        }
                    }
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
            fseek(file, (curr_window_start - 1) * (PAYLOAD_SIZE - HEADER_SIZE), SEEK_SET);
            bytes_read = fread(payload_pointer, 1, PAYLOAD_SIZE - HEADER_SIZE, file);   
            fseek(file, current_position, SEEK_SET);   

            // Set sequence number and send packet
            if (LOGGING_ENABLED) {
                printf("%ld | CLIENT: PACKET TIMEOUT   | Resending seq #: %u, %u bytes \n", time(NULL), curr_window_start, (bytes_read + HEADER_SIZE));
            }
            *seq_pointer = curr_window_start % max_window;
            sendto(send_sock, send_buffer, bytes_read + HEADER_SIZE, 0, (struct sockaddr*)&send_addr, sizeof(send_addr));
            timeout_start = time(nullptr);

            // Update window size for timeout
            ssh = max(cwnd / 2, (unsigned int)2);
            cwnd = 1;
            if (LOGGING_ENABLED){
                printf("%ld | CLIENT: WINDOW INFO      | Upating window size to %u, starting at %u \n", time(NULL), cwnd, curr_window_start);
            }
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
        printf("%ld | CLIENT: CLOSING REQ      | Requesting closing #: %u, %u bytes \n", time(NULL), *seq_pointer, 1);
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
                printf("%ld | CLIENT: CLOSING REQ      | Requesting closing #: %u, %u bytes \n", time(NULL), *seq_pointer, 1);
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
                if (*rec_buffer == CLOSE_PACKET_NUM){
                    if (LOGGING_ENABLED) { 
                        char curr_seq_num = *rec_buffer;
                        printf("%ld | CLIENT: TERMINATING ACK  | closing connection", time(NULL));
                        
                    }
                    break;
                }
            }
        }
    }
}
/*
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
    int start_modded, diff; 
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

            start_modded = start%RECIEVE_WINDOW_SIZE;
            if(ack > start_modded|| (ack + MAX_WINDOW_SIZE) <= start_modded){//if not duplicate ack
                //case 1 : ack is greater than start means okay
                //case 2 : send 100 - 199, ack is 0 
                if(ack < (start%RECIEVE_WINDOW_SIZE)){
                    ack += RECIEVE_WINDOW_SIZE;
                }
                diff = ack - start_modded;                  
                start += diff;  

                // last_ack = ack; 
                
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
*/
void serve_local_file3(int listen_sock, int send_sock, FILE* filename){
    uint8_t start = 0, end = 0, cwnd = 1, cwnd_frac = 0, ssh = START_SSTHRESH;
    /*start, end : [0-199] current bytes, 
        (end - start + 1 <= MAX_WINDOW_SIZE) or ((RECIEVE_WINDOW_SIZE - start) + end + 1 <= MAX_WINDOW_SIZE)
        start : last unacked packet
        end : last sent packet
    */
    //cwnd : [2,100] current window size, this should always be <= 100 
    //ssh : ssthresh
    
    time_t last_sent_time = time(NULL);
    //time for timeout

    //buffer information
    char full_buffer[MAX_PACKET_SIZE];
    char *header_ptr = full_buffer; 
    char *body_ptr = full_buffer + HEADER_SIZE;
    //full buffer : the full message: 1B header with sequence number and 1199B body, also reused for acks
    //sequence number, message_start : pointers to where they start in our file
    
    uint8_t ack;
    uint last_ack_count;
    /*ack : next expected packet, should always be 'greater' than start and 'less than or equal' end + 1 (can loop to lower val)
        if ack is equal to start, this means it was dropped / repeat ack 
    */
    //last_ack_count : to count number of repeat acks, could be infinite, but should be less than uint under most circumstances
    
    bool fast_retrans = false;
    //fast_retrans : if we are in fast retrans
    
    //helpers
    uint8_t diff; 
    size_t bytes_proc;
    bool not_eof = true;
    
    /*
    unsigned long int filesize;

    //get filesize and return to beginning
    fseek(filename, 0, SEEK_END);
    if(ferror(filename)){perror("bad seek to end");exit(1);}

    filesize = ftell(filename);
    if(filesize == -1){perror("bad ftell");exit(1);}

    fseek(filename, 0, SEEK_SET);
    if(ferror(filename)){perror("bad seek to begin");exit(1);}
    */
    printf("start %u, end %u\n", start, end);
    *header_ptr = end; 
    bytes_proc = fread(body_ptr, 1, TEXT_SIZE, filename);
    if(ferror(filename)){perror("bad read");close(listen_sock);exit(1);}

    //send
    bytes_proc = send(send_sock, full_buffer, HEADER_SIZE+bytes_proc, 0);
    if(bytes_proc == -1){perror("bad resend");close(listen_sock);exit(1);}
    last_sent_time = time(NULL); 
    
    if(LOGGING_ENABLED){
        printf("sent %u\n", end);
    }

    not_eof = !feof(filename);
    //send first packet

    while(not_eof){    
        /*
        check if we have recieved an ack
        if we have check it is the correct one and modify window size, ssh, etc as necessary
            timeout will be done later down
        */
        bytes_proc = recv(listen_sock, &ack, HEADER_SIZE, MSG_DONTWAIT);
        if(bytes_proc > 0){
            printf("recieved ack %u\n", ack);
            if(ack == start){
                last_ack_count++;
                if(LOGGING_ENABLED){
                    printf("duplicate ack %u\n", ack);
                }
                if(last_ack_count == DUP_ACK_LIMIT){
                    fast_retrans = true;
                    ssh = max(2, cwnd/2);
                    cwnd = ssh + DUP_ACK_LIMIT;
                } 
                if(fast_retrans){
                    //update cwnd if in fast retrans
                    cwnd = min(cwnd + 1, RECIEVE_WINDOW_SIZE);
                    
                    /*if more dup acks, we will resend
                        NOTE: this only works if it always reads TEXT_SIZE amount
                    */
                    //TODO : does this work if we need to resend the last packet or would that result in a timeout
                    //TODO: rethink the best way determine if we need to resend packet, keeping in mind chance resend is also lost
                    if(last_ack_count%DUP_ACK_LIMIT == 0){
                        //go back to the dropped packet
                        diff = seqdiff(start, end);
                        if(fseek(filename, -TEXT_SIZE*(diff+1), SEEK_CUR)){perror("bad fseek backwards");close(listen_sock);exit(1);}
                        
                        //build header
                        *header_ptr = start;
                        bytes_proc = fread(body_ptr, 1, TEXT_SIZE, filename);
                        if(ferror(filename)){perror("bad reread");close(listen_sock);exit(1);}
                        
                        //resend
                        bytes_proc = send(send_sock, full_buffer, HEADER_SIZE+bytes_proc, 0);
                        if(bytes_proc == -1){perror("bad resend");close(listen_sock);exit(1);}
                        last_sent_time = time(NULL); 

                        //return to original place
                        if(fseek(filename, TEXT_SIZE*(diff), SEEK_CUR)){perror("bad fseek forwards");close(listen_sock);exit(1);}
                    }
                }
                
            }else if(inrange(start, ack-1,cwnd)){ 
                if(LOGGING_ENABLED){
                    printf("ack %u\n", ack);
                }
                //NOTE: doesn't take advantage in the case of timeout and delayed ack
                
                //ack must be within start, (end+1), we will check if it is in range to determine if this is a late ack
                diff = seqdiff(start, ack);
                start = (start + diff) % RECIEVE_WINDOW_SIZE; 
                //recover from fast retrans
                if(fast_retrans){
                    fast_retrans = false;
                    cwnd = ssh;
                }
                if(cwnd <= ssh){
                    if(cwnd + diff > ssh){
                        cwnd = ssh;
                        cwnd_frac = (cwnd +diff - ssh) % cwnd;
                    }else{
                        cwnd = min(cwnd+diff, MAX_WINDOW_SIZE);
                    }
                    
                }else{ //if cwnd > ssh
                    cwnd_frac = cwnd_frac + diff;
                    if(cwnd_frac > cwnd){
                        cwnd_frac = cwnd_frac%cwnd;
                        cwnd = min(cwnd+1, MAX_WINDOW_SIZE);
                    }
                }
            }
        }


        /*
        check if we can send a packet and send a packet
        if we are at the end of the file, we seek to where the last read is
        */
        if (inrange(start, end+1, cwnd)){
            //printf("sending packet: start %u, end %u, cwnd:%u\n", start, end, cwnd);

            //read from file
            printf("sending packet: start %u, end %u, cwnd:%u\n", start, end, cwnd);

            *header_ptr = end; 
            bytes_proc = fread(body_ptr, 1, TEXT_SIZE, filename);
            if(ferror(filename)){perror("bad read");close(listen_sock);exit(1);}

            //send
            bytes_proc = send(send_sock, full_buffer, HEADER_SIZE+bytes_proc, 0);
            if(bytes_proc == -1){perror("bad resend");close(listen_sock);exit(1);}
            last_sent_time = time(NULL); 
            
            if(LOGGING_ENABLED){
                printf("sent %u\n", end);
            }
            
            //if we have read to the end of the file
            if(feof(filename)){
                break;
            }
        }
        /*
            if timeout
        */
        if(difftime(last_sent_time, time(NULL)) > TIMEOUT){
            ssh = max(cwnd/2, START_SSTHRESH); //TODO: should this be start ssthresh or 2
            cwnd = 1;
            diff = seqdiff(start,end);
            if (fseek(filename, -TEXT_SIZE*(diff+1),SEEK_CUR)){perror("bad timeout fseek");exit(1);}
            end = start; 
            if(LOGGING_ENABLED){
                printf("time out, restart at %u\n",start);
            }
        }
    }

    /*
        we have reached end of file, need to wait and see if there are any final issues
    */
    uint8_t final_start = start;
    uint8_t final_end = end;
    size_t last_size = bytes_proc - HEADER_SIZE;
    bool recd_end = false;
    long start_to_end = -1*(last_size + (TEXT_SIZE * seqdiff(final_start, final_end)));

    char last_msg[2] = {(char)CLOSE_PACKET_NUM, final_end};

    bytes_proc = send(send_sock, last_msg, 2, 0);
    if(bytes_proc == -1){perror("bad first end send"); exit(1);}
    
    printf("%u, %u, %u, %u, %ld\n",final_start, final_end, seqdiff(final_start,final_end), last_size, start_to_end);
    printf("%u, %u, %u\n", TEXT_SIZE, seqdiff(final_start,final_end),TEXT_SIZE * seqdiff(final_start, final_end));
    if(fseek(filename, start_to_end, SEEK_END)){perror("bad first end seek"); exit(1);}

    while(true){
        bytes_proc = recv(listen_sock, &ack, HEADER_SIZE, MSG_DONTWAIT);
        if(bytes_proc>0){
            if(ack == CLOSE_PACKET_NUM){
                recd_end = true;
                continue;
            }
            
            if(inrange(final_start, ack, cwnd)){
                
                //seek to correct pos
                diff = seqdiff(final_start, ack);
                if(fseek(filename, TEXT_SIZE * diff, SEEK_CUR)){perror("bad fseek in final"); exit(1);}

                //build packet
                *header_ptr = ack;
                bytes_proc = fread(body_ptr, 1, TEXT_SIZE, filename);
                if(ferror(filename)){perror("bad ending reread");exit(1);}
                        
                //resend
                bytes_proc = send(send_sock, full_buffer, HEADER_SIZE+bytes_proc, 0);
                if(bytes_proc == -1){perror("bad ending send");exit(1);}
                last_sent_time = time(NULL); 

                if(!recd_end){
                    bytes_proc = send(send_sock, last_msg, 2, 0);
                    if(bytes_proc == -1){perror("bad final end send"); exit(1);}
                }

                if(fseek(filename, start_to_end, SEEK_END)){perror("bad final end seek"); exit(1);}
            
            }

            if(difftime(last_sent_time, time(NULL)) > TIMEOUT){

                //seek to correct pos
                diff = seqdiff(final_start, ack);
                if(fseek(filename, TEXT_SIZE * diff, SEEK_CUR)){perror("bad fseek in final"); exit(1);}

                //build packet
                *header_ptr = ack;
                bytes_proc = fread(body_ptr, 1, TEXT_SIZE, filename);
                if(ferror(filename)){perror("bad ending reread");exit(1);}
                        
                //resend
                bytes_proc = send(send_sock, full_buffer, HEADER_SIZE+bytes_proc, 0);
                if(bytes_proc == -1){perror("bad ending send");exit(1);}
                last_sent_time = time(NULL); 

                if(fseek(filename, start_to_end, SEEK_END)){perror("bad timeout end seek"); exit(1);}

            }
        }

    }
}

