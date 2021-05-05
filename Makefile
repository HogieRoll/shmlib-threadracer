CC=gcc
IDIR=src/include
CFLAGS=-I$(IDIR)

ODIR=build
OBJ=shm_racer.o shmlib.o
LIBS=-lpthread -lbsd -lm

%.o: src/%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

shm_racer: $(OBJ) 
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)
