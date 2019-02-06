#include "basic.h"
#include "data_types.h"
#include "common.h"

#define BUFFER_SIZE 1200
#define WINDOWSIZE 10
#define TIMEOUT 3

int n_request = 0;
extern int n_win;
int adaptive;

void CatchAlarm(int ignored)     /* Handler for SIGALRM */
{
    //printf("In Alarm\n");
}

/*Sets up signal handler to avoid zombi processes*/
void sighandler(int sign){

	(void)sign;
	int status;
	pid_t pid;

	--n_request;

	while ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0)
		;
	return;

}

void handle_sigchild(struct sigaction* sa){

    sa->sa_handler = sighandler;
    sa->sa_flags = SA_RESTART;
    sigemptyset(&sa->sa_mask);

    if (sigaction(SIGCHLD, sa, NULL) == -1) {

        fprintf(stderr, "Error in sigaction()\n");
        exit(EXIT_FAILURE);

    }

}

    /******************************************************************
     * Process tries to connect to server and generates a random int; *
     * timeout on recvfrom is setted; if no response received,        *
     * timeout is increased; if no response for 3 times, process      *
     * returns.                                                       *
     ******************************************************************/
int request_to_server(int sockfd,segmentPacket* x,struct sockaddr_in* addr, char* command){

    int n;
    struct sockaddr_in s = *addr;
	//printf("Let me print the port to which I am sending %d\n", ntohs(s.sin_port));
    struct timespec conn_time = {2,0};
    int attempts = 0;
    socklen_t len = sizeof(s);
    x->seq_no = generate_casual();
    strcpy(x->data,command);
    //printf("Stampo il comando %s\n", x->data);

    for(;;){

    	if(sendto(sockfd, x,sizeof(Header), 0, (struct sockaddr *)&s,sizeof(s)) < 0){

    		error("sendto\n");

    	}

    	if(setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&conn_time,sizeof(conn_time)) < 0){

    		error("setsockopt failed\n");

    	}

    	n = recvfrom(sockfd,x,sizeof(Header),0,(struct sockaddr*)&s,&len);

    	if(n < 0){

    		if(errno == EWOULDBLOCK){

    			printf("server not responding; trying again..\n");
    			++attempts;
    			if(attempts == 3){

    				break;

    			}

    			conn_time.tv_sec += conn_time.tv_sec;

    		}else{

    			error("recvfrom");

    		}

    	}

    	if(n>0){

    		conn_time.tv_sec = 0;
    		conn_time.tv_nsec = 0;
        	if(setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&conn_time,sizeof(conn_time)) < 0){

        		error("setsockopt failed\n");

        	}

    		printf("client connected!\n");
    	    *addr = s;
    	    return 1;

    	}

    }

    return 0;					/*not available server*/
}


