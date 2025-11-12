# 定义编译器（g++）
CXX = g++
# 编译选项：
# -std=c++14：使用C++14标准
# -Wall：开启所有警告
# -O2：开启二级优化
# -pthread：启用多线程支持
CXXFLAGS = -std=c++14 -Wall -O2 -pthread
# 链接选项：启用多线程支持
LDFLAGS = -pthread

# 定义目标文件（服务器和客户端可执行文件）
SERVER_TARGET = echo_server
CLIENT_TARGET = echo_client

# 定义服务器和客户端的目标文件（.o）
SERVER_OBJS = echo_server.o server_main.o
CLIENT_OBJS = echo_client.o client_main.o

# 默认目标：编译所有（服务器和客户端）
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# 编译服务器可执行文件：依赖服务器目标文件
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)  # $@：目标文件，$^：所有依赖文件

# 编译客户端可执行文件：依赖客户端目标文件
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# 编译规则：将.cpp文件编译为.o目标文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@  # $<：第一个依赖文件（.cpp），$@：目标文件（.o）

# 清理目标：删除可执行文件和目标文件
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET) $(SERVER_OBJS) $(CLIENT_OBJS)

# 声明伪目标（避免与同名文件冲突）
.PHONY: all clean