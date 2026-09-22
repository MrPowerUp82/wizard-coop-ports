#pragma once

#include "frontend.hpp"
#include "room_list.hpp"
#include "session.hpp"
#include "text_entry.hpp"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace arcana::online {

inline constexpr std::string_view kDefaultServer = "wss://vps65228.publiccloud.com.br/ws";

struct OnlineMenuConfig {
  std::string url{kDefaultServer};
  bool forceUrl{};        // --server given: ignore `pref.server` in the save
  bool allowInsecure{};   // --insecure-ws
  TransportFactory transport;
  // Headless test bot (--online-bot): "create" opens an open room and starts it once `botPlayers`
  // are in; "join" enters the first open room of the list.
  std::string bot;
  int botPlayers{2};
};

// The online pages (room list, create, name/room-code entry, lobby) and the match connection.
class OnlineMenu final : public sdl::OnlinePort {
public:
  enum class Page { Name, Home, Create, Code, Connecting, Lobby, Playing };

  explicit OnlineMenu(OnlineMenuConfig config);
  void openMenu(const Profile& profile) override;
  MenuResult updateMenu(std::uint32_t pressed, std::uint32_t held, const sdl::TextInput& text, Profile& profile) override;
  void renderMenu(sdl::BatchRenderer& b, float width, float height, float scale) override;
  [[nodiscard]] bool wantsText() const override { return page_ == Page::Name || page_ == Page::Code; }
  bool frame(double dt, Vec2 input, GameState& out) override;
  [[nodiscard]] const std::string& localId() const override;
  [[nodiscard]] bool reconnecting() const override;
  [[nodiscard]] bool closed() const override;
  std::optional<std::string> takeNotice() override;
  void choosePower(const std::string& id) override;
  void reroll() override;
  void special() override;
  void dash(Vec2 direction) override;
  void signal(const char* kind, std::optional<Vec2> at) override;
  void leave() override;

  [[nodiscard]] Page page() const { return page_; }
  [[nodiscard]] const std::string& message() const { return message_; }
  void setClockForTests(std::function<double()> now) { now_ = std::move(now); }

private:
  EntryRequest baseEntry(const Profile& profile) const;
  void connect(EntryRequest entry);
  void backHome();
  // Leaving the online pages: drop a room-list request nobody would poll any more.
  MenuResult leaveMenu();
  MenuResult updateHome(std::uint32_t pressed, Profile& profile);
  void updateCreate(std::uint32_t pressed, Profile& profile);
  MenuResult updateText(std::uint32_t pressed, const sdl::TextInput& text, Profile& profile);
  MenuResult updateSession(std::uint32_t pressed);
  void updateBot(Profile& profile);
  [[nodiscard]] int visibleRooms() const;
  void renderHome(sdl::BatchRenderer& b, float width, float height, float s);
  void renderCreate(sdl::BatchRenderer& b, float width, float height, float s);
  void renderText(sdl::BatchRenderer& b, float width, float height, float s);
  void renderLobby(sdl::BatchRenderer& b, float width, float height, float s);

  OnlineMenuConfig config_;
  std::function<double()> now_;
  Page page_{Page::Home};
  std::string url_, name_;  // name_: the saved player name, shown on the room list
  bool urlOk_{};
  std::unique_ptr<RoomList> rooms_;
  std::unique_ptr<Session> session_;
  TextEntry entry_{TextKind::Name};
  std::string message_;
  int index_{};
  bool open_{true};
  int campaign_{};
  std::array<bool, 6> curses_{};
  bool endlessUnlocked_{};
  double botNextTry_{};
};

} // namespace arcana::online