/*Send the desired file to the server after the command put*/
void send_file_client(int sockfd, char *comm, struct sockaddr_in servaddr){

	struct sigaction myAction;
	myAction.sa_handler = CatchAlarm;
	if (sigemptyset(&myAction.sa_mask) < 0){ /* block everything in handler */

        error("sigfillset() failed");

    }
    myAction.sa_flags = 0;

    if (sigaction(SIGALRM, &myAction, 0) < 0){

        error("sigaction() failed for SIGALRM");

    }

	//printf("Sono dentro a send_file_server\n");

	char directoryNameCat[256] = "Files_Client/";

	char* filename = comm + 4;

	strcat(directoryNameCat, filename);

	int retCheck = check_existence(directoryNameCat);
	if(retCheck == 0){

		error("File is not present in the directory of the client\n");

	}

	//opening file to send
	int fd = open(directoryNameCat, O_RDONLY);
	if(fd < 0){

		error("Error while opening file\n");

	}

	/*acquiring stats of the file*/
	struct stat st;
	int count = stat(directoryNameCat, &st);
	if(count < 0){

		error("Error while acquiring stats of file\n");

	}

	/*getting file size*/
	int sz = st.st_size;

	int tries = 0;

	int numberOfSegments = sz / CHUNCKSIZE;

	/*if there are leftovers*/
	if(sz % CHUNCKSIZE > 0){

		numberOfSegments++;

	}

	/*setting window parameter*/
	int base = -1;	//highest segment ACK received
	int seqNum = 0;	//highest segment sent, reset by base
	int dataLenght = 0;	//CHUNCKSIZE size
	int windowSize = WINDOWSIZE;
	unsigned int fromSize;

	int noTearDownAck = 1;

	while(noTearDownAck){

		float ESTIMATEDRTT = (0.875 * ESTIMATEDRTT) + (0.125 * TIMEOUT);
	float DevRTT = (1 - 0.25) * DevRTT + 0.25 * abs(TIMEOUT - ESTIMATEDRTT);

	float TimeoutInterval = ESTIMATEDRTT + (4 * DevRTT);

		/*send packets from base up to window size*/
		while(seqNum <= numberOfSegments && (seqNum - base) <= windowSize){

			struct segmentPacket dataPacket;

			if(seqNum == numberOfSegments){

				dataPacket = createFinalPacket(seqNum, 0);
				printf("Sending final packet\n");

			}else{

				char data[CHUNCKSIZE];
				memset(data, 0, CHUNCKSIZE+1);
				int retRead = read(fd, data, CHUNCKSIZE);
				if(retRead < 0){

					error("Error while reading from file\n");

				}

				dataLenght = retRead;

				//printf("Stampo quello che ho letto dal file: \n%s\n", data);

				dataPacket = createDataPacket(seqNum, dataLenght, data);
				printf("Sending packet: %d\n", seqNum);

			}

			char bufferToSend[sizeof(struct segmentPacket)];
			memcpy(bufferToSend, &dataPacket, sizeof(struct segmentPacket));


			if(sendto(sockfd, bufferToSend, sizeof(bufferToSend), 0, (struct sockaddr *)&servaddr, sizeof(servaddr))<0){

				error("Error while sending packet\n");

			}

			seqNum++;

		}

		alarm(TimeoutInterval);

		int respStringlen;

		printf("Window full: waiting for acks\n");

		struct ACKPacket ack;

		while((respStringlen = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&servaddr, &fromSize)) < 0){

			if(errno == EINTR){

				seqNum = base + 1;

				printf("Timeout: resending\n");

				if(tries >= 10){

					printf("Tries exceeded: Closing\n");
					exit(1);

				}else{

					alarm(0);

					while(seqNum <= numberOfSegments &&(seqNum - base) <= windowSize){

						struct segmentPacket dataPacket;
						//printf("Stampo al dimensione di dataPacket %ld\n", sizeof(dataPacket));

						if(seqNum == numberOfSegments){

							dataPacket = createFinalPacket(seqNum, 0);
							printf("Sending final packet");

						}else{

							char data[CHUNCKSIZE];
							char copy[CHUNCKSIZE];
							bzero(data, strlen(data));

							lseek(fd, seqNum * CHUNCKSIZE, SEEK_SET);
							
							int retRead = read(fd, data, CHUNCKSIZE);
							if(retRead < 0){

								error("Error while reading from file\n");

							}

							strncpy(copy, data, CHUNCKSIZE);
							copy[CHUNCKSIZE] = 0;

							//printf("STAMPO COPY \n%s\n", copy);

							dataLenght = retRead;

							dataPacket = createDataPacket(seqNum, dataLenght, copy);

							//printf("Sending packet: %d\n", seqNum);

						}


						char bufferToSend[sizeof(struct segmentPacket)];

						memcpy(bufferToSend, &dataPacket, sizeof(struct segmentPacket));


						if(sendto(sockfd, bufferToSend, sizeof(bufferToSend), 0, (struct sockaddr *)&servaddr, sizeof(servaddr))<0){

							error("Error while sending to socket\n");

						}

						seqNum++;

					}

					alarm(TimeoutInterval);

				}

				tries++;

			}else{

				error("Error while recvfrom\n");

			}

		}

		if(ack.type != 8){

			printf("Received ack: %d\n", ack.ack_no);
			if(ack.ack_no > base){

				base = ack.ack_no;

			}

		}else{

			printf("Received terminal ack\n");
			noTearDownAck = 0;

		}

		alarm(0);
		tries = 0;

	}

	printf("File sent correctly\n");
	close(fd);
	close(sockfd);
	exit(0);

}

