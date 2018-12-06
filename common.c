#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#define SERVPORT 5194
#include "data_types.h"

/*Check the existence of the file in the current directory*/
int check_existence(char* filename){

	if(access(filename, F_OK)!= 0){

		return 0;

	}

	return 1;

}

/*Creates and returns an ACK packet*/
struct ACKPacket createACKPacket (int ack_type, int base){

        struct ACKPacket ack;
        ack.type = ack_type;
        ack.ack_no = base;

        return ack;

}

/*Creates and returns a data packet*/
struct segmentPacket createDataPacket(int seqNO, int length, char* data){

	struct segmentPacket pkd;

	pkd.type = 1;
	pkd.seq_no = seqNO;
	pkd.length = length;
	memset(pkd.data, 0, sizeof(pkd.data));
	strcpy(pkd.data, data);

	return pkd;

}

/*Creates and returns the final packet of data flow*/
struct segmentPacket createFinalPacket(int seqNO, int length){

	struct segmentPacket pkd;

	pkd.type = 4;
	pkd.seq_no = seqNO;
	pkd.length = length;
	memset(pkd.data, 0, sizeof(pkd.data));

	return pkd;

}

void error(char *str){

	perror(str);
	exit(EXIT_FAILURE);

}

/*Sets up the loss rate for simulation*/
int is_lost(float loss_rate){

    double rv;
    rv = drand48();
    if(rv < loss_rate){

        return(1);

    }else{

    	return(0);

    }

}

char* read_from_stdin(){

	char* line = malloc(MAXLINE*sizeof(char));
	if(line == NULL){

		error("malloc");

	}

	line = fgets(line,MAXLINE,stdin);
	if(line == NULL){

		if(errno == EINTR){

			printf("eintr\n");

		}

		if(feof(stdin)){

			return NULL;

		}

		error("fgets");
	}

	if(line[strlen(line) - 1] == '\n'){

		line[strlen(line) - 1] = '\0';

	}

	return line;

}


int generate_casual(){

    int x = random()%1000 + 1;      //number between 1 and 1000
    return x;

}


void initialize_addr(struct sockaddr_in* s){

	 struct sockaddr_in addr = *s;
	 memset((void *)&addr, 0, sizeof(addr));
	 addr.sin_family = AF_INET;
	 addr.sin_addr.s_addr = htonl(INADDR_ANY);
	 addr.sin_port = htons(SERVPORT);
	 *s = addr;

}
