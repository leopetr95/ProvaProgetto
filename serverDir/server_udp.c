#include "basic.h"
#include "data_types.h"
#include "common.h"

#define BUFFER_SIZE 1200
#define WINDOWSIZE 4
#define TIMEOUT 3

extern int n_win;
int adaptive;

sem_t *sem;
int *available_proc;

/*Sets up signal handler to avoid zombi processes*/
void sighandler(int sign){

	(void)sign;
	int status;
	pid_t pid;

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

    sem_close(sem);
    sem_unlink("sem");

}

/*Initializes socket*/
void initialize_socket(int* sock_fd,struct sockaddr_in* s){

	int sockfd;
	if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){

		error("errore in socket");

	}

	if(bind(sockfd, (struct sockaddr *)s, sizeof(*s)) < 0){

		error("error in bind");

	}

	*sock_fd = sockfd;


}

/*Creates a shared memory for child processes*/
int create_shared_mem(){

	key_t key = ftok(".", 'b');
	if(key == -1){

		error("Errore nella ftok shared memory\n");

	}

	int ret = shmget(key, sizeof(int), IPC_CREAT | 0666);
	if(ret == -1){

		error("Errore nella shmget\n");

	}

	return ret;

}

/*Creates a message queue for the requests executed by child processes*/
int create_queue(){

	key_t key = ftok(".", 'a');
	if(key == -1){

		error("Errore nella ftok queue\n");

	}

	int ret = msgget(key, IPC_CREAT | 0666);
	if(ret == -1){

		error("Errore nella msgget\n");

	}

	return ret;

}

/*Listens the requests from the client and returns the string containing the command and the file*/
char* listen_request(int sockfd,segmentPacket* p,struct sockaddr_in* addr,socklen_t* len){

	char* comando;
    struct sockaddr_in servaddr = *addr;
    socklen_t l = *len;
    l = sizeof(servaddr);
    printf("listening request\n");

    int returnRec = recvfrom(sockfd, p, sizeof(Header), 0, (struct sockaddr *)&servaddr, &l);
    if(returnRec < 0){

    	error("recvfrom\n");

    }

    comando = p->data;

    *addr = servaddr;
    *len = l;
    return comando;

}

/*Generates the list of file in server directory*/
char* list_file_server(){

	FILE* proc = popen("ls Files_Server", "r");
	if(proc == NULL){

		perror("Error while popen\n");
		exit(1);

	}

	int c;
	int i = 0;
	char *buff = malloc(BUFFER_SIZE*sizeof(char));

	while(( c = fgetc(proc)) != EOF && i < BUFFER_SIZE){

		buff[i++] = c;

	}

	buff[i] = 0;
	pclose(proc);

	return buff;

}

/*Send the requested file to the client after the command get*/
void send_file_server(char *filename, int sockfd, struct sockaddr_in servaddr){

	printf("Sono dentro a send_file_server\n");

	char directoryNameCat[256] = "Files_Server/";

	strcat(directoryNameCat, filename);

	/*Check if the file to send is in the server directory*/
	int retCheck = check_existence(directoryNameCat);
	if(retCheck == 0){

		perror("Error while checking existence file\n");
		exit(1);

	}

	/*opening file to send*/
	int fd = open(directoryNameCat, O_RDONLY);
	if(fd < 0){

		perror("Error while opening file\n");
		exit(1);

	}

	/*acquiring stats of the file*/
	struct stat st;
	int count = stat(directoryNameCat, &st);
	if(count < 0){

		perror("Error while acquiring stats of file\n");
		exit(1);

	}

	/*getting file size*/
	int sz = st.st_size;

	int tries = 0;

	int numberOfSegments = sz / CHUNCKSIZE;

	/*if there are leftovers*/
	if(sz % CHUNCKSIZE > 0){

		numberOfSegments++;

	}

	//setting window parameter
	int base = -1;	//highest segment ACK received
	int seqNum = 0;	//highest segment sent, reset by base
	int dataLenght = 0;	//CHUNCKSIZE size
	int windowSize = WINDOWSIZE;
	unsigned int fromSize;

	int noTearDownAck = 1;

	while(noTearDownAck){

		//send packets from base up to window size
		while(seqNum <= numberOfSegments && (seqNum - base) <= windowSize){

			struct segmentPacket dataPacket;

			if(seqNum == numberOfSegments){

				dataPacket = createFinalPacket(seqNum, 0);
				printf("Sending final packet\n");

			}else{

				char data[CHUNCKSIZE];
				memset(data, 0, CHUNCKSIZE + 1);
				int retRead = read(fd, data, CHUNCKSIZE);

				dataLenght = retRead;

				if(retRead < 0){

					perror("Error while reading from file\n");
					exit(1);

				}

				printf("Stampo quello che ho letto dal file: \n%s\n", data);

				dataPacket = createDataPacket(seqNum, dataLenght, data);
				printf("Sending packet: %d\n", seqNum);

			}

			char bufferToSend[sizeof(struct segmentPacket)];
			memcpy(bufferToSend, &dataPacket, sizeof(struct segmentPacket));

			if(sendto(sockfd, bufferToSend, sizeof(bufferToSend), 0, (struct sockaddr *)&servaddr, sizeof(servaddr))<0){

				perror("Error while sending packet\n");
				exit(1);

			}

			seqNum++;

		}

		alarm(TIMEOUT);

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

						if(seqNum == numberOfSegments){

							dataPacket = createFinalPacket(seqNum, 0);
							printf("Sending final packet");

						}else{

							char data[CHUNCKSIZE];

							lseek(fd, seqNum * CHUNCKSIZE, SEEK_SET);

							int retRead = read(fd, data, CHUNCKSIZE);
							if(retRead < 0){

								perror("Error while reading from file\n");
								exit(1);

							}

							dataLenght = retRead;

							dataPacket = createDataPacket(seqNum, dataLenght, data);
							printf("Sending packet: %d\n", seqNum);

						}

						char bufferToSend[sizeof(struct segmentPacket)];
						memcpy(bufferToSend, &dataPacket, sizeof(struct segmentPacket));

						if(sendto(sockfd, bufferToSend, sizeof(bufferToSend), 0, (struct sockaddr *)&servaddr, sizeof(servaddr))<0){

							perror("Error while sending to socket\n");
							exit(1);

						}

						seqNum++;

					}

					alarm(TIMEOUT);

				}

				tries++;

			}else{

				perror("Error while recvrom\n");
				exit(1);

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

	close(sockfd);
	exit(0);

}

