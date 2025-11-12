CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -pthread
LDFLAGS = -pthread

SERVER_TARGET = echo_server
CLIENT_TARGET = echo_client

SERVER_OBJS = echo_server.o server_main.o
CLIENT_OBJS = echo_client.o client_main.o

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET) $(SERVER_OBJS) $(CLIENT_OBJS)

.PHONY: all clean