#include "system_resources.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>
#include <thread>

unsigned long long LinuxSystemMonitor::CpuTimes::idleAll() const {
  return idle + iowait;
}

unsigned long long LinuxSystemMonitor::CpuTimes::nonIdle() const {
  return user + nice + system + irq + softirq + steal;
}

unsigned long long LinuxSystemMonitor::CpuTimes::total() const {
  return idleAll() + nonIdle();
}

LinuxSystemMonitor::CpuTimes LinuxSystemMonitor::readCpuTimes() {
  std::ifstream statFile("/proc/stat");
  std::string line;
  CpuTimes times = {0, 0, 0, 0, 0, 0, 0, 0};

  if (std::getline(statFile, line)) {
    if (line.rfind("cpu ", 0) == 0) {
      std::istringstream ss(line);
      std::string cpu;
      ss >> cpu >> times.user >> times.nice >> times.system >> times.idle >>
          times.iowait >> times.irq >> times.softirq >> times.steal;
    }
  }
  return times;
}

double LinuxSystemMonitor::getRamUsagePercent() {
  std::ifstream meminfo("/proc/meminfo");
  std::string line;
  unsigned long long totalMem = 0;
  unsigned long long availableMem = 0;

  while (std::getline(meminfo, line)) {
    if (line.find("MemTotal:") == 0) {
      std::istringstream ss(line);
      std::string key;
      ss >> key >> totalMem;
    } else if (line.find("MemAvailable:") == 0) {
      std::istringstream ss(line);
      std::string key;
      ss >> key >> availableMem;
    }
    if (totalMem > 0 && availableMem > 0)
      break;
  }

  if (totalMem == 0)
    return 0.0;
  unsigned long long usedMem = totalMem - availableMem;
  return (static_cast<double>(usedMem) / totalMem) * 100.0;
}

double LinuxSystemMonitor::getCpuUsagePercent(int intervalMs) {
  CpuTimes t1 = readCpuTimes();
  std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
  CpuTimes t2 = readCpuTimes();

  unsigned long long total1 = t1.total();
  unsigned long long total2 = t2.total();
  unsigned long long idle1 = t1.idleAll();
  unsigned long long idle2 = t2.idleAll();

  unsigned long long totalDelta = total2 - total1;
  unsigned long long idleDelta = idle2 - idle1;

  if (totalDelta == 0)
    return 0.0;

  double usage =
      (static_cast<double>(totalDelta - idleDelta) / totalDelta) * 100.0;
  return usage;
}

double LinuxSystemMonitor::getDiskUsagePercent(const std::string &path) {
  struct statvfs stat;
  if (statvfs(path.c_str(), &stat) != 0) {
    return 0.0;
  }

  unsigned long long totalBlocks = stat.f_blocks;
  unsigned long long freeBlocks =
      stat.f_bavail; // f_bavail accounts for non-super-user blocks

  if (totalBlocks == 0)
    return 0.0;

  unsigned long long usedBlocks = totalBlocks - freeBlocks;
  return (static_cast<double>(usedBlocks) / totalBlocks) * 100.0;
}
