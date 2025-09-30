# Compiler and flags
CC      = gcc
CFLAGS  = -Wall -Wextra -O0 -g

# Targets
TARGET  = set-leds
SRCS    = set-leds.c
OBJS    = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Link the program
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile source files into objects
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

# Run the program
run: $(TARGET)
	./$(TARGET)

# Run under gdbserver for remote debugging
debug: $(TARGET)
	sudo gdbserver localhost:1234 ./$(TARGET) --localconfig

.PHONY: all clean run debug
