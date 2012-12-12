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

unsigned int WINDOW_SIZE = 10;
unsigned int last_seq_recv = -1;
unsigned int last_seq_length = 0;
unsigned int last_seq_eof = 0;
int buf_len = 1500;
char *window;


void output_packets()
{
  unsigned int i = 0;
  for(i = 0; i < WINDOW_SIZE; i++)
  {
    void *buffer = (void *)(window + (i*buf_len));
    header *myheader = get_header_again(buffer);
    char *data = get_data(buffer);

    //This is an empty slot in the window
    if(myheader->magic != MAGIC) continue;

    if(myheader->sequence == last_seq_recv + last_seq_length)
    {
      //Found the next packet in memory!
      mylog("[recv data] %d (%d) %s\n", myheader->sequence, myheader->length, "PULLED FROM WINDOW");

      //Send it out!
      last_seq_recv = myheader->sequence;
      last_seq_length = myheader->length;
      last_seq_eof = myheader->eof;
      write(1, data, myheader->length);

      //Fill with zeros!
      memset(buffer, 0, buf_len);

      //Keep looking for the next one!
      i = 0;
    }
  }
}

void add_window(void *buffer)
{
  unsigned int i = 0;
  header *newheader = get_header_again(buffer);
  for(i = 0; i < WINDOW_SIZE; i++)
  {
    header *myheader = get_header_again((void *)(window + (i*buf_len)));
    if(myheader->magic != MAGIC) break;
    if(newheader->magic == MAGIC && myheader->sequence == newheader->sequence)
    {
        mylog("[recv data] %d (%d) %s\n", myheader->sequence, myheader->length, "ALREADY IN WINDOW");
        break;
    }
  }

  //Full window?
  if(i == WINDOW_SIZE)
  {
    mylog("pie");
    exit(1);
  }

  //add to the window
  memcpy(window + (i*buf_len), buffer, buf_len);
}

int main() {
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

  mylog("[start server] recv\n");
  window = calloc(WINDOW_SIZE, buf_len);

  // first, open a UDP socket  
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  // next, construct the local port
  struct sockaddr_in out;
  out.sin_family = AF_INET;
  out.sin_port = htons(0);
  out.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sock, (struct sockaddr *) &out, sizeof(out))) {
    perror("bind");
    exit(1);
  }

  struct sockaddr_in tmp;
  int len = sizeof(tmp);
  if (getsockname(sock, (struct sockaddr *) &tmp, (socklen_t *) &len)) {
    perror("getsockname");
    exit(1);
  }

  mylog("[bound] %d\n", ntohs(tmp.sin_port));

  // wait for incoming packets
  struct sockaddr_in in;
  socklen_t in_len = sizeof(in);

  // construct the socket set
  fd_set socks;

  // construct the timeout
  struct timeval t;
  t.tv_sec = 30;
  t.tv_usec = 0;

  // our receive buffer
  void* buf = malloc(buf_len);

  // wait to receive, or for a timeout
  while (1) {
    FD_ZERO(&socks);
    FD_SET(sock, &socks);

    if (select(sock + 1, &socks, NULL, NULL, &t)) {
      int received;
      if ((received = recvfrom(sock, buf, buf_len, 0, (struct sockaddr *) &in, (socklen_t *) &in_len)) < 0) {
        perror("recvfrom");
        exit(1);
      }

      dump_packet(buf, received);

      header *myheader = get_header(buf);
      char *data = get_data(buf);
  
      if (myheader->magic == MAGIC) {

        if(myheader->sequence == last_seq_recv){
          mylog("[recv duplicate] %d\n", myheader->sequence);
        } else if(myheader->sequence == last_seq_recv + last_seq_length || last_seq_recv == (unsigned int)(-1) ) {
          mylog("[recv data] %d (%d) %s\n", myheader->sequence, myheader->length, "ACCEPTED");
          write(1, data, myheader->length);
          last_seq_recv = myheader->sequence;
          last_seq_length = myheader->length;
          last_seq_eof = myheader->eof;
          output_packets();
        } else {
          mylog("[recv data] %d (%d) %s\n", myheader->sequence, myheader->length, "ADDED TO WINDOW");
          add_window(buf);
          output_packets();
        }
        mylog("[send ack] %d\n", last_seq_recv + last_seq_length);

        header *responseheader = make_header(last_seq_recv + last_seq_length, 0, last_seq_eof, 1);
        if (sendto(sock, responseheader, sizeof(header), 0, (struct sockaddr *) &in, (socklen_t) sizeof(in)) < 0) {
          perror("sendto");
          exit(1);
        }

        if (myheader->eof) {
          output_packets();
          mylog("[recv eof]\n");
          mylog("[completed]\n");
          exit(0);
        }
      } else {
        mylog("[recv corrupted packet]\n");
      }
    } else {
      mylog("[error] timeout occurred\n");
      exit(1);
    }
  }


  return 0;
}
