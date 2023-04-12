TARGET_EXEC=writer
CFLAGS=-Wall 
CROSS_COMPILE=aarch64_be-none-linux-gnu-
OBJECTS=writer.o
CC=$(CROSS_COMPILE)gcc
NATIVE_CC=clang
NATIVE_CFLAGS=-lamdlibm -lm
ARCH=znver2


#all:writer.c
#	$(CC) $(CFLAGS) -c -o writer $^

all:writer.c
	$(NATIVE_CC) -march=$(ARCH) $^ $(NATIVE_CFLAGS) -o $(TARGET_EXEC) 

clean:	
	rm -rf *.o $(TARGET_EXEC)




