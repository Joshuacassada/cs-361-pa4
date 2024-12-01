//---------------------------------------------------------------------
// Assignment : PA-04 Multi-Threaded UDP Server
// Author     : [Your names here]
// File Name  : factory.c
//---------------------------------------------------------------------

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include "wrappers.h"
#include "message.h"

#define IPSTRLEN 50

typedef struct sockaddr SA;

typedef struct {
    int facID;
    int capacity;
    int duration;
} FactoryData;

// Global variables
int remainsToMake = 0;
pthread_mutex_t orderMutex = PTHREAD_MUTEX_INITIALIZER;
int sd;
struct sockaddr_in srvrSkt, clntSkt;
time_t startTime;

void* subFactory(void* arg) {
    FactoryData* data = (FactoryData*)arg;
    int partsImade = 0, myIterations = 0;
    
    printf("Created Factory Thread # %d with capacity = %d parts & duration = %d mSec\n",
           data->facID, data->capacity, data->duration);
    
    while (1) {
        pthread_mutex_lock(&orderMutex);
        if (remainsToMake <= 0) {
            pthread_mutex_unlock(&orderMutex);
            break;
        }
        int toMake = (remainsToMake < data->capacity) ? remainsToMake : data->capacity;
        remainsToMake -= toMake;
        pthread_mutex_unlock(&orderMutex);

        partsImade += toMake;
        myIterations++;

        printf("Factory (by Joshua Cassada and Thomas Cantrell), # %d Going to make    %d parts in %d mSec\n",
               data->facID, toMake, data->duration);

        msgBuf msg;
        msg.purpose = htonl(PRODUCTION_MSG);
        msg.facID = htonl(data->facID);
        msg.capacity = htonl(data->capacity);
        msg.partsMade = htonl(toMake);
        msg.duration = htonl(data->duration);

        sendto(sd, &msg, sizeof(msg), 0, (SA*)&clntSkt, sizeof(clntSkt));
        Usleep(data->duration * 1000);
    }

    msgBuf msg;
    msg.purpose = htonl(COMPLETION_MSG);
    msg.facID = htonl(data->facID);
    msg.partsMade = htonl(partsImade);

    sendto(sd, &msg, sizeof(msg), 0, (SA*)&clntSkt, sizeof(clntSkt));

    printf(">>> Factory # %d : Terminating after making total of %d parts in %d iterations\n",
           data->facID, partsImade, myIterations);
           
    free(data);
    return NULL;
}

void goodbye(int sig) {
    printf("\n### Server (%d) terminating. Goodbye!\n\n", getpid());
    msgBuf msg;
    msg.purpose = htonl(PROTOCOL_ERR);
    sendto(sd, &msg, sizeof(msg), 0, (SA*)&clntSkt, sizeof(clntSkt));
    close(sd);
    exit(0);
}

int main(int argc, char *argv[]) {
    char *myName = "Joshua Cassada and Thomas Cantrell";
    unsigned short port = 5000;  // Default port is now 5000
    int N = 1;
    
    printf("\nThis is the FACTORY server ( by %s )\n\n", myName);
    printf("I will attempt to accept orders at port %d and use %d sub-factories.\n\n", port, N);

    switch (argc) {
        case 1: break;
        case 2: N = atoi(argv[1]); break;
        case 3: 
            N = atoi(argv[1]);
            port = atoi(argv[2]);
            break;
        default:
            printf("Usage: %s [numThreads] [port]\n", argv[0]);
            exit(1);
    }

    sd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&srvrSkt, 0, sizeof(srvrSkt));
    srvrSkt.sin_family = AF_INET;
    srvrSkt.sin_addr.s_addr = htonl(INADDR_ANY);
    srvrSkt.sin_port = htons(port);
    
    if (bind(sd, (SA*)&srvrSkt, sizeof(srvrSkt)) < 0)
        err_sys("bind failed");
    
    printf("Bound socket %d to IP 0.0.0.0 Port %d\n\n", sd, port);
    
    sigactionWrapper(SIGINT, goodbye);
    sigactionWrapper(SIGTERM, goodbye);

    while (1) {
        printf("FACTORY server ( by %s ) waiting for Order Requests\n", myName);
        
        msgBuf msg;
        socklen_t client_len = sizeof(clntSkt);
        recvfrom(sd, &msg, sizeof(msg), 0, (SA*)&clntSkt, &client_len);
        
        char ipStr[IPSTRLEN];
        inet_ntop(AF_INET, &clntSkt.sin_addr, ipStr, IPSTRLEN);
        printf("\nFACTORY server ( by %s ) received: { REQUEST , OrderSize=%d }\n", 
               myName, ntohl(msg.orderSize));
        printf("        From IP %s Port %d\n", ipStr, ntohs(clntSkt.sin_port));

        remainsToMake = ntohl(msg.orderSize);
        startTime = time(NULL);
        
        printf("\nFACTORY ( by %s ) sent this Order Confirmation to the client { ORDR_CNFRM , numFacThrds=%d }\n\n",
               myName, N);
               
        // Send order confirmation
        msg.purpose = htonl(ORDR_CONFIRM);
        msg.numFac = htonl(N);
        sendto(sd, &msg, sizeof(msg), 0, (SA*)&clntSkt, client_len);

        pthread_t threads[20];
        for (int i = 0; i < N; i++) {
            FactoryData* data = malloc(sizeof(FactoryData));
            data->facID = i + 1;
            data->capacity = 10 + (rand() % 41);  // Random 10-50
            data->duration = 500 + (rand() % 701); // Random 500-1200
            
            Pthread_create(&threads[i], NULL, subFactory, data);
        }

        for (int i = 0; i < N; i++) {
            Pthread_join(threads[i], NULL);
        }
        
        time_t endTime = time(NULL);
        printf("\n****** FACTORY ( by %s ) Summary Report ******\n", myName);
        // The summary will be printed by each thread as they complete
        printf("Order-to-Completion time = %.1f milliseconds\n\n", 
               (endTime - startTime) * 1000.0);
    }
    
    return 0;
}