/*Recieve the file from the client after the command put*/
void get_file_server(int sockfd, char* comm, struct sockaddr_in servaddr, float loss_rate){

	printf("Sono dentro get_file_server\n");

	time_t intps;
	struct tm* tmi;

	intps = time(NULL);
	tmi = localtime(&intps);

	char directoryNameCat[256] = "Files_Server/";

	char filename[128]; 

	sprintf(filename,"file.%d.%d.%d.%d.%d.%d.txt",tmi->tm_mday,tmi->tm_mon+1,1900+tmi->tm_year,tmi->tm_hour,tmi->tm_min,tmi->tm_sec);

	strcat(directoryNameCat, filename);

	//char *filename = comm+4;

   /*check if the file is already in the server directory*/
	/*int retCheck = check_existence(filename);	
	if(retCheck == 0){

    	perror("Error while checking existence\n");
    	exit(1);	

  	}*/

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

    		perror("Error while receiving from\n");
    		exit(1);

  		}  

  		printf("Printo quello che ho ricevuto %s\n", dataPacket.data);


  		memcpy(bufferToRecieve, &dataPacket, sizeof(segmentPacket));

		seqNum = dataPacket.seq_no;

		if(!is_lost(loss_rate)){

    		if(dataPacket.seq_no == 0 && dataPacket.type == 1){

    			fd = open(directoryNameCat, O_CREAT|O_WRONLY|O_TRUNC, 0666);
    			if(fd < 0){

    				perror("Error while opening file\n");
    				exit(1);

    			}

      			memset(data, 0, sizeof(data));
      			strcpy(data, dataPacket.data);

      			int n = write(fd, data, CHUNCKSIZE);
      			if(n < 0){

      				perror("Error while writing into file\n");
      				exit(1);

      			}

      			close(fd);

      			base = 0;
      			ack = createACKPacket(2, base);

    		}else if(dataPacket.seq_no == base + 1){

    			fd = open(directoryNameCat, O_WRONLY|O_APPEND, 0666);
    			if(fd < 0){

    				perror("Error while opening file\n");
    				exit(1);

    			}

      			printf("Received subsequent packet %d\n", dataPacket.seq_no);
      			strcpy(data, dataPacket.data);

      			int n = write(fd, data, dataPacket.length);
      			if(n < 0){

      				perror("Error while writing into file\n");
      				exit(1);

      			}

      			memset(data, 0, CHUNCKSIZE);
      			base = dataPacket.seq_no;
      			ack = createACKPacket(2, base);

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

        			perror("Error while sending to socketto\n");
        			exit(1);

      			}

    		}else if(base == -1){

    			printf("Received TearDown Packet\n");
    			printf("Sendint Terminal ACK\n");
    			if(sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){

        			perror("Error while sending to socket\n");
        			exit(1);

      			}

    		}

    		if(dataPacket.type == 4 && base == -1){

    			printf("Message received\n");
    			close(fd);
    			memset(data, 0, sizeof(data));

    		}

		}else{

    		printf("Simulated lose\n");

  		}

	}

}

