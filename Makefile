PROXY_OBJECTS=proxy.o network.o
SERVER_OBJECTS=server.o network.o
CFLAGS=-fPIC -Wall -Wextra -std=gnu11 -Wpointer-arith -g
LDFLAGS=-fPIC -ldl

all: libremotec.so server

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libremotec.so: $(PROXY_OBJECTS)
	$(CC) $(LDFLAGS) -shared $(PROXY_OBJECTS) -o $@

server: $(SERVER_OBJECTS)
	$(CC) $(LDFLAGS) $(SERVER_OBJECTS) -o $@

clean:
	rm -f $(PROXY_OBJECTS) $(SERVER_OBJECTS) libremotec.so server

.PHONY: all clean
