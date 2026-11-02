#pragma once
#ifndef SYSTEM_MONITOR_HPP
#define SYSTEM_MONITOR_HPP

#include <string>

class LinuxSystemMonitor {
private:
  struct CpuTimes {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;

    unsigned long long idleAll() const;
    unsigned long long nonIdle() const;
    unsigned long long total() const;
  };

  CpuTimes readCpuTimes();

public:
  // Returns RAM usage percentage (0.0 to 100.0)
  double getRamUsagePercent();

  // Returns CPU usage percentage over a brief sampling interval (default 100ms)
  double getCpuUsagePercent(int intervalMs = 100);

  // Returns Disk usage percentage for a specified path (default root "/")
  double getDiskUsagePercent(const std::string &path = "/");
};

#endif
