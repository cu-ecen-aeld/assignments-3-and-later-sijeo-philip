

CFLAGS ?= -Wall -Werror -O2
CC ?= gcc

TARGET = aesdsocket

# The default target compiles the executable.

all: $(TARGET)

# Compile the aesdsocket executable from the aesdsocket.c source file.
$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) -o $(TARGET) aesdsocket.c $(LDFLAGS)

# Clean up the build artifacts

clean:
	rm -f $(TARGET)

.PHONY: all clean

