#pragma once
#ifndef FILE_CACHE_READER_HPP
#define FILE_CACHE_READER_HPP

#include <atomic>
#include <climits>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <sys/inotify.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// Buffer length macro required for inotify read events
#define EVENT_BUF_LEN (1024 * (sizeof(struct inotify_event) + NAME_MAX + 1))

class AsyncMultiFileWatcher {
public:
  // Callback signature: (event_mask, full_file_path)
  using EventCallback =
      std::function<void(uint32_t mask, const std::string &file_path)>;

  explicit AsyncMultiFileWatcher(EventCallback callback);
  ~AsyncMultiFileWatcher();

  // Add as many files as you want (can be in the same or different directories)
  bool addFile(const std::string &file_path);

  bool start();
  void stop();

private:
  void monitorLoop();

  EventCallback callback_;
  std::atomic<bool> running_;
  int inotify_fd_;
  std::thread worker_thread_;

  std::mutex mutex_;
  // Maps Watch Descriptor (wd) -> Directory path (e.g., 1 -> "/home/me/test")
  std::unordered_map<int, std::string> watch_dirs_;
  // Maps Watch Descriptor (wd) -> Set of target filenames in that directory
  std::unordered_map<int, std::unordered_set<std::string>> watched_files_;
};

#endif
