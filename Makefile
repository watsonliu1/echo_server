# 指定C++编译器为g++
CC = g++

# 编译选项：
# -std=c++14：使用C++14标准编译
# -Wall：开启所有警告信息，便于代码调试
# -O2：开启二级优化，提升程序运行效率
# -pthread：启用线程库支持（适配多线程代码）
CFLAGS = -std=c++14 -Wall -O2 -pthread

# 链接选项：-pthread确保链接时包含线程库（与编译选项对应）
LDFLAGS = -pthread

# ========== 关键修改点说明 ==========
# 1. server_global.o → server_stats.o：原全局统计相关的目标文件改名，对应源文件命名调整
# 2. performance_monitor.o → client_performance_monitor.o：客户端性能监控目标文件改名，区分服务端/客户端监控模块
# 定义服务器端编译所需的目标文件列表：
# echo_server.o：服务器核心类实现文件
# server_performance_monitor.o：服务器性能监控模块
# server_stats.o：服务器全局统计模块（原server_global.o）
# server_main.o：服务器主函数入口文件
SERVER_OBJS = echo_server.o server_performance_monitor.o server_stats.o server_main.o

# 定义客户端编译所需的目标文件列表：
# echo_client.o：客户端核心类实现文件
# client_main.o：客户端主函数入口文件
# client_performance_monitor.o：客户端性能监控模块（原performance_monitor.o）
CLIENT_OBJS = echo_client.o client_main.o client_performance_monitor.o

# 定义最终生成的可执行文件名称（服务器+客户端）
TARGETS = echo_server echo_client

# 默认目标：执行make时默认编译所有可执行文件
all: $(TARGETS)

# 服务器可执行文件编译规则：
# 依赖：SERVER_OBJS中定义的所有目标文件
# 命令：使用指定编译器和链接选项，将依赖文件链接为echo_server可执行文件
# $@：自动替换为目标名（echo_server），$^：自动替换为所有依赖文件
echo_server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 客户端可执行文件编译规则：
# 依赖：CLIENT_OBJS中定义的所有目标文件
# 命令：将依赖文件链接为echo_client可执行文件
echo_client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 通用编译规则（模式匹配）：
# 匹配所有.cpp文件，自动生成对应的.o目标文件
# $<：自动替换为依赖的.cpp源文件，$@：自动替换为目标.o文件
%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

# 清理规则：执行make clean时清理编译产物
# 删除目标文件、可执行文件、日志文件，以及历史改名的目标文件（兼容旧版本）
clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(TARGETS) *.log server_global.o performance_monitor.o