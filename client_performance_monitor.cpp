#include "client_performance_monitor.h"
#include "common.h"
#include <fstream>
#include <sstream>
#include <sys/resource.h>
#include <unistd.h>

PerformanceMonitor::PerformanceMonitor() {
  start_time = std::chrono::high_resolution_clock::now();
  initial_cpu_usage = get_cpu_usage();
  initial_memory_kb = get_memory_usage_kb();
}

double PerformanceMonitor::get_cpu_usage() {
  std::ifstream proc_stat("/proc/self/stat");
  if (!proc_stat.is_open())
    return 0;

  std::string line;
  std::getline(proc_stat, line);
  std::istringstream iss(line);

  int pid;
  std::string comm;
  char state;
  int ppid, pgrp, session, tty_nr, tpgid;
  unsigned int flags;
  unsigned long minflt, cminflt, majflt, cmajflt;
  unsigned long utime, stime;

  iss >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >>
      flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime;

  static const double clock_ticks = sysconf(_SC_CLK_TCK);
  return (utime + stime) / clock_ticks;
}

size_t PerformanceMonitor::get_memory_usage_kb() {
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);
  return usage.ru_maxrss;
}

void PerformanceMonitor::log_current_stats(const std::string &thread_id,
                                           int msg_count) {
  auto now = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = now - start_time;

  double current_cpu = get_cpu_usage() - initial_cpu_usage;
  size_t current_memory = get_memory_usage_kb() - initial_memory_kb;

  log_info("[Thread " + thread_id +
           "] "
           "Elapsed: " +
           std::to_string(elapsed.count()) +
           "s | "
           "CPU: " +
           std::to_string(current_cpu) +
           "s | "
           "Memory: " +
           std::to_string(current_memory) +
           "KB | "
           "Sent: " +
           std::to_string(msg_count));
}