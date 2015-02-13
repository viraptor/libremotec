PROXY_OBJECTS=proxy.o network.o
SERVER_OBJECTS=server.o network.o
CFLAGS=-fPIC -Wall -Wextra -std=gnu11
LDFLAGS=-fPIC -ldl

all: libremotec.so server test

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libremotec.so: $(PROXY_OBJECTS)
	$(CC) $(LDFLAGS) -shared $(PROXY_OBJECTS) -o $@

server: $(SERVER_OBJECTS)
	$(CC) $(LDFLAGS) $(SERVER_OBJECTS) -o $@

clean:
	rm -f $(PROXY_OBJECTS) $(SERVER_OBJECTS) libremotec.so server

.PHONY: all clean
