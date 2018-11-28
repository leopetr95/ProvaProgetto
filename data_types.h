#ifndef DATA_TYPES_H_
#define DATA_TYPES_H_


#define MAXLINE 1460
#include <sys/types.h>
#include <sys/socket.h>
 #include <netinet/in.h>
#include <semaphore.h>
#include <pthread.h>

#define CHUNCK 512



typedef struct{
	sem_t sem;
	int n_avail;
}Process_data;

typedef struct segmentPacket{

	int type;
	int seq_no;
	int ack_no;
	int length;
	char data[CHUNCK];

}segmentPacket;

typedef struct ACKPacket{

	int type;
	int ack_no;
	char data[10];

}ACKPacket;

typedef struct{
	int n_seq,n_ack;
	char data[MAXLINE];
	int flag;
	struct timespec tstart;
}Header;


struct msgbuf{
	long mtype;
	int client_seq;
	struct sockaddr_in s;
};


typedef struct circ_buff{
	Header *win;
	pthread_mutex_t mtx;
	int end;
	int S,E;
}Window;


struct thread_data{
	int sockfd;
	struct sockaddr_in servaddr;
	pthread_t tid;
	Window* w;
};


#endif
