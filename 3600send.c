/*
 *   CS3600 Project 4: A Simple Transport Protocol
 */

#include <math.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "3600sendrecv.h"

//max buffer size, timeout duration
static int DATA_SIZE = 1460;
static unsigned int TIMEOUT_SEC = 2;
static unsigned int TIMEOUT_USEC = 0;

//Starting sequence, window size, buffer
unsigned int sequence = 0;
int window_size = 100;
unsigned char* buffer;
unsigned char* buffer_pointer;

//Data structure to be stored in the window
struct bufferdata {
  int valid;
  int sequence;
  int length;
  unsigned char *offset;
};

struct bufferdata *buffer_contents;

//returns first free index in window
int find_free_buffer_contents_index(){
  for(int i = 0; i < window_size; i++){
    if (buffer_contents[i].valid) continue;
    return i;
  }
  return -1;
}

//Given a sequence, return packet index in window
int find_packet_in_buffer(int sequence){
  for(int i = 0; i < window_size; i++){
    if(buffer_contents[i].valid){
      if(buffer_contents[i].sequence == sequence) return i;
    }
  }
  return -1;
}

//Invalidate all packets in window with a lesser sequence number
void invalidate_less_than(int sequence){
  for(int i = 0; i < window_size; i++){
    if(buffer_contents[i].valid){
      if(buffer_contents[i].sequence < sequence) buffer_contents[i].valid = 0;
    }
  }
}

//Get packet at index in window
void *get_packet_from_buffer(int bindex){
  if (bindex < 0) return NULL;
  void *packet = calloc(buffer_contents[bindex].length, 1);
  memcpy(packet, buffer_contents[bindex].offset, buffer_contents[bindex].length);
  return packet;
}

//Print usage message
void usage() {
  printf("Usage: 3600send host:port\n");
  exit(1);
}

/**
 * Reads the next block of data from stdin
 */
int get_next_data(char *data, int size) {
  return read(0, data, size);
}

/**
 * Builds and returns the next packet, or NULL
 * if no more data is available.
 */
int get_next_packet(int sequence) {
  char *data = malloc(DATA_SIZE);
  int data_len = get_next_data(data, DATA_SIZE);

  if (data_len == 0) {
    free(data);
    return -1;
  }

  header *myheader = make_header(sequence, data_len, 0, 0);
  void *packet = malloc(sizeof(header) + data_len);
  memcpy(packet, myheader, sizeof(header));
  memcpy(((char *) packet) +sizeof(header), data, data_len);

  free(data);
  free(myheader);

  int len = sizeof(header) + data_len;

  int bindex = find_free_buffer_contents_index();
  if (bindex < 0) exit(1); // CRASH!
  buffer_contents[bindex].length = len;
  buffer_contents[bindex].sequence = sequence;
  buffer_contents[bindex].offset = buffer_pointer;
  buffer_contents[bindex].valid = 1;

  memcpy(buffer_pointer, packet, len);
  buffer_pointer += 1500;
  if(buffer_pointer > buffer + window_size * 1500) buffer_pointer = buffer;


  free(packet);

  mylog("[made packet] %d\n", sequence);
  return bindex;
}

