CC = g++
CFLAGS = -std=c++14 -Wall -O2 -pthread
LDFLAGS = -pthread

# ========== 关键修改点 ==========
# 1. server_global.o → server_stats.o（对应文件改名）
# 2. performance_monitor.o → client_performance_monitor.o（可选，区分客户端监控）
SERVER_OBJS = echo_server.o server_performance_monitor.o server_stats.o server_main.o
CLIENT_OBJS = echo_client.o client_main.o client_performance_monitor.o

# 可执行文件
TARGETS = echo_server echo_client

all: $(TARGETS)

# 服务器编译
echo_server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 客户端编译
echo_client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 通用编译规则（自动匹配.cpp生成.o）
%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

# 清理：新增可能的改名文件（如server_stats.o、client_performance_monitor.o）
clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(TARGETS) *.log server_global.o performance_monitor.o