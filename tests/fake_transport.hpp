#pragma once
// In-memory Transport for the online tests: the test plays the server by pushing events into a
// wire and reading what the client sent.
#include "transport.hpp"

#include <memory>
#include <string>
#include <vector>

namespace arcana::online::testing {

struct FakeWire {
  std::string url;
  std::vector<std::string> sent;
  std::vector<TransportEvent> pending;
  bool closed{};
};

class FakeTransport final : public Transport {
public:
  explicit FakeTransport(std::shared_ptr<FakeWire> wire) : wire_(std::move(wire)) {}
  void open(const std::string& url) override { wire_->url = url; }
  void send(std::string text) override { wire_->sent.push_back(std::move(text)); }
  void close() override { wire_->closed = true; }
  void poll(std::vector<TransportEvent>& out) override {
    out.insert(out.end(), wire_->pending.begin(), wire_->pending.end());
    wire_->pending.clear();
  }

private:
  std::shared_ptr<FakeWire> wire_;
};

// Every connection the code under test opens gets its own wire, in order.
struct FakeNet {
  std::vector<std::shared_ptr<FakeWire>> wires;
  TransportFactory factory() {
    return [this] {
      wires.push_back(std::make_shared<FakeWire>());
      return std::make_unique<FakeTransport>(wires.back());
    };
  }
};

inline void open(FakeWire& w) { w.pending.push_back({TransportEvent::Type::Open, {}, 0, false}); }
inline void message(FakeWire& w, std::string text) { w.pending.push_back({TransportEvent::Type::Message, std::move(text), 0, false}); }
inline void closeWith(FakeWire& w, int code, std::string reason = {}) { w.pending.push_back({TransportEvent::Type::Close, std::move(reason), code, false}); }
inline void error(FakeWire& w, bool tls = false) { w.pending.push_back({TransportEvent::Type::Error, "falhou", 0, tls}); }

} // namespace arcana::online::testing
