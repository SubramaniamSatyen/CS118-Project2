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
#define WINDOW_SIZE 20
#define TIMEOUT 0.22               // Determine this from fixed prop delay of 1 ms

//Our macros
#define MAX_PACKET_SIZE 1200
#define HEADER_SIZE 1
#define TEXT_SIZE (MAX_PACKET_SIZE-HEADER_SIZE)
#define START_SSTHRESH 15
#define DUP_ACK_LIMIT 3
#define MAX_WINDOW_SIZE 100
#define RECIEVE_WINDOW_SIZE (2*MAX_WINDOW_SIZE)
#define CLOSE_PACKET_NUM 255
#define CLOSE_MULTI 2

#define LOGGING_ENABLED false

#endif