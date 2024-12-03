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
#include <sys/time.h>
#include <pthread.h>
#include "wrappers.h"
#include "message.h"

#define IPSTRLEN 50
#define MAXFACTORIES 20

typedef struct sockaddr SA;

typedef struct {
    int facID;
    int capacity;
    int duration;
} FactoryData;

// Global variables
int activeThreads = 0;
int totalPartsPerFactory[MAXFACTORIES];
int iterationsPerFactory[MAXFACTORIES];
pthread_mutex_t orderMutex = PTHREAD_MUTEX_INITIALIZER;
int sd;
struct sockaddr_in srvrSkt, clntSkt;
char *myName;

void* subFactory(void* arg) {
    FactoryData* data = (FactoryData*)arg;
    int partsImade = 0, myIterations = 0;
    
    printf("Created Factory Thread # %d with capacity = %2d parts & duration = %4d mSec\n",
           data->facID, data->capacity, data->duration);
    
    while (1) {
        pthread_mutex_lock(&orderMutex);
        if (activeThreads <= 0) {
            pthread_mutex_unlock(&orderMutex);
            break;
        }
        int toMake = (activeThreads < data->capacity) ? activeThreads : data->capacity;
        activeThreads -= toMake;
        pthread_mutex_unlock(&orderMutex);

        partsImade += toMake;
        myIterations++;

        printf("Factory (%s), # %d: Going to make    %2d parts in %4d mSec\n",
               myName, data->facID, toMake, data->duration);

        msgBuf msg;
        msg.purpose = htonl(PRODUCTION_MSG);
        msg.facID = htonl(data->facID);
        msg.capacity = htonl(data->capacity);
        msg.partsMade = htonl(toMake);
        msg.duration = htonl(data->duration);

        sendto(sd, &msg, sizeof(msg), 0, (SA*)&clntSkt, sizeof(clntSkt));
        usleep(data->duration * 1000);
    }

    msgBuf msg;
    msg.purpose = htonl(COMPLETION_MSG);
    msg.facID = htonl(data->facID);
    msg.partsMade = htonl(partsImade);

    sendto(sd, &msg, sizeof(msg), 0, (SA*)&clntSkt, sizeof(clntSkt));

    printf(">>> Factory # %d : Terminating after making total of %d parts in %d iterations\n",
           data->facID, partsImade, myIterations);
           
    totalPartsPerFactory[data->facID - 1] = partsImade;
    iterationsPerFactory[data->facID - 1] = myIterations;
    
    free(data);
    pthread_exit(NULL);
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
    myName = "Joshua Cassada and Thomas Cantrell";
    unsigned short port = 5000;
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

        activeThreads = ntohl(msg.orderSize);
        
        struct timeval startTime;
        gettimeofday(&startTime, NULL);
        
        msg.purpose = htonl(ORDR_CONFIRM);
        msg.numFac = htonl(N);
        sendto(sd, &msg, sizeof(msg), 0, (SA*)&clntSkt, client_len);
        
        printf("\nFACTORY ( by %s ) sent this Order Confirmation to the client { ORDR_CNFRM , numFacThrds=%d }\n\n",
               myName, N);

        pthread_t threads[MAXFACTORIES];
        for (int i = 0; i < N; i++) {
            FactoryData* data = malloc(sizeof(FactoryData));
            data->facID = i + 1;
            data->capacity = 10 + (rand() % 41);
            data->duration = 500 + (rand() % 701);
            
            Pthread_create(&threads[i], NULL, subFactory, data);
        }

        for (int i = 0; i < N; i++) {
            Pthread_join(threads[i], NULL);
        }
        
        struct timeval endTime;
        gettimeofday(&endTime, NULL);
        double elapsedMS = (endTime.tv_sec - startTime.tv_sec) * 1000.0 +
                          (endTime.tv_usec - startTime.tv_usec) / 1000.0;

        printf("\n****** FACTORY Server ( by %s ) Summary Report *******\n", myName);
        printf("Sub-Factory      Parts Made      Iterations\n");

        int grandTotal = 0;
        for (int i = 0; i < N; i++) {
            printf("     %d             %2d              %d\n", 
                   i + 1, totalPartsPerFactory[i], iterationsPerFactory[i]);
            grandTotal += totalPartsPerFactory[i];
        }
        printf("============================================\n");
        printf("Grand total parts made  =  %d  vs  order size of   %d\n\n", 
               grandTotal, ntohl(msg.orderSize));
        printf("Order-to-Completion time = %.1f milliseconds\n\n", elapsedMS);
    }
    
    return 0;
}