# 编译器与编译选项
CC := gcc
CXX := g++
CFLAGS := -Wall -Wextra -O2 -std=c11  # C语言编译选项：开启警告、优化、C11标准
CXXFLAGS := -Wall -Wextra -O2 -std=c++11  # C++编译选项：C++11标准
LDFLAGS := -lpthread  # 链接线程库（客户端多线程需要）

# 源文件与目标文件
SERVER_SRC := echo_server.cpp  # 服务器源文件（假设为C++）
CLIENT_SRC := echo_client.cpp  # 客户端源文件（假设为C++）
SERVER_OBJ := $(SERVER_SRC:.cpp=.o)
CLIENT_OBJ := $(CLIENT_SRC:.cpp=.o)

# 二进制文件名称
SERVER_BIN := echo_server
CLIENT_BIN := echo_client

# 默认目标：编译服务器和客户端
all: $(SERVER_BIN) $(CLIENT_BIN)

# 编译服务器
$(SERVER_BIN): $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# 编译客户端
$(CLIENT_BIN): $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# 生成目标文件（.o）
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# 清理目标文件和二进制文件
clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) $(SERVER_BIN) $(CLIENT_BIN)

# 伪目标：避免与同名文件冲突
.PHONY: all clean