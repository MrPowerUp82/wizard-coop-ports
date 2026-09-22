// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "arcana/game.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace arcana;
struct ZeroRandom : Random { arcana::real next() override { return 0.1; } };
int main(){
  ZeroRandom rng;
  assert(xpNeeded(1)==10);
  auto s=createGameState("quick",{"swarm","famine","swarm","invalid"});
  assert(s.campaign=="quick"&&s.curses.size()==2);
  auto p=createPlayer("p1","Mago",0); s.players[p.id]=p;
  for(int i=0;i<300;i++) updateGame(s,1.0/30.0,rng);
  assert(s.time>9.9&&s.time<10.1);
  assert(!s.enemies.empty());
  auto& pl=s.players.at("p1");
  pl.pendingPowers={"arcane"}; double dmg=pl.damage; assert(applyPower(pl,"arcane")); assert(pl.damage>dmg);
  pl.specialCharge=100; pl.specialCooldown=0; assert(activateSpecial(s,"p1",rng)); assert(pl.specialCharge==0);
  pl.dashCooldown=0; assert(activateDash(s,"p1",{1,0})); assert(pl.dashFor>0);
  auto d=difficultyAt(300,1,1,&s); assert(d.hpScale>1);
  auto json=stateToJson(s,"p1"); assert(json.find("\"players\"")!=std::string::npos);
  // playerMovement: the online client predicts with the exact server formula (movement.js).
  {
    Player p = createPlayer("m", "M", 0);
    p.input = {1, 0};
    const Vec2 walk = playerMovement(p, 0.5);
    assert(std::fabs(walk.x - p.speed * 0.5) < 1e-9 && std::fabs(walk.y) < 1e-9);
    p.dashFor = 0.1; p.dashX = 0; p.dashY = 1;
    const Vec2 burst = playerMovement(p, 0.5);
    assert(std::fabs(burst.y - cfg::DASH_SPEED * 0.1) < 1e-9);
    assert(std::fabs(burst.x - p.speed * 0.4) < 1e-9);
    Event e;
    e.player = "p2"; e.name = "Ana";
    assert(e.player == "p2" && e.name == "Ana");
  }
  std::cout << "arcana_core_tests: OK\n";
}
