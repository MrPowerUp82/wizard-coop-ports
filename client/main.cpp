#include "arcana/game.hpp"
#include <SDL.h>
#ifdef ARCANA_HAS_NET
#include "network.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <sstream>
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
using namespace arcana;
struct RGB{Uint8 r,g,b;}; static constexpr RGB PC[4]={{118,223,255},{255,120,72},{126,225,98},{194,145,255}};
static void circle(SDL_Renderer*r,int cx,int cy,int rad,RGB c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,255);for(int y=-rad;y<=rad;y++){int x=(int)std::sqrt(std::max(0,rad*rad-y*y));SDL_RenderDrawLine(r,cx-x,cy+y,cx+x,cy+y);}}
static SDL_Point w2s(double x,double y,const Player&cam,const SDL_Rect&vp){constexpr double scale=.8;return {(int)(vp.x+vp.w/2+(x-cam.x)*scale),(int)(vp.y+vp.h/2+(y-cam.y)*scale)};}
static void drawView(SDL_Renderer*r,const GameState&s,const Player&cam,SDL_Rect vp){
  SDL_RenderSetViewport(r,&vp);SDL_SetRenderDrawColor(r,12,15,26,255);SDL_RenderFillRect(r,nullptr);SDL_RenderSetViewport(r,nullptr);
  for(const auto&g:s.gems){auto q=w2s(g.x,g.y,cam,vp);RGB c=g.type=="heart"?RGB{240,80,90}:g.type=="greenGem"?RGB{90,245,140}:g.type=="coin"?RGB{245,205,80}:RGB{100,170,255};circle(r,q.x,q.y,g.type=="gem"?4:6,c);}
  for(const auto&z:s.zones){auto q=w2s(z.x,z.y,cam,vp);SDL_SetRenderDrawColor(r,110,90,180,255);SDL_Rect a{q.x-(int)(z.radius*.8),q.y-(int)(z.radius*.8),(int)(z.radius*1.6),(int)(z.radius*1.6)};SDL_RenderDrawRect(r,&a);}
  for(const auto&h:s.hazards){auto q=w2s(h.x,h.y,cam,vp);SDL_SetRenderDrawColor(r,255,80,70,255);SDL_Rect a{q.x-(int)(h.radius*.8),q.y-(int)(h.radius*.8),(int)(h.radius*1.6),(int)(h.radius*1.6)};SDL_RenderDrawRect(r,&a);}
  for(const auto&e:s.enemies){auto q=w2s(e.x,e.y,cam,vp);RGB c=e.boss?RGB{255,185,70}:e.elite?RGB{255,220,80}:RGB{180,190,205};circle(r,q.x,q.y,e.boss?28:e.elite?14:10,c);}
  for(const auto&sh:s.shots){auto q=w2s(sh.x,sh.y,cam,vp);circle(r,q.x,q.y,4,PC[std::clamp(sh.color,0,3)]);}
  for(const auto&sh:s.enemyShots){auto q=w2s(sh.x,sh.y,cam,vp);circle(r,q.x,q.y,4,{255,80,80});}
  for(const auto&[_,p]:s.players){auto q=w2s(p.x,p.y,cam,vp);circle(r,q.x,q.y,13,PC[std::clamp(p.color,0,3)]);int w=50;SDL_Rect bg{q.x-w/2,q.y-24,w,5},fg=bg;fg.w=(int)(w*(p.hp/std::max(1.0,p.maxHp)));SDL_SetRenderDrawColor(r,50,50,60,255);SDL_RenderFillRect(r,&bg);SDL_SetRenderDrawColor(r,90,230,120,255);SDL_RenderFillRect(r,&fg);}
}
#ifdef ARCANA_HAS_NET
static bool updateRemote(GameState&s,const std::string&raw,std::string&playerId,bool&joined){using boost::property_tree::ptree;try{std::stringstream ss(raw);ptree root;boost::property_tree::read_json(ss,root);auto type=root.get<std::string>("type","");if(type=="joined"){playerId=root.get<std::string>("playerId","");joined=true;return false;}const ptree*st=&root;if(type=="start"){auto c=root.get_child_optional("state");if(!c)return false;st=&*c;}else if(type!="state")return false;s.campaign=st->get<std::string>("campaign","quick");s.time=st->get<double>("time",0);s.over=st->get<bool>("over",false);s.victory=st->get<bool>("victory",false);s.phase=st->get<int>("phase",0);s.phaseTime=st->get<double>("phaseTime",0);s.phaseStatus=st->get<std::string>("phaseStatus","horde");s.loop=st->get<int>("loop",0);s.players.clear();if(auto ps=st->get_child_optional("players"))for(auto&kv:*ps){auto&v=kv.second;Player p=createPlayer(kv.first,v.get<std::string>("name","Arcanista"),v.get<int>("color",0));p.x=v.get<double>("x",0);p.y=v.get<double>("y",0);p.hp=v.get<double>("hp",100);p.maxHp=v.get<double>("maxHp",100);p.xp=v.get<double>("xp",0);p.level=v.get<int>("level",1);p.alive=v.get<bool>("alive",true);p.specialCharge=v.get<double>("specialCharge",0);p.coins=v.get<int>("coins",0);if(auto pp=v.get_child_optional("pendingPowers"))for(auto&x:*pp)p.pendingPowers.push_back(x.second.get_value<std::string>());s.players[p.id]=std::move(p);}s.enemies.clear();if(auto es=st->get_child_optional("enemies"))for(auto&kv:*es){auto&v=kv.second;Enemy e;e.id=v.get<std::uint64_t>("id",0);e.type=v.get<std::string>("type","");e.x=v.get<double>("x",0);e.y=v.get<double>("y",0);e.hp=v.get<double>("hp",1);e.maxHp=v.get<double>("maxHp",1);e.boss=v.get<bool>("boss",false);e.elite=v.get<bool>("elite",false);s.enemies.push_back(e);}s.shots.clear();if(auto qs=st->get_child_optional("shots"))for(auto&kv:*qs){auto&v=kv.second;Shot q;q.x=v.get<double>("x",0);q.y=v.get<double>("y",0);q.color=v.get<int>("color",0);s.shots.push_back(q);}s.enemyShots.clear();if(auto qs=st->get_child_optional("enemyShots"))for(auto&kv:*qs){auto&v=kv.second;EnemyShot q;q.x=v.get<double>("x",0);q.y=v.get<double>("y",0);q.radius=v.get<double>("radius",12);s.enemyShots.push_back(q);}s.gems.clear();if(auto gs=st->get_child_optional("gems"))for(auto&kv:*gs){auto&v=kv.second;Drop g;g.id=v.get<std::uint64_t>("id",0);g.x=v.get<double>("x",0);g.y=v.get<double>("y",0);g.type=v.get<std::string>("type","gem");g.value=v.get<double>("value",1);s.gems.push_back(g);}return true;}catch(...){return false;}}
#endif
int main(int argc,char**argv){
  bool local2=argc>1&&std::string(argv[1])=="--local2";bool online=false,createRoom=false;std::string host,port="8081",roomCode,playerId;int onlineColor=0;
#ifdef ARCANA_HAS_NET
  if(argc>=4&&(std::string(argv[1])=="--create"||std::string(argv[1])=="--join")){online=true;createRoom=std::string(argv[1])=="--create";host=argv[2];port=argv[3];if(!createRoom&&argc>=5)roomCode=argv[4];if(argc>=(createRoom?5:6))onlineColor=std::clamp(std::stoi(argv[createRoom?4:5]),0,3);}
#endif
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)!=0){std::cerr<<SDL_GetError()<<"\n";return 1;}
  SDL_Window*w=SDL_CreateWindow("Arcana Survivors C++",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);SDL_Renderer*r=SDL_CreateRenderer(w,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!w||!r){std::cerr<<SDL_GetError()<<"\n";return 1;}
  SeededRandom rng(0xA11CE);auto s=createGameState("quick");bool joined=false,requestSent=false;
