
CFLAGS=-Wall -Werror -O2
CROSS_COMPILE ?= 
CC = $(CROSS_COMPILE)gcc


#Default target: build the writer application
writer: writer.o
	$(CC) $(CFLAGS) -o $@ $^

#Compile writer.o from writer.c
writer.o: writer.c
	$(CC) $(CFLAGS) -c -o $@ $<

#Clean Target: Remove the application and object files
clean:
	rm -f writer *.o



