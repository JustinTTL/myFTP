/* 
 * udpclient.c - A UDP client that will drop packets and acknowledgements for testing purposes
 * usage: udpclient <host> <port>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define BUFSIZE 1024
#define DATASIZE 512
#define DROPCHANCE 25  /* TUNE DROP CHANCE HERE  !! */

/* Message Format (Packed Struct) */
typedef struct __attribute((__packed__)) msg_1{
        char type;
        char win_size;
        char fileName[20];
}msg_1;

typedef struct __attribute((__packed__)) msg_2{
        char type;
        char seq_no;
        char data[DATASIZE];
}msg_2;

typedef struct __attribute((__packed__)) msg_3{
        char type;
        char seq_no;
}msg_3;

typedef struct __attribute((__packed__)) msg_4{
        char type;
}msg_4;


/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE];

    /* check command line arguments */
    if (argc != 3) {
       fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);
        
    msg_1 rrq;
    bzero((char *) &rrq, sizeof(msg_1));
    rrq.type = (char)1;
    rrq.win_size = (char)1;                /* TODO: WIN SIZE GOES HERE */
    strcpy(rrq.fileName,"Example1.txt");  /* TODO: REQUEST FILE NAME GOES HERE */ 
    
    /* send the message to the server */
    serverlen = sizeof(serveraddr);
    int msglen = strlen(rrq.fileName) + 2;
    fprintf(stderr, "Sending RRQ MSG for %s of size: %i\n", rrq.fileName, msglen);
    n = sendto(sockfd, (char *)&rrq, msglen , 0, (struct sockaddr *) &serveraddr, serverlen);
    if (n < 0) 
      error("ERROR in sendto");
   
    int currseq = 0;
    
    FILE *file = fopen("result.txt", "w");
    if (file == NULL)
    {
        printf("Error opening file!\n");
        exit(1);
    }
    
    
    /* Main Loop*/
    int donetransmit = 0;
    while(!donetransmit){
        bzero(buf, BUFSIZE);
        n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &serveraddr, &serverlen);
        if (n < 0)
                error("ERROR in recvfrom");
        
        if((rand()%100) < DROPCHANCE ){
                fprintf(stderr, "Packet %i is lost\n",buf[1]);
                continue;
        }
        if(buf[0] == (char)4){
                error("File Not Found");
        }
        if(buf[0] == (char)2){
                msg_2 data_msg = *(msg_2 *)buf;
                fprintf(stderr, "Received data for seq: %i; Data size: %i\n", data_msg.seq_no,n-2);
                /* Received already obtained sequence, client needs to reaffirm */
                if(data_msg.seq_no < currseq){
                       fprintf(stderr, "----Reaffirm server with ACK message for %i\n",data_msg.seq_no); 
                       if(((rand()%100) < DROPCHANCE)&& !donetransmit){
                                fprintf(stderr, "----ACK for seq %i is dropped again!\n", data_msg.seq_no);
                                continue;
                        }
                        msg_3 ack_msg;
                        ack_msg.type = (char)3;
                        ack_msg.seq_no = (char)data_msg.seq_no;
                        sendto(sockfd, (char *)&ack_msg, 2 , 0, (struct sockaddr *) &serveraddr, serverlen); 
                
                }
                else if(data_msg.seq_no == currseq){
                        fprintf(stderr, "--Write Success for seq %i\n", data_msg.seq_no);
                        fwrite(data_msg.data,n-2,1,file); /* n-2 is the actual size of the data field, strlen returns weird result when given non-null terminated strings*/
                        if (strlen(data_msg.data) < 512){
                                fprintf(stderr, "******* TRANSMISSION FINISH! ********* \n");
                                donetransmit = 1;
                        }
                        
                        else if((rand()%100) < DROPCHANCE){
                                fprintf(stderr, "--Acknowledge for seq %i is dropped!\n", data_msg.seq_no);
                                currseq++;
                                continue;
                        }
                        
                        msg_3 ack_msg;
                        ack_msg.type = (char)3;
                        ack_msg.seq_no = (char)currseq;
                        sendto(sockfd, (char *)&ack_msg, 2 , 0, (struct sockaddr *) &serveraddr, serverlen);
                        fprintf(stderr, "Sent ACK for seq: %i\n", currseq);
                        currseq++;
                }
                else{
                        fprintf(stderr, "--RECEIVED OUT OF ORDER SEQ: %i\n", data_msg.seq_no);
                }
        }
                   
            
    }
    
    fclose (file);

    return 0;
}
