#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace arcana_client {
class NetworkClient {
  struct Impl; std::unique_ptr<Impl> impl_;
public:
  NetworkClient(); ~NetworkClient();
  NetworkClient(const NetworkClient&)=delete; NetworkClient& operator=(const NetworkClient&)=delete;
  void connect(const std::string& host,const std::string& port,const std::string& path="/");
  void send(std::string text);
  std::vector<std::string> poll();
  bool connected() const;
  std::string error() const;
};
}
