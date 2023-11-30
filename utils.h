#ifndef UTILS_H
#define UTILS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MACROS
#define SERVER_IP "127.0.0.1"
#define LOCAL_HOST "127.0.0.1"
#define SERVER_PORT_TO 5002
#define CLIENT_PORT 6001
#define SERVER_PORT 6002
#define CLIENT_PORT_TO 5001
#define PAYLOAD_SIZE 1024
#define WINDOW_SIZE 5
#define TIMEOUT 2
#define MAX_SEQUENCE 1024

//Our macros
#define MAX_PACKET_SIZE 1200
#define HEADER_SIZE 1
#define TEXT_SIZE (MAX_PACKET_SIZE-HEADER_SIZE)
#define START_SSTHRESH 4
#define DUP_ACK_LIMIT 3
#define MAX_WINDOW_SIZE 100
#define RECIEVE_WINDOW_SIZE (2*MAX_WINDOW_SIZE)
#define CLOSE_PACKET_NUM 255

#define LOGGING_ENABLED true


// Packet Layout
// You may change this if you want to
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char ack;
    char last;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

// Utility function to build a packet
void build_packet(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char last, char ack,unsigned int length, const char* payload) {
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->ack = ack;
    pkt->last = last;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// Utility function to print a packet
void printRecv(struct packet* pkt) {
    printf("RECV %d %d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", (pkt->ack) ? " ACK": "");
}

void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
}

//returns the number of packets between end and start
uint8_t seqdiff(uint8_t start, uint8_t end){
    if (end < start){end += RECIEVE_WINDOW_SIZE;}
    return end - start; 
}

bool inrange(uint8_t start, uint8_t index, uint8_t cwnd){
    if (index < start){
        index += RECIEVE_WINDOW_SIZE;
    }
    return cwnd > (index - start);
}

bool isEnd(uint8_t seq, uint8_t final){
    switch (final){
        case CLOSE_PACKET_NUM:
            return false;
        default: 
            return (seq-1) == final; 
    }
}
#endif