/* COMP 112 - Networks and Protocols               */
/* Tufts Fall 2015 - Justin Lee                    */ 
/* Assignment #2 - GO-BACK-N UDP ARQ File Transfer */ 

/* This is a simple file transfer program using UDP    */                       
/* Reliability will be implemented using the GO-BACK-N */
/* ARQ protocol of a 3ms fixed timeout value and a 5   */
/* consecutive timeout disconnect.                     */

#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>   
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>    
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>  

#define BUFSIZE 1024
#define DATASIZE 512

#define timeout_sec 1
#define timeout_usec 0

/* General Boolean Tags */
#define TRUE   1
#define FALSE  0
#define SUCCESS 1
#define ERROR   0
#define AUX    -1

/* Message Format (Packed Struct) */
typedef struct __attribute((__packed__)) msg_1{
        char type;
        char win_size;
        char fileName[20];
}msg_1;

typedef struct __attribute((__packed__)) msg_2{
        char type;
        char seq_no;
        char data[512];
}msg_2;

typedef struct __attribute((__packed__)) msg_3{
        char type;
        char seq_no;
}msg_3;

typedef struct __attribute((__packed__)) msg_4{
        char type;
}msg_4;


void sendError(int socket, void *cli_addrp, socklen_t clilen);
int transmit(FILE *file, int win_size, int socket, void *cli_addrp, void *clilen);

/* Useful Error Function */
void error(const char *msg)
{
        perror(msg);
        exit(1);
}