#ifdef ARCANA_HAS_NET
  arcana_client::NetworkClient net;if(online)net.connect(host,port,"/");
#endif
  if(!online){s.players["p1"]=createPlayer("p1","Arcanista 1",0);s.players["p1"].powers["orbit"]=1;if(local2){s.players["p2"]=createPlayer("p2","Arcanista 2",1);s.players["p2"].powers["aura"]=1;}}
  bool run=true;auto last=std::chrono::steady_clock::now();
  while(run){
#ifdef ARCANA_HAS_NET
    if(online){if(net.connected()&&!requestSent){requestSent=true;if(createRoom)net.send("{\"type\":\"create\",\"name\":\"Arcanista\",\"color\":"+std::to_string(onlineColor)+",\"campaign\":\"quick\",\"visibility\":\"open\"}");else net.send("{\"type\":\"join\",\"room\":\""+roomCode+"\",\"name\":\"Arcanista\",\"color\":"+std::to_string(onlineColor)+"}");}for(auto&m:net.poll()){bool was=joined;updateRemote(s,m,playerId,joined);if(createRoom&&joined&&!was)net.send("{\"type\":\"start\"}");}}
#endif
    SDL_Event e;while(SDL_PollEvent(&e)){if(e.type==SDL_QUIT)run=false;if(e.type==SDL_KEYDOWN){auto key=e.key.keysym.sym;if(key==SDLK_ESCAPE)run=false;std::string meId=online?playerId:"p1";auto it=s.players.find(meId);if(it!=s.players.end()){auto&p=it->second;if(key==SDLK_SPACE){
#ifdef ARCANA_HAS_NET
          if(online)net.send("{\"type\":\"special\"}");else
#endif
          activateSpecial(s,meId,rng);}if(key==SDLK_LSHIFT){
#ifdef ARCANA_HAS_NET
          if(online)net.send("{\"type\":\"dash\",\"x\":"+std::to_string(p.input.x)+",\"y\":"+std::to_string(p.input.y)+"}");else
#endif
          activateDash(s,meId,{p.input.x,p.input.y});}if(!p.pendingPowers.empty()&&key>=SDLK_1&&key<=SDLK_3){int i=key-SDLK_1;if(i<(int)p.pendingPowers.size()){
#ifdef ARCANA_HAS_NET
            if(online)net.send("{\"type\":\"choosePower\",\"power\":\""+p.pendingPowers[i]+"\"}");else
#endif
            applyPower(p,p.pendingPowers[i]);}}}if(local2&&!online){auto&q=s.players["p2"];if(key==SDLK_RCTRL)activateSpecial(s,"p2",rng);if(key==SDLK_RETURN)activateDash(s,"p2",{q.input.x,q.input.y});}}}
    const Uint8*k=SDL_GetKeyboardState(nullptr);auto norm=[](double&x,double&y){double l=std::hypot(x,y);if(l>1){x/=l;y/=l;}};std::string meId=online?playerId:"p1";auto mit=s.players.find(meId);Player fallback;Player& p=mit!=s.players.end()?mit->second:fallback;p.input.x=(k[SDL_SCANCODE_D]?1:0)-(k[SDL_SCANCODE_A]?1:0);p.input.y=(k[SDL_SCANCODE_S]?1:0)-(k[SDL_SCANCODE_W]?1:0);norm(p.input.x,p.input.y);
#ifdef ARCANA_HAS_NET
    if(online&&joined)net.send("{\"type\":\"input\",\"x\":"+std::to_string(p.input.x)+",\"y\":"+std::to_string(p.input.y)+",\"seq\":"+std::to_string(++p.inputSeq)+"}");
#endif
    if(local2&&!online){auto&q=s.players["p2"];q.input.x=(k[SDL_SCANCODE_RIGHT]?1:0)-(k[SDL_SCANCODE_LEFT]?1:0);q.input.y=(k[SDL_SCANCODE_DOWN]?1:0)-(k[SDL_SCANCODE_UP]?1:0);norm(q.input.x,q.input.y);if(!q.pendingPowers.empty())applyPower(q,q.pendingPowers.front());}
    auto now=std::chrono::steady_clock::now();double dt=std::chrono::duration<double>(now-last).count();last=now;
#ifdef ARCANA_HAS_NET
    if(!online)
#endif
    updateGame(s,std::min(dt,.05),rng);
    int ww,hh;SDL_GetRendererOutputSize(r,&ww,&hh);SDL_SetRenderDrawColor(r,5,6,10,255);SDL_RenderClear(r);if(local2&&!online){SDL_Rect l{0,0,ww/2,hh},rr{ww/2,0,ww-ww/2,hh};drawView(r,s,s.players["p1"],l);drawView(r,s,s.players["p2"],rr);SDL_SetRenderDrawColor(r,255,255,255,80);SDL_RenderDrawLine(r,ww/2,0,ww/2,hh);}else if(mit!=s.players.end()){SDL_Rect full{0,0,ww,hh};drawView(r,s,p,full);}SDL_RenderPresent(r);
  }
  SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
