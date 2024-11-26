//---------------------------------------------------------------------
// Assignment : PA-03 UDP Single-Threaded Server
// Date       :
// Author     : Joshua Cassada and Thomas Cantrell
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
#include <sys/stat.h>
#include <sys/wait.h>

#include "wrappers.h"
#include "message.h"

#define MAXSTR     200
#define IPSTRLEN    50

typedef struct sockaddr SA;

int minimum(int a, int b)
{
    return (a <= b ? a : b);
}

void subFactory(int factoryID, int myCapacity, int myDuration);

void factLog(char *str)
{
    printf("%s", str);
    fflush(stdout);
}

// Global Variables
int remainsToMake,      // Must be protected by a Mutex
    actuallyMade;       // Actually manufactured items
int numActiveFactories = 1, orderSize;
int sd;                 // Server socket descriptor
struct sockaddr_in srvrSkt,    // Server's address
                  clntSkt;     // Client's address

//------------------------------------------------------------
//  Handle Ctrl-C or KILL 
//------------------------------------------------------------
void goodbye(int sig) 
{
    printf("\n### I (%d) have been nicely asked to TERMINATE. goodbye\n\n", getpid());
    msgBuf errorMsg;
    errorMsg.purpose = htonl(PROTOCOL_ERR);
    sendto(sd, &errorMsg, sizeof(errorMsg), 0, (SA *) &clntSkt, sizeof(clntSkt));
    close(sd);
    exit(0);
}

/*-------------------------------------------------------*/
int main(int argc, char *argv[])
{
    char *myName = "Joshua Cassada and Thomas Cantrell";
    unsigned short port = 50015;    // Default port
    int N = 1;                      // Default number of factories

    printf("\nThis is the FACTORY server developed by %s\n\n", myName);
    
    char myUserName[30];
    char ipStr[IPSTRLEN];
    getlogin_r(myUserName, 30);
    time_t now;
    time(&now);
    fprintf(stdout, "Logged in as user '%s' on %s\n\n", myUserName, ctime(&now));
    fflush(stdout);

    // Process command line arguments
    switch (argc) 
    {
        case 1:
            break;
        case 2:
            N = atoi(argv[1]);
            break;
        case 3:
            N = atoi(argv[1]);
            port = atoi(argv[2]);
            break;
        default:
            printf("FACTORY Usage: %s [numThreads] [port]\n", argv[0]);
            exit(1);
    }

    // Create and configure server socket
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    memset((void *)&srvrSkt, 0, sizeof(srvrSkt));
    srvrSkt.sin_family = AF_INET;
    srvrSkt.sin_addr.s_addr = htonl(INADDR_ANY);
    srvrSkt.sin_port = htons(port);

    // Bind socket
    if (bind(sd, (const struct sockaddr *)&srvrSkt, sizeof(srvrSkt)) < 0) {
        perror("bind failed");
        close(sd);
        exit(EXIT_FAILURE);
    }

    inet_ntop(AF_INET, &srvrSkt.sin_addr.s_addr, ipStr, IPSTRLEN);
    printf("Bound socket %d to IP %s Port %d\n", sd, ipStr, ntohs(srvrSkt.sin_port));

    // Set up signal handlers
    sigactionWrapper(SIGINT, goodbye);
    sigactionWrapper(SIGTERM, goodbye);

    while (1)
    {
        printf("\nFACTORY server waiting for Order Requests\n");

        // Receive order request
        msgBuf msg1;
        memset(&msg1, 0, sizeof(msg1));
        socklen_t client_len = sizeof(clntSkt);
        
        if (recvfrom(sd, &msg1, sizeof(msg1), 0, (SA *)&clntSkt, &client_len) < 0){
            err_sys("recvfrom");
        };
    

        printf("\n\nFACTORY server received: ");
        printMsg(&msg1);
        puts("");
        
        inet_ntop( AF_INET, (void *) & clntSkt.sin_addr.s_addr , ipStr , IPSTRLEN );
        printf("        From IP %s Port %d\n", ipStr, ntohs(clntSkt.sin_port));

        // Store order size (convert from network byte order)
        orderSize = ntohl(msg1.orderSize);

        // Prepare and send order confirmation with network byte order
        msg1.purpose = htonl(ORDR_CONFIRM);
        msg1.numFac = htonl(1);
        msg1.orderSize = htonl(orderSize);

        if (sendto(sd, &msg1, sizeof(msg1), 0, (SA *)&clntSkt, client_len) < 0) {
            perror("sendto failed");
            continue;
        }

        printf("\n\nFACTORY sent this Order Confirmation to the client ");
        printMsg(&msg1);
        puts("");
        
        remainsToMake = orderSize;
        subFactory(1, 50, 350);  // Single factory, ID=1, capacity=50, duration=350 ms
    }
 
    return 0;
}

void subFactory(int factoryID, int myCapacity, int myDuration)
{
    char strBuff[MAXSTR];
    int partsImade = 0, myIterations = 0;
    msgBuf msg;

    while (1)
    {
        if (remainsToMake <= 0)
            break;

        int toMake = minimum(myCapacity, remainsToMake);
        remainsToMake -= toMake;
        partsImade += toMake;
        myIterations++;
        printf("Factory # %d: Going to make     %d parts in   %d mSec\n", factoryID, toMake, myDuration);

        Usleep(myDuration * 1000);

        // Send production message with network byte order
        msg.purpose = htonl(PRODUCTION_MSG);
        msg.facID = htonl(factoryID);
        msg.capacity = htonl(myCapacity);
        msg.partsMade = htonl(toMake);
        msg.duration = htonl(myDuration);

        if (sendto(sd, &msg, sizeof(msg), 0, (SA *)&clntSkt, sizeof(clntSkt)) < 0) {
            perror("sendto failed in production message");
            // Note: We continue production even if sending fails
        }
    }

    // Send completion message with network byte order
    memset(&msg, 0, sizeof(msg));
    msg.purpose = htonl(COMPLETION_MSG);
    msg.facID = htonl(factoryID);
    msg.capacity = htonl(myCapacity);
    msg.partsMade = htonl(partsImade);
    msg.duration = htonl(myDuration);

    if (sendto(sd, &msg, sizeof(msg), 0, (SA *)&clntSkt, sizeof(clntSkt)) < 0) {
        perror("sendto failed in completion message");
    }

    snprintf(strBuff, MAXSTR, ">>> Factory # %-3d: Terminating after making total of %-5d parts in %-4d iterations\n",
             factoryID, partsImade, myIterations);
    factLog(strBuff);
}