int main(int argc, char *argv[])
{
        /* --- Server/Client addr --- */
        struct sockaddr_in serv_addr, cli_addr;
        
        /* --- Sockets and Port Number --- */
        int master_socket, portno;  
        socklen_t clilen = sizeof(cli_addr);     

        /* --- Buffers --- */
        char buf[BUFSIZE]; 
        int n;                   /* message byte size */
        
        /* --- Misc ---  */
        int retval;                               
        int i;
        
        /* Error Check of command line arguements */
        if (argc < 2) {
                fprintf(stderr,"ERROR, no port provided\n");
                exit(1);
        }
       
        /*--------------- SERVER SOCKET & BINDING --------------------*/
        /* Create a master socket */
        master_socket = socket(AF_INET , SOCK_DGRAM , 0);
        if (master_socket < 0)
                error("ERROR, socket call");

        /* Clear and initialize serv_addr struct */
        bzero((char *) &serv_addr, sizeof(serv_addr));
        portno = atoi(argv[1]);
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(portno);
        /* Binding socket to address in serv_addr */
        if (bind(master_socket, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
                error("ERROR on binding");
        /*-----------------------------------------------------------*/
        
        fprintf(stderr, "Server listening on port %d \n", portno);
        
        while(TRUE){
                fprintf(stderr, "-------------------------------------\n");
                printf("Waiting for client...\n");
                fflush(stdout);
                
                /* Bzeroing field allows Buffer INIT of NULLs, in order to avoid reading issues */
                bzero(buf, BUFSIZE);
                
                /* Set Socket option to have not timeout while waiting for new clients */
                struct timeval tv;
                tv.tv_sec = FALSE;
                tv.tv_usec = FALSE;
                n = setsockopt(master_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                if (n < 0)
                        error("ERROR in setsockopt(1)");       
                
                n = recvfrom(master_socket, buf, BUFSIZE, 0, (struct sockaddr *) &cli_addr, &clilen);
                if (n < 0)
                        error("ERROR in recvfrom");
              
                /* print details of the client/peer and the data received */
                printf("Received packet from %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
                      
                /* Received RRQ Message */
                if(buf[0] == (char)1){
                        msg_1 rrq_msg = *(msg_1 *)buf;
                        fprintf(stderr, "\nGot a RRQ of Wondow Size of %i for file: %s\n",
                                (int)rrq_msg.win_size, rrq_msg.fileName);     
                        FILE* file = fopen(rrq_msg.fileName,"r");
                        if(file == NULL){
                                sendError(master_socket,(struct sockaddr *) &cli_addr, clilen);
                                continue;
                        }
                        
                        retval = transmit(file, (int)rrq_msg.win_size, master_socket, (struct sockaddr *) &cli_addr ,
                                          &clilen);
                        if(retval == ERROR){
                                fprintf(stderr, "--Acknowledgement Request Timeout\n");
                                fprintf(stderr, "--Closing Connection\n");
                        }
                        
                        fclose(file);
                }
                else {
                        fprintf(stderr, "Not a RRQ request. Ignore\n");
                        continue;
                }  
        }
        
        close(master_socket);
        return 0;
}

int transmit(FILE *file, int win_size, int socket, void *cli_addrp, void *clilenp){
        char buf[BUFSIZE]; 
        fprintf(stderr, "--File Found! Transmitting...\n");
        
        /* Finding File Size */
        struct stat filestat;
        fstat((int)fileno(file), &filestat);
        int fileSize = filestat.st_size;
        fprintf(stderr, "--Total Size: %i bytes\n", fileSize);
        
        /* Variables for window traversing */
        int expectedack = 0;    
        int timeoutcount = 0;
        int donetransmit = FALSE;
        int endRead      = FALSE;
        int sendindex    = 0;
        int readtill     = win_size -1;
        
        /* Populating Array with Initial Sending Window */
        /* Note that since our packets are cleared, even if the size of the file requested */
        /* doesn't require win_size amount of packets to transmit, it will simply send    */
        /* over null characters                                                            */
        int i,n;
        msg_2 *window[win_size];

        for(i = 0; (i < win_size) && (!endRead); i++){
                msg_2 *packet = malloc(sizeof(msg_2));
                bzero(packet, sizeof(msg_2));
                packet -> type = (char)2;
                packet -> seq_no = (char)i;
                n = fread(packet->data, DATASIZE, 1, file);
                if(n == 0){
                       fprintf(stderr, "Read to EOF in seq_no: %i\n",i);
                       endRead == TRUE;
                       readtill == i;
                }
                window[i] = packet;
                
                /* Peaking Data Packets Data Portion */
                //fprintf(stderr, "--- Packet Data in %i ---- \n%s\n", i, window[i]->data);
                //fprintf(stderr, "***** Length: %i *****\n" , strlen(window[i]->data));
                fprintf(stderr, "--Sending Packet %i; Data Size: %i...\n", (int)window[i]->seq_no, strlen(window[i]->data));
                n = sendto(socket,(char *) packet, strlen(packet->data)+2, 0,
                           (struct sockaddr *) cli_addrp, *(socklen_t *)clilenp);
                if (n < 0) 
                        error("ERROR in sendto"); 
        }
        
        /* Begin GO-BACK-N ARQ */
        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = timeout_usec;
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); 
        
        while(!donetransmit){
                
                if(recvfrom(socket, buf, BUFSIZE, 0, (struct sockaddr *) cli_addrp, clilenp) < ERROR){
                        timeoutcount++;
                        fprintf(stderr, "Timeout: %i\n", timeoutcount);
                        if(timeoutcount >= 5){
                                return ERROR;
                        }
                        else{
                                /* RESEND ENTIRE WINDOW */
                                for(i = sendindex; i < win_size; i++){
                                        fprintf(stderr, "--Resending Packet %i...\n", (int)window[i]->seq_no);
                                        n = sendto(socket,(char *)window[i], strlen(window[i]->data)+2, 0,
                                                  (struct sockaddr *) cli_addrp, *(socklen_t *)clilenp);
                                        if (n < 0) 
                                                error("ERROR in resending");   
                                        }
                                continue;
                        }
                }
                
                /* Received Acknowledgement */
                else{
                        timeoutcount = 0;
                        if(buf[0] == (char)3){
                                msg_3 ack_msg = *(msg_3 *)buf;
                                fprintf(stderr, "Got a ACK message for sequence %i\n", (int)ack_msg.seq_no);
                                
                                /* Receied correct acknowledgement */
                                if((int)ack_msg.seq_no >= expectedack){
                                        fprintf(stderr, "CORRECT ACK!: Received %i when expecting %i\n", (int)ack_msg.seq_no, expectedack);
                                        /* There is still things to fread */
                                        if(endRead != TRUE){
                                                /* If received a later ACK, assume that some ACK was simply dropped and that client is not missing anything */
                                                while((expectedack <= (int)ack_msg.seq_no)&& !endRead){
                                                        /* Free first entry and push everything up (shifting the window) */
                                                        free(window[0]);
                                                        for(i = 0; i < (win_size-1); i++){
                                                                window[i] = window[i+1];
                                                        }
                                                        /* Make the new packet to fill */
                                                        int newseqno = expectedack + win_size;
                                                        fprintf(stderr, "Pushing sequence %i into window\n", newseqno);
                                                        msg_2 *newpacket = malloc(sizeof(msg_2));
                                                        bzero(newpacket, sizeof(msg_2));
                                                        newpacket -> type = (char)2;
                                                        newpacket -> seq_no = (char)newseqno;
                                                        n = fread(newpacket->data, DATASIZE, 1, file);
                                                        if(n == 0){
                                                                fprintf(stderr, "Read to EOF in seq_no: %i\n",newseqno);
                                                                endRead = TRUE;
                                                        }
                                                        window[win_size-1] = newpacket;
                        
                                                        fprintf(stderr, "--Sending Packet %i; Data Size: %i...\n", (int)newpacket->seq_no, strlen(newpacket->data));
                                                        n = sendto(socket,(char *) newpacket, strlen(newpacket->data)+2, 0,
                                                                (struct sockaddr *) cli_addrp, *(socklen_t *)clilenp);
                                                        if (n < 0) 
                                                                error("ERROR in sendto"); 
                                                        expectedack++;
                                                }
                                        }
                                        else if(endRead == TRUE){
                                                /* fread has reached EOF, so instead of shifting window, shift up 
                                                 * read index to complete the window 
                                                 */

                                                if (sendindex == win_size-1){
                                                        fprintf(stderr, "***** Transmission completed! ******\n");
                                                        donetransmit = TRUE;
                                                }
                                                else{
                                                        while(expectedack <= (int)ack_msg.seq_no){
                                                                sendindex++;
                                                                expectedack++;
                                                        }
                                                }
                                                
                                        }
                                }
                        }
                }
        }
        return SUCCESS;
}



void sendError(int socket, void *cli_addrp, socklen_t clilen){
        int n; 
        fprintf(stderr, "--Requested File Not Found\n");
        msg_4 msg;
        msg.type = (char)4;
        fprintf(stderr, "--Sending ERROR message (type 4)\n");
        n = sendto(socket, (char *)&msg, 1, 0,(struct sockaddr *) cli_addrp, clilen);
        if (n < 0) 
                error("ERROR in sendto"); 
        
}