#include "common.h"
#include <atomic>

// 服务器全局统计变量（原子变量保证多线程安全）
std::atomic<long long> g_total_processed_msgs(0); // 总处理消息数
std::atomic<int> g_active_connections(0);         // 当前活跃连接数