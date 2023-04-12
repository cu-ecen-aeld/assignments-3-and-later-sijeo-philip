TARGET_EXEC=writer
CFLAGS=-Wall 
#CROSS_COMPILE=aarch64_be-none-linux-gnu-
OBJECTS=writer.o
CC=gcc
NATIVE_CC=gcc
NATIVE_CFLAGS=-lamdlibm -lm
ARCH=znver2


#all:writer.c
#	$(CC) $(CFLAGS) -c -o writer $^
#	chmod 777 writer

all:writer.c
	$(NATIVE_CC) -march=$(ARCH) $^ $(NATIVE_CFLAGS) -o $(TARGET_EXEC)
	chmod 777 writer


clean:	
	rm -rf *.o $(TARGET_EXEC)




