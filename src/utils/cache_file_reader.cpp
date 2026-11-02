#include "cache_file_reader.hpp"
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <unistd.h>

AsyncMultiFileWatcher::AsyncMultiFileWatcher(EventCallback callback)
    : callback_(std::move(callback)), running_(false), inotify_fd_(-1) {

  // Initialize inotify immediately so we can add files before or after start()
  inotify_fd_ = inotify_init1(IN_NONBLOCK);
  if (inotify_fd_ < 0) {
    std::cerr << "Error: Failed to initialize inotify." << std::endl;
  }
}

AsyncMultiFileWatcher::~AsyncMultiFileWatcher() {
  stop();
  if (inotify_fd_ >= 0) {
    close(inotify_fd_);
  }
}

bool AsyncMultiFileWatcher::addFile(const std::string &file_path) {
  if (inotify_fd_ < 0)
    return false;

  std::filesystem::path p(file_path);
  std::string dir = (p.has_parent_path() && !p.parent_path().empty())
                        ? p.parent_path().string()
                        : ".";
  std::string filename = p.filename().string();

  // Watch the parent directory (if already watched, inotify returns the
  // existing wd)
  int wd = inotify_add_watch(inotify_fd_, dir.c_str(),
                             IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO |
                                 IN_MOVED_FROM);

  if (wd < 0) {
    std::cerr << "Error: Failed to watch directory: " << dir << std::endl;
    return false;
  }

  // Lock mutex so we can safely add files even while the background thread is
  // running
  std::lock_guard<std::mutex> lock(mutex_);
  watch_dirs_[wd] = dir;
  watched_files_[wd].insert(filename);
  return true;
}

bool AsyncMultiFileWatcher::start() {
  if (running_ || inotify_fd_ < 0)
    return false;
  running_ = true;
  worker_thread_ = std::thread(&AsyncMultiFileWatcher::monitorLoop, this);
  return true;
}

void AsyncMultiFileWatcher::stop() {
  if (!running_)
    return;
  running_ = false;

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

void AsyncMultiFileWatcher::monitorLoop() {
  char buffer[EVENT_BUF_LEN];

  while (running_) {
    int length = read(inotify_fd_, buffer, EVENT_BUF_LEN);
    if (length < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      break;
    }

    int i = 0;
    while (i < length) {
      struct inotify_event *event = (struct inotify_event *)&buffer[i];

      if (event->len > 0 && callback_) {
        std::string current_file(event->name);
        std::string matched_full_path;

        // Check if this event's wd and filename match any of our target files
        {
          std::lock_guard<std::mutex> lock(mutex_);
          auto it = watched_files_.find(event->wd);
          if (it != watched_files_.end() && it->second.count(current_file)) {
            std::string dir = watch_dirs_[event->wd];
            matched_full_path =
                (dir == ".") ? current_file : (dir + "/" + current_file);
          }
        }

        // Trigger callback outside the mutex lock to avoid deadlocks
        if (!matched_full_path.empty()) {
          callback_(event->mask, matched_full_path);
        }
      }

      i += sizeof(struct inotify_event) + event->len;
    }
  }
}
