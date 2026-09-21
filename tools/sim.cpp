#include "arcana/game.hpp"
#include <iostream>
#include <string>
using namespace arcana;
int main(int argc,char**argv){int seconds=argc>1?std::stoi(argv[1]):30;SeededRandom rng(12345);auto s=createGameState("quick");auto p=createPlayer("bot","Bot",0);p.powers["orbit"]=2;p.powers["aura"]=1;s.players[p.id]=p;for(int i=0;i<seconds*30&&!s.over;i++){auto& me=s.players.at("bot");double t=i/30.0;me.input.x=std::cos(t*.7);me.input.y=std::sin(t*.7);updateGame(s,1.0/30.0,rng);if(!me.pendingPowers.empty())applyPower(me,me.pendingPowers.front());if(me.specialCharge>=100)activateSpecial(s,"bot",rng);}auto& me=s.players.at("bot");std::cout<<"time="<<s.time<<" phase="<<s.phase<<" enemies="<<s.enemies.size()<<" level="<<me.level<<" kills="<<me.stats.kills<<" damage="<<me.stats.damage<<"\n";}
