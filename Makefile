CC = gcc
CFLAGS = -O3 -Wall -Wextra
DEPS = common.h data_types.h basic.h
COMMONOBJ = common.o 

COBJ = client_udp.o
SOBJ = server_udp.o
LIBS = -lrt -pthread
CEXE = client_udp
SEXE = server_udp

%.o : %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

server : $(COMMONOBJ) $(SOBJ)
	$(CC) $(LIBS) -o $(SEXE) $^ $(CFLAGS)

client: $(COMMONOBJ) $(COBJ)
	$(CC) $(LIBS) -o $(CEXE) $^ $(CFLAGS)

 

all: server client

.PHONY : all test clean

clean:

	rm -f $(COBJ) $(SOBJ) $(COMMONOBJ) $(SEXE) $(CEXE)
