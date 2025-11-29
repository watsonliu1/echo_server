CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -g
LDFLAGS = -pthread

# 源文件
SERVER_SRCS = logger.cpp config.cpp thread_pool.cpp performance_monitor.cpp echo_server.cpp server_main.cpp
CLIENT_SRCS = logger.cpp config.cpp echo_client.cpp client_main.cpp

# 目标文件
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.cpp=.o)

# 可执行文件
SERVER_TARGET = echo_server
CLIENT_TARGET = echo_client

all: $(SERVER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(SERVER_TARGET) $(CLIENT_TARGET) *.log