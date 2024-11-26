//---------------------------------------------------------------------
// Assignment : PA-03 UDP Single-Threaded Server
// Date       :
// Author     : Joshua Cassada and Thomas Cantrell
// File Name  : procurement.c
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

#define MAXFACTORIES    20

typedef struct sockaddr SA;

/*-------------------------------------------------------*/
int main(int argc, char *argv[])
{
    int     numFactories,      // Total Number of Factory Threads
            activeFactories,   // How many are still alive and manufacturing parts
            iters[MAXFACTORIES+1] = {0},  // num Iterations completed by each Factory
            partsMade[MAXFACTORIES+1] = {0}, 
            totalItems = 0;

    char  *myName = "Joshua Cassada and Thomas Cantrell"; 
    printf("\nPROCUREMENT: Started. Developed by %s\n\n", myName);    

    char myUserName[30];
    getlogin_r(myUserName, 30);
    time_t  now;
    time(&now);
    fprintf(stdout, "Logged in as user '%s' on %s\n\n", myUserName, ctime(&now));
    fflush(stdout);
    
    if (argc < 4)
    {
        printf("PROCUREMENT Usage: %s  <order_size> <FactoryServerIP>  <port>\n", argv[0]);
        exit(-1);
    }

    unsigned        orderSize  = atoi(argv[1]);
    char	       *serverIP   = argv[2];
    unsigned short  port       = (unsigned short) atoi(argv[3]);
 
    /* Set up local and remote sockets */
    struct sockaddr_in myAddr, serverAddr;
    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialize local address structure
    memset((void *) &myAddr, 0, sizeof(myAddr));
    myAddr.sin_family = AF_INET;
    myAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    myAddr.sin_port = htons(0);  // Let system assign port

    if (bind(sd, (const struct sockaddr *)&myAddr, sizeof(myAddr)) < 0) {
        perror("bind failed");
        close(sd);
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    memset((void *) &serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sd);
        exit(EXIT_FAILURE);
    }

    printf("\nAttempting Factory server at '%s' : %d\n", serverIP, port);


    // Send the initial request to the Factory Server
    msgBuf msg1;
    memset(&msg1, 0, sizeof(msg1));
    msg1.purpose = htonl(REQUEST_MSG);
    msg1.orderSize = htonl(orderSize);
    
    if (sendto(sd, &msg1, sizeof(msg1), 0, (SA *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("sendto failed");
        close(sd);
        exit(EXIT_FAILURE);
    }

    printf("\nPROCUREMENT Sent this message to the FACTORY server: ");
    printMsg(&msg1);
    puts("");

    /* Now, wait for order confirmation from the Factory server */
    msgBuf msg2;
    memset(&msg2, 0, sizeof(msg2));
    printf("\nPROCUREMENT is now waiting for order confirmation ...\n");

    socklen_t len = sizeof(serverAddr);
    if (recvfrom(sd, &msg2, sizeof(msg2), 0, (SA *)&serverAddr, &len) < 0) {
        perror("recvfrom failed");
        close(sd);
        exit(EXIT_FAILURE);
    }

    printf("PROCUREMENT received this from the FACTORY server: ");
    printMsg(&msg2);
    puts("\n");

    // Convert received values from network to host byte order
    numFactories = ntohl(msg2.numFac);
    orderSize = ntohl(msg2.orderSize);  // Make sure we have the confirmed order size
    activeFactories = numFactories;

    // Monitor all Active Factory Lines & Collect Production Reports
    while (activeFactories > 0) 
    {
        msgBuf msg;
        memset(&msg, 0, sizeof(msg));

        if (recvfrom(sd, &msg, sizeof(msg), 0, (SA *)&serverAddr, &len) < 0) {
            err_sys("recvfrom failed");
        }

        // Convert all received values from network to host byte order
        msgPurpose_t purpose = ntohl(msg.purpose);
        unsigned facID = ntohl(msg.facID);
        unsigned capacity = ntohl(msg.capacity);
        unsigned partsMadeNow = ntohl(msg.partsMade);
        unsigned duration = ntohl(msg.duration);

        if (purpose == PRODUCTION_MSG) {
            iters[facID]++;
            partsMade[facID] += partsMadeNow;
            printf("PROCUREMENT: Factory #%d   produced %d   parts in %d   milliSecs\n",
               facID, partsMadeNow, duration);
        } 
        else if (purpose == COMPLETION_MSG) {
            activeFactories--;
            printf("PROCUREMENT: Factory #%d         COMPLETED its task\n", facID);
        }
        else if (purpose == PROTOCOL_ERR){
            printf("PROCUREMENT: Received { PROTOCOL_ERROR }\n");
            close(sd);
            exit(1);
        }
    }

    // Print the summary report
    totalItems = 0;
    printf("\n\n****** PROCUREMENT Summary Report ******\n");

    for (int i = 1; i <= numFactories; i++) {
        printf("Factory #%d: made a total  of    %d items in      %d iterations\n",
               i, partsMade[i], iters[i]);
        totalItems += partsMade[i];
    }

    printf("==============================\n");
    printf("Grand total parts made = %d   vs order size of   %d\n", totalItems, orderSize);

    if (totalItems == orderSize) {
        printf("\n>>> PROCUREMENT Terminated\n");
    } else {
        printf("Order size mismatch - error in production\n");
    }


    close(sd);
    return 0;
}