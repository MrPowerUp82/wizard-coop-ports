#include "room_list.hpp"

#include <utility>

namespace arcana::online {

RoomList::RoomList(std::string url, TransportFactory factory) : url_(std::move(url)), factory_(std::move(factory)) {}

void RoomList::finish(double now) {
  loaded_ = true;
  transport_->close();
  transport_.reset();
  nextAt_ = now + kRefreshMs;
}

void RoomList::update(double now) {
  if (!transport_) {
    if (now < nextAt_) return;
    transport_ = factory_();
    transport_->open(url_);
    startedAt_ = now;
    return;
  }
  events_.clear();
  transport_->poll(events_);
  for (const auto& e : events_) {
    if (e.type == TransportEvent::Type::Open) {
      transport_->send(encodeSimple("listRooms"));
    } else if (e.type == TransportEvent::Type::Message) {
      ServerMessage m = parseServerMessage(e.text);
      if (m.kind != ServerKind::Rooms) continue;
      if (m.rooms.version != kProtocolVersion) {
        rooms_ = {};
        error_ = "O servidor usa outra versão do jogo. Atualize para jogar online.";
      } else {
        rooms_ = std::move(m.rooms);
        error_.clear();
      }
      finish(now);
      return;
    } else {
      error_ = e.tls ? "Certificado do servidor inválido." : "Não foi possível alcançar o servidor.";
      finish(now);
      return;
    }
  }
  if (now - startedAt_ > kTimeoutMs) {
    error_ = "O servidor não respondeu.";
    finish(now);
  }
}

} // namespace arcana::online
