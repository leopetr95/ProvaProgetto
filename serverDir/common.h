#ifndef COMMON_H_H_
#define COMMON_H_H_

#include "basic.h"
#include "data_types.h"

int check_existence(char* filename);
struct ACKPacket createACKPacket (int ack_type, int base);
struct segmentPacket createDataPacket(int seqNO, int length, char* data);
struct segmentPacket createFinalPacket(int seqNO, int length);
int is_lost(float loss_rate);

void error(char* str);
char* read_from_stdin();
int generate_casual();
void initialize_addr(struct sockaddr_in* s);

#endif