//Send a given packet
int send_packet(int sock, struct sockaddr_in out, void *packet, int packet_len) {
  if (packet == NULL) return 0;

  mylog("[send data] %d (%d)\n", sequence, packet_len - sizeof(header));

  if (sendto(sock, packet, packet_len, 0, (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
    perror("sendto");
    exit(1);
  }

  return 1;
}

//Send a packet with an EOF signal
void send_final_packet(int sock, struct sockaddr_in out) {
  header *myheader = make_header(sequence, 0, 1, 0);
  mylog("[send eof]\n");

  if (sendto(sock, myheader, sizeof(header), 0, (struct sockaddr *) &out, (socklen_t) sizeof(out)) < 0) {
    perror("sendto");
    exit(1);
  }
}

//Set the maximum timeout
void set_timeout(struct timeval *t){
  t->tv_sec = TIMEOUT_SEC;
  t->tv_usec = TIMEOUT_USEC;
}

int main(int argc, char *argv[]) {
  /**
   * I've included some basic code for opening a UDP socket in C, 
   * binding to a empheral port, printing out the port number.
   * 
   * I've also included a very simple transport protocol that simply
   * acknowledges every received packet.  It has a header, but does
   * not do any error handling (i.e., it does not have sequence 
   * numbers, timeouts, retries, a "window"). You will
   * need to fill in many of the details, but this should be enough to
   * get you started.
   */
  mylog("[start server] send\n");


  //Initialize the window to given size  
  buffer = (unsigned char *) malloc(window_size * 1500);
  buffer_pointer = buffer;
  buffer_contents = (struct bufferdata *) calloc(window_size, sizeof(struct bufferdata));


  // extract the host IP and port
  if ((argc != 2) || (strstr(argv[1], ":") == NULL)) {
    usage();
  }

  char *tmp = (char *) malloc(strlen(argv[1])+1);
  strcpy(tmp, argv[1]);

  char *ip_s = strtok(tmp, ":");
  char *port_s = strtok(NULL, ":");
 
  // first, open a UDP socket  
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  // next, construct the local port
  struct sockaddr_in out;
  out.sin_family = AF_INET;
  out.sin_port = htons(atoi(port_s));
  out.sin_addr.s_addr = inet_addr(ip_s);

  // socket for received packets
  struct sockaddr_in in;
  socklen_t in_len;

  // construct the socket set
  fd_set socks;

  // construct the timeout
  struct timeval *t = (struct timeval *) malloc(sizeof(struct timeval));
  set_timeout(t);

  int bindex = get_next_packet(sequence);
  
  int window = window_size;

  int timeout_count = 0;

  int received_eof = 0; 

  //Until the receiving client confirms they have received all info, keep going
  while (!received_eof){
    send_packet(sock, out, get_packet_from_buffer(bindex), buffer_contents[bindex].length);
    window --;
    while(window > 0){
      sequence += buffer_contents[bindex].length - sizeof(header);
      bindex = get_next_packet(sequence);
      if(!send_packet(sock, out, get_packet_from_buffer(bindex), buffer_contents[bindex].length)){
        send_final_packet(sock, out);
        break;
      }
      window --;
    }

    int done = 0;


    while (!done) {
      FD_ZERO(&socks);
      FD_SET(sock, &socks);

      // wait to receive, or for a timeout
      if (select(sock + 1, &socks, NULL, NULL, t)) {
        unsigned char buf[10000];
        int buf_len = sizeof(buf);
        int received;
        if ((received = recvfrom(sock, &buf, buf_len, 0, (struct sockaddr *) &in, (socklen_t *) &in_len)) < 0) {
          perror("recvfrom");
          exit(1);
        }

        header *myheader = get_header(buf);
        if (myheader->magic == MAGIC && myheader->eof == 1)
          received_eof = 1;

        if ((myheader->magic == MAGIC) && (myheader->ack == 1)) {
          mylog("[recv ack] %d\n", myheader->sequence);
          sequence = myheader->sequence;
	  invalidate_less_than(sequence);
          bindex = find_packet_in_buffer(sequence);
          if(bindex < 0){
            bindex = get_next_packet(sequence);
            window ++;
          }
          mylog("[window] size: %d\n", window);
          done = 1;
          timeout_count = 0;
        } else {
          mylog("[recv corrupted ack] %x %d\n", MAGIC, sequence);
        }
      } else {
        timeout_count ++;
//        if(timeout_count < 10) {
          mylog("[timeout] occurred, resending\n");
          for(int i = 0; i < window_size; i++){
            if(buffer_contents[i].valid)
              send_packet(sock, out, get_packet_from_buffer(i), buffer_contents[i].length);
          }
          set_timeout(t);
 //       }
  /*      else {
          mylog("[error] timeout occured 10 times, lost connection\n");
          // make sure the other size knows I'm quitting, if it's still there.
          send_final_packet(sock,out);
          exit(1);
        }*/
      }
    }
  }


  mylog("[completed]\n");

  return 0;
}
