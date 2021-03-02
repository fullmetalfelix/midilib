
CC      = gcc
CCFLAGS = -O3 -fPIC -DDEBUG
LFLAGS  = -lm

SRC = midilib.c

all:
	$(CC) $(CCFLAGS) --shared -o libmidilib.so $(SRC) $(LFLAGS)


clean:
	rm -f *.o
