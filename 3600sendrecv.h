/*
 * CS3600 Project 4: A Simple Transport Protocol
 */

#ifndef __3600SENDRECV_H__
#define __3600SENDRECV_H__

#include <stdio.h>
#include <stdarg.h>

typedef struct header_t {
  unsigned int magic:14;
  unsigned int ack:1;
  unsigned int eof:1;
  unsigned short length;
  unsigned int sequence;
} header;

unsigned int MAGIC;

void dump_packet(unsigned char *data, int size);
header *make_header(int sequence, int length, int eof, int ack);
header *get_header(void *data);
header *get_header_again(void *data);
char *get_data(void *data);
char *timestamp();
void mylog(char *fmt, ...);

#endif