/*According to the recieved command the corresponding function is activated*/
void manage_client(int sockfd,struct msgbuf msg){

	printf("Sono dentro manage_client\n");

	/*The recieved string contains both the command and the file to be sent/put*/
    char comm[256];

    strcpy(comm, msg.command);

    /*Compare the first 3 char of the command with the string put*/
	if(strncmp(comm, "put", 3) == 0){

		//l'ultimo parametro è il loss rate, da impostare

		printf("Sono nella put\n");
		get_file_server(sockfd, comm, msg.s, msg.loss);

	}
	/*Compare the first 4 char of the command with the string list*/
	else if((strncmp(comm,"list",4) == 0)){

		char* returnList = list_file_server();

		int retsend = sendto(sockfd, returnList, strlen(returnList), 0, (struct sockaddr *)&msg.s, sizeof(msg.s));
		if(retsend < 0){

			perror("Error while sending list\n");
			exit(1);

		}

	/*Compare the first 3 char of the command with the string get*/
	}else if((strncmp(comm,"get",3) == 0)){

		printf("Sono nella get\n");
		send_file_server(comm + 4, sockfd, msg.s);

	}

	printf("end request\n");

}

/*************************************************************************************************
 * Child process waits on message queue; when father writes on queue, child executes		     *
 * client request. A child process executes 5 request, then terminates.							 *
 *************************************************************************************************/

void child_job(int queue_id,int shared_id,pid_t pid){

	printf("I am in child job\n");

	(void)pid;
	segmentPacket p;
	struct sockaddr_in addr;
	int sockfd;
	struct msgbuf msg;

	msg.mtype = 1;

	/*Attaching the child process to the shared memory*/
	available_proc = shmat(shared_id, NULL, 0);
	if(available_proc == (void *)-1){

		error("Errore nella shmat\n");

	}

	while(1){

		/*The size to needed by the msgrcv is the size of the struct msgbuf 
		except the field mtype*/
		long msgsz = sizeof(struct msgbuf) - sizeof(long);

		/*recieve a request previously added in the message queue by the father
		and stores it in the struct msgbuf*/
		if(msgrcv(queue_id, &msg, msgsz,1,0) == -1){

			error("msgrcv");

		}

		sem_wait(sem);
		--available_proc;
		sem_post(sem);

		memset((void *)&addr,0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(0);

		initialize_socket(&sockfd,&addr);				//every child process creates a new socket

		p.seq_no = -1;
		p.ack_no = msg.client_seq;

		if(sendto(sockfd, &p, sizeof(segmentPacket), 0, (struct sockaddr *)&msg.s, sizeof(msg.s)) < 0){			/*connection ack*/

			error("sendto");

		}

		/*Execute the request*/
		manage_client(sockfd,msg);	

		sem_wait(sem);
		available_proc++;
		sem_post(sem);

	}

	exit(EXIT_SUCCESS);

}

/*Creates 10 child processes that will execute the requests*/
void prefork(int queue_id, int shm_id){

	pid_t pid;

	for(int i = 0; i < 10; i++){

		pid = fork();
		if(pid == -1){

			error("Errore nella fork\n");

		}else if(pid == 0){

			child_job(queue_id, shm_id, getpid());

		}

	}

	return; 

}

/*Message queue is filled with requests from client*/
void write_on_queue(int queue_id,struct sockaddr_in s, segmentPacket p, char* recievedCommand, float loss){

	struct msgbuf msg;
	msg.s = s;
	msg.client_seq = p.seq_no;
	msg.loss = loss;

	strcpy(msg.command, recievedCommand);
	long size = sizeof(struct msgbuf) - sizeof(long);
	msg.mtype = 1;

	if(msgsnd(queue_id, &msg, size, 0) == -1){

		error("msgsnd");

	}
}


int main(int argc, char **argv){

	(void) argc;
	(void) argv;
	int sockfd;
	int shared_id;
	socklen_t len;
	struct sockaddr_in addr;
	segmentPacket p;
	struct sigaction sa;
	float loss;

	char *recievedCommand;

	handle_sigchild(&sa);					/*handle SIGHCLD to avoid zombie processes*/

	srand(time(NULL));

	if(argc < 2){

		fprintf(stderr, "Usage: %s lossRate\n", argv[0]);
		exit(1);

	}

	loss = atof(argv[1]);

  /*
   * Create a message queue where send client data to child processes, in particular
   *  IP and port number memorized in struct sockaddr_in by recvfrom if argument is
   *  not NULL
   */
	int queue_id = create_queue();


  /*
   * Create shared memory, where set variable with number of available processes, and a semaphore
   */
	shared_id = create_shared_mem();

	available_proc = shmat(shared_id, NULL, 0);

	if(available_proc == (void *)-1){

  		error("Errore nella shmat\n");

	}


	sem = sem_open("sem", O_CREAT | O_EXCL, 0666, 1);
	if(sem == SEM_FAILED){

  		error("Errore in sem_open\n");

	}

	*available_proc = 10;						//initial number of processes

	prefork(queue_id,shared_id);		//create processes and passing memory id and queue id

	memset((void *)&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVPORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){		//create listen socket

    	error("errore in socket\n");

	}

	if(bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0){

  		error("Errore in bind\n");

	}

	while(1){

  		/*Acquiriquing the request from client and storing the command*/
  		recievedCommand = listen_request(sockfd, &p, &addr, &len);
  		/*Writing requests in the message queue*/
		write_on_queue(queue_id,addr, p, recievedCommand, loss);					//write on queue message client data
	  
  	}

  	wait(NULL);
  	return 0;

}