/*Receive the file from the server*/
void get_file_client(int sockfd, char* comm, struct sockaddr_in servaddr, float loss_rate){

	time_t intps;
	struct tm* tmi;

	intps = time(NULL);
	tmi = localtime(&intps);

	char directoryNameCat[256] = "Files_Client/";

	char filename[128];
	sprintf(filename,"file.%d.%d.%d.%d.%d.%d.txt",tmi->tm_mday,tmi->tm_mon+1,1900+tmi->tm_year,tmi->tm_hour,tmi->tm_min,tmi->tm_sec);

	strcat(directoryNameCat, filename);

	//char *filename = comm+4;

  	/*check if the file is already in the client directory*/
	/*int retCheck = check_existence(filename);	
	if(retCheck == 1){

    	error("File already in the directory\n");

  	}*/

  	printf("File does not exists, continuing\n");

  	int fd;

	char data[8192];
	int base = -2;
	int seqNum = 0;

	srand48(2345);

	while(1){

		unsigned int length;

		segmentPacket dataPacket;
		ACKPacket ack;

		char bufferToRecieve[sizeof(struct segmentPacket)];

		int n = recvfrom(sockfd, &dataPacket, sizeof(dataPacket), 0, (struct sockaddr *)&servaddr, &length);
		if(n < 0){

    		error("Error while receiving from\n");

  		}  

  		memcpy(bufferToRecieve, &dataPacket, sizeof(segmentPacket));

		seqNum = dataPacket.seq_no;

		if(!is_lost(loss_rate)){

    		if((dataPacket.seq_no == 0) && (dataPacket.type == 1)){


    			//printf("Stampo quello che ho ricevuto\n%s\n", dataPacket.data);

    			fd = open(directoryNameCat, O_CREAT|O_WRONLY|O_TRUNC, 0666);
    			if(fd < 0){

    				error("Error while opening file\n");

    			}

      			memset(data, 0, sizeof(data));
      			strcpy(data, dataPacket.data);

      			int n = write(fd, data, CHUNCKSIZE);
      			if(n < 0){

      				error("Error while writing into file\n");

      			}

      			close(fd);

      			base = 0;
      			ack = createACKPacket(2, base);

    		}else if(dataPacket.seq_no == base + 1){

    			fd = open(directoryNameCat, O_WRONLY|O_APPEND, 0666);
    			if(fd < 0){

    				error("Error while opening file\n");

    			}

    			//printf("Stampo quello che ho ricevuto\n%s\n", dataPacket.data);

      			printf("Received subsequent packet %d\n", dataPacket.seq_no);
      			strcpy(data, dataPacket.data);

      			//printf("PRINTO: %d\n", dataPacket.length);

      			int n = write(fd, data, dataPacket.length);

      			if(n < 0){

      				error("Error while writing into file\n");

      			}

      			memset(data, 0, CHUNCKSIZE);
      			base = dataPacket.seq_no;
      			ack = createACKPacket(2, base);

      			close(fd);

    		}else if(dataPacket.type == 1 &&dataPacket.seq_no != base + 1){

      			printf("Received out of sinc packet %d\n", dataPacket.seq_no);
      			ack = createACKPacket(2, base);

    		}

    		if(dataPacket.type == 4 && seqNum == base){

    			base = -1;
    			ack = createACKPacket(8, base);

    		}

    		if(base >= 0){

    			printf("Sending ack %d\n", base);
    			if(sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&servaddr, sizeof(servaddr))<0){

        			error("Error while sending to socket\n");

      			}

    		}else if(base == -1){

    			printf("Received TearDown Packet\n");
    			printf("Sendint Terminal ACK\n");
    			if(sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){

        			error("Error while sending to socket\n");

      			}

    		}

    		if(dataPacket.type == 4 && base == -1){

    			printf("Message received\n");
    			close(fd);
    			memset(data, 0, sizeof(data));
    			break;

    		}

		}else{

    		printf("Simulated lose\n");

  		}

	}

	printf("DUASIDUASIDAUIS\n");

}

void receive_list(int sockfd, struct sockaddr_in servaddr){

	char bufferList[8192];

	unsigned int length = sizeof(servaddr);

	int n = recvfrom(sockfd, bufferList, sizeof(bufferList), 0, (struct sockaddr*)&servaddr, &length);
	if(n < 0){

		error("Error while recieving list\n");

	}

	printf("List of file in server directory:\n\n%s\n\n", bufferList);

	return;

}

int main(int argc, char *argv[]) {

	int   sockfd;
	struct    sockaddr_in   servaddr;
	segmentPacket p;
	struct sigaction sa;
	float loss; 

  	handle_sigchild(&sa);


  	if(argc < 3){

    	fprintf(stderr, "utilizzo: daytime_clientUDP <indirizzo IP  server loss>\n");
    	exit(1);

  	}

  	loss = atof(argv[2]);

  	if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){ /* crea il socket   */

		error("socket");

   	}

	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERVPORT);

   	if(inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0){

    	error("error in inet_pton for %s");

   	}


	pid_t pid;
	char* line;

  /*************************************************************
   * father process waits command from standard input; then,   *
   * creates a new process to execute. If CTRL + D received,   *
   * waits all children; then, father terminates.               *
   *************************************************************/

	while(feof(stdin) == 0){

		char comm[5];

		printf("write command\n");
		line = read_from_stdin();

		if(line == NULL){

			wait(NULL);
			printf("terminated all request; closing connection\n");
			break;

	  	}

		++n_request;
		if(n_request > 5){

			printf("too many request; wait\n");
			continue;

	  	}

		pid = fork();

		if(pid != 0){

			continue;

		}

		if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){ /* create new socket   */

			error("socket");

		}

		if(!request_to_server(sockfd,&p,&servaddr, line)){

			printf("not available server\n");
			exit(EXIT_SUCCESS);

	  	}

		strncpy(comm, line, 4);
		comm[strlen(comm)] = '\0';

		//printf("Stampo comm%s\n", comm);

		if(strncmp(comm, "put", 3) == 0){

			send_file_client(sockfd,line,servaddr);
			break;

	  	}else if((strncmp(comm,"get",3) == 0)){

	  		struct timeval tv1, tv2;
	  		gettimeofday(&tv1, NULL);

			get_file_client(sockfd,line,servaddr, loss);

			gettimeofday(&tv2, NULL);
			printf ("Total time = %f seconds\n",(double) ((tv2.tv_usec - tv1.tv_usec) / 1000000) + (double) (tv2.tv_sec - tv1.tv_sec));

			break;
		  
	  	}else if(strncmp(comm, "list", 4) == 0){

	  		receive_list(sockfd, servaddr);
	  		break;

	  	}else{

	  		fprintf(stderr, "command not recognize. USE COMMAND  LIST or GET/PUT FOLLOWEWD BY A PROPER FILE NAME \n");
	  		break;

	  	}

    }

    printf("closing connection\n");

    exit(EXIT_SUCCESS);

}
