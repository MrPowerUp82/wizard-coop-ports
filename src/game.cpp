#include "arcana/game.hpp"
#include "arcana/data.hpp"
#include "arcana/constants.hpp"
#include "arcana/native/spatial_grid.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace arcana {
namespace {
using cfg::MAX_PLAYERS; using cfg::MAX_ENEMIES; using cfg::MAX_SHOTS; using cfg::MAX_ENEMY_SHOTS; using cfg::MAX_DROPS; using cfg::MAX_HAZARDS; using cfg::MAX_RUNES; using cfg::MAX_ZONES;

using PlayerList = native::StaticVector<Player*, cfg::MAX_PLAYERS>;
using EnemyGrid = native::SpatialGrid<Enemy, cfg::MAX_ENEMIES>;

template<class Container, class Pred> void retain(Container& v, Pred pred, std::size_t cap=std::numeric_limits<std::size_t>::max()) {
  v.erase(std::remove_if(v.begin(),v.end(),[&](const typename Container::value_type& x){return !pred(x);}),v.end());
  if(v.size()>cap) v.erase(v.begin(), v.end()-static_cast<std::ptrdiff_t>(cap));
}
bool hasCurse(const GameState& s,const std::string& id){return std::find(s.curses.begin(),s.curses.end(),id)!=s.curses.end();}
const CampaignDef& campaignOf(const GameState& s){ auto it=campaigns().find(s.campaign); return it==campaigns().end()?campaigns().at("classic"):it->second; }
std::uint64_t nextId(GameState& s){return ++s.nextId;}
void pushEvent(GameState& s,std::string kind,double x=0,double y=0,int color=0){
  s.events.push_back(Event{++s.eventSeq,std::move(kind),s.time,x,y,0,0,color});
  if(s.events.size()>64) s.events.erase(s.events.begin(),s.events.end()-64);
}
void pushEvent(GameState& s,Event e){e.id=++s.eventSeq;e.t=s.time;s.events.push_back(std::move(e));if(s.events.size()>64)s.events.erase(s.events.begin(),s.events.end()-64);}

double healingScale(const GameState& s){return hasCurse(s,"famine")?.5:1.0;}
double enemyXp(const std::string& type){const auto& e=enemyDefs().at(type);return e.xp>0?e.xp:std::max(1.0,std::round(e.hp/14.0));}

Player* nearestPlayer(GameState& s, const Enemy& e){Player* best=nullptr;double bd=1e300;for(auto& [_,p]:s.players)if(p.alive){double d=distanceSq({e.x,e.y},{p.x,p.y});if(d<bd){bd=d;best=&p;}}return best;}
Enemy* nearestEnemy(GameState& s,Vec2 at,double range){Enemy* best=nullptr;double bd=range*range;for(auto& e:s.enemies)if(e.hp>0){double d=distanceSq(at,{e.x,e.y});if(d<bd){bd=d;best=&e;}}return best;}

void applyMeta(Player& p,const MetaRanks& meta,const Loadout* loadout){
  auto r=[&](const char* k,int hi){auto i=meta.rank.find(k);return i==meta.rank.end()?0:std::clamp(i->second,0,hi);};
  if(loadout && r("arsenal",1) && (loadout->weapon=="orbit"||loadout->weapon=="aura"||loadout->weapon=="chain"||loadout->weapon=="runes"||loadout->weapon=="familiar")) p.powers[loadout->weapon]=1;
  p.specialVariant=(loadout&&r("secondSpell",1)&&loadout->special==1)?1:0;
  p.maxHp+=r("vigor",5)*6;p.hp=p.maxHp;p.damage*=1+r("might",5)*.03;p.attackDelay*=1-r("celerity",4)*.03;
  p.speed*=1+r("stride",3)*.04;p.armor+=r("ward",4)*.5;p.pickupRadius+=r("reach",3)*15;
  p.xpMult=1+r("wisdom",5)*.03;p.coinMult=1+r("greed",3)*.1;p.specialCharge=r("channel",3)*20;
  p.rerolls=1+r("reroll",3);if(r("pact",1))p.powers["familiar"]=std::max(1,rankOf(p,"familiar"));p.phoenix=r("phoenix",1);
}

bool eligible(const Player& p,const std::string& id,bool coop){
  auto it=powerDefs().find(id);if(it==powerDefs().end())return false;const auto& d=it->second;if(rankOf(p,id)>=d.max)return false;
  if(d.kind=="signature")return p.color==d.color&&p.level>=d.minLevel;if(d.kind=="coop")return coop;
  if(d.kind=="evolution"){for(const auto& [req,rank]:d.requirements)if(rankOf(p,req)<rank)return false;}return true;
}

void grantXp(GameState& s,Player& p,double amount,Random& random){
  p.xp+=amount*p.xpMult;bool coop=s.players.size()>1;
  while(p.alive&&p.pendingPowers.empty()&&p.xp>=xpNeeded(p.level)){p.xp-=xpNeeded(p.level);++p.level;p.hp=std::min(p.maxHp,p.hp+p.maxHp*.1*healingScale(s));offerPowers(p,random,coop);}
}

bool hurt(GameState& s,Player& p,double damage){
  if(!p.alive||p.hitCooldown>0||!p.pendingPowers.empty()||p.invulnerableFor>0)return false;
  if(hasCurse(s,"brittle"))damage*=1.25;if(s.bloodPact)damage*=1.3;double taken=std::max(2.0,damage-p.armor);
  p.hp=std::max(0.0,p.hp-taken);p.hitCooldown=cfg::CONTACT_PLAYER_COOLDOWN;p.stats.taken+=taken;
  if(p.hp<=0&&p.phoenix>0){--p.phoenix;p.hp=p.maxHp*.5;p.invulnerableFor=3;pushEvent(s,"phoenix",p.x,p.y,p.color);return true;}
  if(p.hp<=0){p.alive=false;p.input={};p.pendingPowers.clear();p.reviveProgress=0;p.reviveBy.clear();}
  return true;
}

void addDrop(GameState& s,double x,double y,const std::string& type,double value){
  if(type=="gem" && (s.gems.size()>=MAX_DROPS||s.gems.size()>=140)){
    Drop* target=nullptr;double best=s.gems.size()>=MAX_DROPS?1e300:90.0*90.0;
    for(auto& g:s.gems)if(g.type=="gem"&&!g.dead){double d=distanceSq({g.x,g.y},{x,y});if(d<best){best=d;target=&g;}}
    if(target){target->value+=value;target->ttl=std::max(target->ttl,cfg::DROP_TTL/2);return;}
  }
  if(s.gems.size()<MAX_DROPS)s.gems.push_back({nextId(s),x,y,value,cfg::DROP_TTL,type,{ },false});
}

Enemy* spawnEnemy(GameState& s,const std::string& type,double x,double y,double hpScale=1,bool elite=false,bool minion=false,bool thief=false){
  if(s.enemies.size()>=MAX_ENEMIES)return nullptr;auto it=enemyDefs().find(type);if(it==enemyDefs().end())return nullptr;double hp=it->second.hp*hpScale*(elite?7:1);
  Enemy e;e.id=nextId(s);e.type=type;e.x=x;e.y=y;e.hp=e.maxHp=hp;e.elite=elite;e.minion=minion;e.thief=thief;s.enemies.push_back(std::move(e));return &s.enemies.back();
}

void dropLoot(GameState& s,Enemy& e,Random& rnd){
  addDrop(s,e.x,e.y,"gem",enemyXp(e.type)*(e.elite?7:1));if(e.elite){addDrop(s,e.x+18,e.y,"chest",1);addDrop(s,e.x-18,e.y,"magnet",1);return;}if(e.minion)return;
  double roll=rnd.next();if(roll<.05)addDrop(s,e.x,e.y,"heart",25);else if(roll<.25)addDrop(s,e.x,e.y,"greenGem",25);else if(roll<.28)addDrop(s,e.x,e.y,"coin",1);
}

void thiefDown(GameState& s,const Enemy& e){for(int n=0;n<3;n++)addDrop(s,e.x+(n-1)*22,e.y+20,"coin",4);if(s.encounter&&s.encounter->enemyId==e.id)s.encounter->status="complete";pushEvent(s,"thiefDown",e.x,e.y);}

void onKill(GameState& s,Enemy& e,Player* source,Random& rnd,bool shard){
  dropLoot(s,e,rnd);if(source)source->stats.kills++;const auto& def=enemyDefs().at(e.type);
  if(def.behavior=="splitter"){double scale=e.maxHp/def.hp/(e.elite?7:1);for(int n=0;n<2;n++){double a=rnd.next()*PI*2;spawnEnemy(s,"slimelet",e.x+std::cos(a)*18,e.y+std::sin(a)*18,scale,false,true);}}
  if(source&&!shard&&e.slowFor>0&&rankOf(*source,"shatter")){
    int count=rankOf(*source,"avalanche")?8:4;double mult=rankOf(*source,"avalanche")?.9:.6;double off=rnd.next()*PI;
    for(int n=0;n<count&&s.shots.size()<MAX_SHOTS;n++){double a=off+n*PI*2/count;Shot sh;sh.x=e.x;sh.y=e.y;sh.vx=std::cos(a)*420;sh.vy=std::sin(a)*420;sh.ttl=.5;sh.color=0;sh.damage=source->damage*mult;sh.pierce=1;sh.hitIds={e.id};sh.owner=source->id;sh.shard=true;s.shots.push_back(std::move(sh));}
  }
  if(e.thief)thiefDown(s,e);if(e.elite)pushEvent(s,"eliteDown",e.x,e.y);
}

void track(Player* p,const std::string& kind,double amount){if(!p||amount<=0)return;p->stats.damage+=amount;p->stats.by[kind]+=amount;}

bool damageEnemy(GameState& s,Enemy& e,double damage,Random& rnd,Player* source=nullptr,bool slow=false,bool shard=false,const std::string& element="",const std::string& kind="spell"){
  if(e.hp<=0)return false;std::string reaction;if(element=="fire"&&e.slowFor>0)reaction="thermal";else if(element=="lightning"&&e.rootFor>0)reaction="conduction";else if(element=="moon"&&e.burningFor>0)reaction="eclipse";
  double bonus=0;if(!reaction.empty()&&e.comboAt<=s.time){e.comboAt=s.time+1;std::string setter=reaction=="thermal"?e.slowBy:reaction=="conduction"?e.rootBy:e.burnBy;bool team=source&&!setter.empty()&&setter!=source->id&&s.players.contains(setter);bonus=(source?source->damage:damage)*(team?2:1.5);if(team){for(Player* p:{source,&s.players.at(setter)})p->specialCharge=std::min(100.0,p->specialCharge+6);}if(reaction=="thermal"){e.slowFor=0;e.freezeFor=0;}pushEvent(s,"combo",e.x,e.y,source?source->color:0);}
  if(element=="fire"){e.burningFor=1.5;if(source)e.burnBy=source->id;}double total=damage+bonus,dealt=std::min(e.hp,total);if(total>0){track(source,kind,dealt*damage/total);track(source,"combo",dealt*bonus/total);}e.hp-=total;
  if(slow){e.slowFor=1.2;if(source)e.slowBy=source->id;}if(e.hp<=0){onKill(s,e,source,rnd,shard);return true;}return false;
}

Vec2 dashDirection(const Player& p,Vec2 in){double l=std::hypot(in.x,in.y);if(l>.01)return {in.x/l,in.y/l};l=std::hypot(p.moveX,p.moveY);return l?Vec2{p.moveX/l,p.moveY/l}:Vec2{0,1};}
Vec2 movementDelta(const Player& p,double dt){double burst=std::min(dt,std::max(0.0,p.dashFor));return {(p.dashX)*cfg::DASH_SPEED*burst+p.input.x*p.speed*(dt-burst),(p.dashY)*cfg::DASH_SPEED*burst+p.input.y*p.speed*(dt-burst)};}

struct Spell { double speed,radius; int pierce; bool slow; double splash; };
const std::array<Spell,4> SPELLS={Spell{490,29,1,true,0},Spell{430,29,1,false,75},Spell{560,29,3,false,0},Spell{400,48,2,false,0}};
Shot playerShot(const Player& p,double angle,bool special=false){const auto& sp=SPELLS[p.color];Shot sh;sh.x=p.x;sh.y=p.y;sh.vx=std::cos(angle)*sp.speed;sh.vy=std::sin(angle)*sp.speed;sh.ttl=special?2:1.55;sh.damage=p.damage*(special?3:1);sh.color=p.color;sh.special=special;sh.pierce=sp.pierce;sh.owner=p.id;if(!special&&p.color==3&&rankOf(p,"boomerang")){sh.boomerang=true;sh.fullmoon=rankOf(p,"fullmoon");}if(!special&&p.color==2&&rankOf(p,"bramble")){sh.pierce+=2;sh.damage*=1.15;}return sh;}

} // namespace

double SeededRandom::next(){a_+=0x6d2b79f5u;std::uint32_t t=a_;t=(t^(t>>15))*(t|1u);t^=t+(t^(t>>7))*(t|61u);return static_cast<double>(t^(t>>14))/4294967296.0;}

int rankOf(const Player& p,const std::string& id){auto it=p.powers.find(id);return it==p.powers.end()?0:it->second;}
int xpNeeded(int level){return static_cast<int>(std::floor((5+level*3+level*level*.65)*1.2));}
GameState createGameState(std::string campaign,std::vector<std::string> curses){GameState s;s.enemies.reserve(cfg::MAX_ENEMIES);s.shots.reserve(cfg::MAX_SHOTS);s.enemyShots.reserve(cfg::MAX_ENEMY_SHOTS);s.gems.reserve(cfg::MAX_DROPS);s.hazards.reserve(cfg::MAX_HAZARDS);s.runes.reserve(cfg::MAX_RUNES);s.zones.reserve(cfg::MAX_ZONES);s.events.reserve(64);s.campaign=(campaign=="quick"||campaign=="endless")?campaign:"classic";static const std::unordered_set<std::string> valid={"swarm","frenzy","brittle","famine","tyrant","nobility"};for(auto& c:curses)if(valid.contains(c)&&std::find(s.curses.begin(),s.curses.end(),c)==s.curses.end())s.curses.push_back(c);return s;}
Player createPlayer(std::string id,std::string name,int color,const MetaRanks* meta,const Loadout* loadout){Player p;p.id=std::move(id);p.name=std::move(name);p.color=std::clamp(color,0,3);p.x=p.color*55.0;if(meta||loadout){MetaRanks empty;applyMeta(p,meta?*meta:empty,loadout);}return p;}
void addLatePlayer(GameState& s,Player player){Player* anchor=nullptr;double avg=0;for(auto& [_,p]:s.players){if(!anchor||(p.alive&&!anchor->alive))anchor=&p;avg+=p.level;}if(anchor){player.x=anchor->x+60;player.y=anchor->y+20;}int target=s.players.empty()?1:std::max(1,static_cast<int>(std::floor(avg/s.players.size()*.8)));for(int l=1;l<target;l++)player.xp+=xpNeeded(l);player.invulnerableFor=3;s.players[player.id]=std::move(player);}

std::vector<std::string> availablePowers(const Player& p,Random& rnd,bool coop,const std::vector<std::string>& exclude){
 std::vector<std::string> pool;for(const auto& [id,_]:powerDefs())if(std::find(exclude.begin(),exclude.end(),id)==exclude.end()&&eligible(p,id,coop))pool.push_back(id);std::vector<std::string> out;auto w=[](const std::string& k){return k=="evolution"?1000.0:k=="signature"?1.6:k=="weapon"?1.25:k=="coop"?.8:1.0;};while(!pool.empty()&&out.size()<3){double total=0;for(auto& id:pool)total+=w(powerDefs().at(id).kind);double roll=rnd.next()*total;std::size_t i=0;for(;i<pool.size();i++){roll-=w(powerDefs().at(pool[i]).kind);if(roll<0)break;}if(i>=pool.size())i=pool.size()-1;out.push_back(pool[i]);pool.erase(pool.begin()+i);}return out;
}
bool offerPowers(Player& p,Random& rnd,bool coop){p.pendingPowers=availablePowers(p,rnd,coop);p.powerTimer=0;return !p.pendingPowers.empty();}
bool applyPower(Player& p,const std::string& id){if(std::find(p.pendingPowers.begin(),p.pendingPowers.end(),id)==p.pendingPowers.end())return false;auto it=powerDefs().find(id);if(it==powerDefs().end()||rankOf(p,id)>=it->second.max)return false;int r=rankOf(p,id)+1;p.powers[id]=r;if(id=="arcane")p.damage*=1.25;else if(id=="haste")p.attackDelay=std::max(.18,p.attackDelay*.88);else if(id=="vitality"){p.maxHp+=22;p.hp=std::min(p.maxHp,p.hp+30);}else if(id=="swiftness")p.speed*=1.12;else if(id=="multishot")p.projectiles=std::min(4,p.projectiles+1);else if(id=="magnet")p.pickupRadius+=55;else if(id=="armor")p.armor+=1.5;p.pendingPowers.clear();p.powerTimer=0;p.invulnerableFor=3;return true;}
bool rerollPowers(Player& p,Random& rnd,bool coop){if(p.pendingPowers.empty()||p.rerolls<=0)return false;auto old=p.pendingPowers;auto choices=availablePowers(p,rnd,coop,old);for(auto& x:old)if(choices.size()<3&&eligible(p,x,coop))choices.push_back(x);--p.rerolls;p.pendingPowers=std::move(choices);p.powerTimer=0;return true;}

double phaseDuration(const GameState& s){return campaignOf(s).seconds;}double phaseClock(const GameState& s){return s.phaseTime*300.0/phaseDuration(s);}double curseReward(const GameState& s){double r=1;for(auto& c:s.curses)r+=c=="swarm"?.25:c=="frenzy"?.2:c=="brittle"?.25:c=="famine"?.15:c=="tyrant"?.25:c=="nobility"?.2:0;return r;}
Difficulty difficultyAt(double clock,int playerCount,int phase,const GameState* s){double minutes=phase*3+clock/60,extra=std::max(0,playerCount-1),loop=s?s->loop:0;int count=1+static_cast<int>(std::floor(minutes/1.5))+static_cast<int>(std::floor(extra/2.0))+loop;Difficulty d;d.spawnInterval=std::max(.25,.5-minutes*.04);d.hpScale=(1+minutes*.215+minutes*minutes*.018)*(1+extra*.3)*(1+loop*.6);d.damageScale=std::min(2.15,1+minutes*.091)*(1+loop*.15);d.speedScale=std::min(1.22,1+minutes*.028)*(s&&hasCurse(*s,"frenzy")?1.15:1);d.spawnCount=(s&&hasCurse(*s,"swarm"))?static_cast<int>(std::ceil(count*1.35)):count;return d;}

bool activateDash(GameState& s,const std::string& id,Vec2 input){auto it=s.players.find(id);if(it==s.players.end())return false;auto& p=it->second;if(!p.alive||s.over||s.phaseStatus=="transition"||!p.pendingPowers.empty()||p.dashCooldown>0)return false;auto d=dashDirection(p,input);p.dashX=d.x;p.dashY=d.y;p.dashFor=.18;p.dashCooldown=4;++p.motionId;p.invulnerableFor=std::max(p.invulnerableFor,.18);pushEvent(s,"evade",p.x,p.y,p.color);return true;}
bool sendSignal(GameState& s,const std::string& id,const std::string& kind,std::optional<Vec2> at){auto it=s.players.find(id);static const std::array<std::string,4> kinds={"here","help","danger","look"};if(it==s.players.end()||s.over||std::find(kinds.begin(),kinds.end(),kind)==kinds.end())return false;auto& p=it->second;if(s.time-p.signalAt<.8)return false;Vec2 q=at.value_or(Vec2{p.x,p.y});if(distanceSq(q,{p.x,p.y})>2500.0*2500.0)return false;p.signalAt=s.time;Event e;e.kind="signal";e.x=q.x;e.y=q.y;e.color=p.color;e.text=kind;pushEvent(s,std::move(e));return true;}

namespace {

void converge(GameState& s,Player& p,Random& rnd){
  retain(s.recentSpecials,[&](const RecentSpecial& r){auto it=s.players.find(r.id);return s.time-r.t<=1.5&&it!=s.players.end()&&it->second.alive;});
  auto it=std::find_if(s.recentSpecials.begin(),s.recentSpecials.end(),[&](const RecentSpecial& r){return r.id!=p.id&&distanceSq({r.x,r.y},{p.x,p.y})<420.0*420.0;});
  if(it==s.recentSpecials.end()){s.recentSpecials.push_back({p.id,s.time,p.x,p.y});return;}auto pit=s.players.find(it->id);if(pit==s.players.end())return;auto& q=pit->second;double x=(p.x+q.x)/2,y=(p.y+q.y)/2,damage=(p.damage+q.damage)*4;s.recentSpecials.erase(it);for(auto& e:s.enemies)if(e.hp>0&&distanceSq({x,y},{e.x,e.y})<320.0*320.0)damageEnemy(s,e,damage,rnd,&p,false,false,"","convergence");pushEvent(s,"convergence",x,y,p.color);
}

void addZone(GameState& s,Zone z){if(s.zones.size()<MAX_ZONES){z.id=nextId(s);s.zones.push_back(std::move(z));}}

void castAltSpecial(GameState& s,Player& p,Random& rnd,Event& ev){
  if(p.color==0){Zone z;z.x=p.x;z.y=p.y;z.radius=230;z.ttl=3.5;z.kind="hail";z.dps=p.damage*2.4;z.slow=true;z.owner=p.id;z.color=0;addZone(s,z);}
  else if(p.color==1){Zone z;z.x=p.x;z.y=p.y;z.radius=125;z.ttl=5;z.kind="flameshield";z.dps=p.damage*3;z.follow=true;z.owner=p.id;z.color=1;addZone(s,z);}
  else if(p.color==2){for(auto& [_,a]:s.players)if(a.alive&&distanceSq({a.x,a.y},{p.x,p.y})<320.0*320.0)a.hp=std::min(a.maxHp,a.hp+a.maxHp*.3*healingScale(s));for(int n=0;n<16&&s.shots.size()<MAX_SHOTS;n++)s.shots.push_back(playerShot(p,n*PI/8,true));}
  else {Enemy* t=nearestEnemy(s,{p.x,p.y},450);auto d=dashDirection(p,{p.input.x,p.input.y});double x=t?t->x:p.x+d.x*200,y=t?t->y:p.y+d.y*200;Zone z;z.x=x;z.y=y;z.radius=230;z.ttl=2.2;z.kind="vortex";z.dps=p.damage;z.pull=260;z.damage=p.damage*8;z.owner=p.id;z.color=3;addZone(s,z);ev.x=x;ev.y=y;}
}

void addBurnZone(GameState& s,Player& p,double x,double y){double radius=rankOf(p,"hellfire")?80:55,ttl=rankOf(p,"hellfire")?3:2,dps=p.damage*(rankOf(p,"hellfire")?.75:.5);for(auto& z:s.zones)if(z.owner==p.id&&distanceSq({z.x,z.y},{x,y})<900){z.ttl=ttl;return;}Zone z;z.x=x;z.y=y;z.radius=radius;z.ttl=ttl;z.dps=dps;z.kind="burn";z.owner=p.id;z.color=1;addZone(s,z);}

void updatePlayerAttacks(GameState& s,const PlayerList& alive){if(s.enemies.empty())return;for(Player* p:alive){if(!p->pendingPowers.empty()||p->attackCooldown>0)continue;Enemy* t=nearestEnemy(s,{p->x,p->y},900);if(!t)continue;int bond=0;for(auto* q:alive)if(distanceSq({p->x,p->y},{q->x,q->y})<240.0*240.0||p==q)bond=std::max(bond,rankOf(*q,"bond"));p->attackCooldown=p->attackDelay*(1-bond*.08);double base=std::atan2(t->y-p->y,t->x-p->x);if(s.shots.size()<MAX_SHOTS){++p->castCount;p->castAngle=base;}for(int n=0;n<p->projectiles&&s.shots.size()<MAX_SHOTS;n++)s.shots.push_back(playerShot(*p,base+(n-(p->projectiles-1)/2.0)*.16));}}

double hitRadius(const Enemy& e,const Spell& sp){if(e.boss)return enemyDefs().at(e.type).radius+sp.radius-29;return e.elite?sp.radius+8:sp.radius;}

void updateShots(GameState& s,double dt,Random& rnd,EnemyGrid& enemyGrid){
  for(auto& sh:s.shots){
    Player* owner=nullptr;
    if(!sh.owner.empty()){
      auto it=s.players.find(sh.owner);
      if(it!=s.players.end()) owner=&it->second;
    }
    const auto& sp=SPELLS[std::clamp(sh.color,0,3)];
    if(sh.boomerang&&owner&&owner->alive){
      if(!sh.returning&&sh.ttl<=.8){
        sh.returning=true;
        sh.hitIds.clear();
        sh.pierce=sp.pierce;
        if(sh.fullmoon)sh.damage*=1.6;
      }
      if(sh.returning){
        double a=std::atan2(owner->y-sh.y,owner->x-sh.x);
        sh.vx=std::cos(a)*sp.speed;
        sh.vy=std::sin(a)*sp.speed;
        if(distanceSq({sh.x,sh.y},{owner->x,owner->y})<900)sh.ttl=0;
      }
    }
    sh.x+=sh.vx*dt;
    sh.y+=sh.vy*dt;
    sh.ttl-=dt;
    if(sh.ttl<=0)continue;

    // Projectiles only pierce a handful of targets. Selecting the nearest hit
    // in-place avoids allocating/sorting a vector for every projectile/frame.
    const double reach=(sh.fullmoon&&sh.returning)?1.35:1;
    while(sh.ttl>0 && sh.pierce>0){
      Enemy* hit=nullptr;
      double best=std::numeric_limits<double>::max();
      // 115 px covers the largest boss hitbox with Full Moon's return multiplier.
      enemyGrid.query(static_cast<float>(sh.x),static_cast<float>(sh.y),115.0f,[&](Enemy& e,float d2){
        if(e.hp<=0||std::find(sh.hitIds.begin(),sh.hitIds.end(),e.id)!=sh.hitIds.end())return false;
        const double r=hitRadius(e,sp)*reach;
        if(d2<r*r&&d2<best){best=d2;hit=&e;}
        return false;
      });
      if(!hit)break;
      sh.hitIds.push_back(hit->id);
      const char* kind=sh.special?"special":sh.shard?"shatter":sh.returning?"boomerang":"spell";
      const char* elem=sh.color==1?"fire":sh.color==3?"moon":"";
      damageEnemy(s,*hit,sh.damage,rnd,owner,sp.slow,sh.shard,elem,kind);
      if(sp.splash>0){
        const double hx=hit->x,hy=hit->y;
        enemyGrid.query(static_cast<float>(hx),static_cast<float>(hy),static_cast<float>(sp.splash),[&](Enemy& o,float){
          if(&o!=hit&&o.hp>0)damageEnemy(s,o,sh.damage*.6,rnd,owner,false,false,"fire",kind);
          return false;
        });
        if(owner&&rankOf(*owner,"burn"))addBurnZone(s,*owner,hx,hy);
      }
      if(--sh.pierce<=0){sh.ttl=0;break;}
      if(owner&&sh.color==2&&rankOf(*owner,"ricochet")){
        Enemy* target=nullptr;double bd=220.0*220.0;
        enemyGrid.query(static_cast<float>(hit->x),static_cast<float>(hit->y),220.0f,[&](Enemy& o,float d2){
          if(o.hp>0&&std::find(sh.hitIds.begin(),sh.hitIds.end(),o.id)==sh.hitIds.end()&&d2<bd){bd=d2;target=&o;}
          return false;
        });
        if(target){double speed=std::hypot(sh.vx,sh.vy),a=std::atan2(target->y-sh.y,target->x-sh.x);sh.vx=std::cos(a)*speed;sh.vy=std::sin(a)*speed;}
        break;
      }
    }
  }
  retain(s.shots,[](const Shot& q){return q.ttl>0;},MAX_SHOTS);
}

bool hitOnce(Enemy& e,int playerSlot,double time,double every){
  const auto slot=static_cast<std::size_t>(std::clamp(playerSlot,0,cfg::MAX_PLAYERS-1));
  if(e.orbitHitUntil[slot]>time)return false;
  e.orbitHitUntil[slot]=time+every;
  return true;
}

void updateOrbit(GameState& s,Player& p,int rank,double dt,Random& rnd){bool evolved=rankOf(p,"constellation"),solar=rankOf(p,"solarcrown");static const int counts[5]={1,2,2,3,3};int count=counts[rank-1]+(evolved?2:0);double radius=78+rank*4+(evolved?20:0),damage=p.damage*(.55+.1*rank)*(evolved?2:1)*(solar?1.3:1);p.orbitAngle=std::fmod(p.orbitAngle+2.7*dt,PI*2);for(int n=0;n<count;n++){double a=p.orbitAngle+n*PI*2/count,x=p.x+std::cos(a)*radius,y=p.y+std::sin(a)*radius,r=16+(evolved?26:18);for(auto& e:s.enemies)if(e.hp>0&&distanceSq({x,y},{e.x,e.y})<r*r&&hitOnce(e,p.color,s.time,.5))damageEnemy(s,e,damage,rnd,&p,false,false,solar?"fire":"","orbit");}}
void updateAura(GameState& s,Player& p,int rank,double dt,Random& rnd,const PlayerList& alive){p.auraTimer-=dt;if(p.auraTimer>0)return;p.auraTimer=.5;bool evolved=rankOf(p,"sanctuary");double radius=70+rank*10+(evolved?30:0),damage=p.damage*(.25+.08*rank);for(auto& e:s.enemies)if(e.hp>0&&distanceSq({p.x,p.y},{e.x,e.y})<radius*radius)damageEnemy(s,e,damage,rnd,&p,evolved&&!e.boss,false,"","aura");if(evolved)for(auto* a:alive)if(distanceSq({a->x,a->y},{p.x,p.y})<radius*radius&&a->sanctuaryAt<=s.time){a->hp=std::min(a->maxHp,a->hp+2);a->sanctuaryAt=s.time+.5;}}
void updateChain(GameState& s,Player& p,int rank,double dt,Random& rnd){
  p.chainTimer-=dt;if(p.chainTimer>0)return;
  Enemy* target=nearestEnemy(s,{p.x,p.y},260);
  if(!target){p.chainTimer=.25;return;}
  bool evolved=rankOf(p,"tempest");
  p.chainTimer=evolved?1:std::max(.9,2.4-.2*rank);
  int jumps=evolved?8:1+rank;
  double damage=p.damage*(.8+.1*rank);
  native::StaticVector<std::uint64_t,12> hit;
  Vec2 from{p.x,p.y};Event ev;ev.kind="chain";ev.color=p.color;ev.points={p.x,p.y};
  for(int n=0;n<=jumps&&target;n++){
    hit.push_back(target->id);ev.points.push_back(target->x);ev.points.push_back(target->y);
    damageEnemy(s,*target,damage,rnd,&p,false,false,"lightning","chain");from={target->x,target->y};
    target=nullptr;double bd=150.0*150.0;
    for(auto& e:s.enemies)if(e.hp>0&&std::find(hit.begin(),hit.end(),e.id)==hit.end()){
      double d=distanceSq(from,{e.x,e.y});if(d<bd){bd=d;target=&e;}
    }
  }
  pushEvent(s,std::move(ev));
}

void updateRunes(GameState& s,Player& p,int rank,double dt){p.runeTimer-=dt;if(p.runeTimer>0)return;p.runeTimer=3.2-.25*rank;bool evolved=rankOf(p,"minefield");int count=evolved?3:1;double radius=70+8*rank+(evolved?30:0);for(int n=0;n<count&&s.runes.size()<MAX_RUNES;n++){double a=n*PI*2/count+s.time,off=count>1?60:0;s.runes.push_back({nextId(s),p.x+std::cos(a)*off,p.y+std::sin(a)*off,radius,p.damage*(1.6+.3*rank),8,.4,p.id,p.color});}}
void updateFamiliar(GameState& s,Player& p,int rank,double dt,Random& rnd){
  double a=s.time*1.4+p.color;Vec2 home{p.x+std::cos(a)*62,p.y+std::sin(a)*62*.55-26};
  if(!p.familiar)p.familiar=Familiar{home.x,home.y,.4};auto& pet=*p.familiar;
  if(distanceSq({pet.x,pet.y},{p.x,p.y})>160000){pet.x=home.x;pet.y=home.y;}
  double follow=1-std::exp(-6*dt);pet.x+=(home.x-pet.x)*follow;pet.y+=(home.y-pet.y)*follow;pet.timer-=dt;if(pet.timer>0)return;
  bool evo=rankOf(p,"covenant");static const int targets[5]={1,1,2,2,3};
  const int wanted=std::min(5,targets[rank-1]+(evo?2:0));
  std::array<std::pair<double,Enemy*>,5> found{};int foundCount=0;
  for(auto& e:s.enemies)if(e.hp>0){
    const double d=distanceSq({pet.x,pet.y},{e.x,e.y});if(d>=380.0*380.0)continue;
    int insert=std::min(foundCount,wanted-1);
    if(foundCount>=wanted && d>=found[insert].first)continue;
    if(foundCount<wanted)++foundCount;
    while(insert>0&&d<found[insert-1].first){if(insert<foundCount)found[insert]=found[insert-1];--insert;}
    found[insert]={d,&e};
  }
  if(!foundCount){pet.timer=.2;return;}
  pet.timer=std::max(.35,1.3-.12*rank)*(evo?.6:1);
  double damage=p.damage*(.9+.22*rank)*(evo?1.4:1);Event ev;ev.kind="familiar";ev.x=pet.x;ev.y=pet.y;ev.color=p.color;
  for(int i=0;i<foundCount;i++){
    auto* e=found[i].second;const char* elem=p.color==1?"fire":p.color==3?"moon":"";
    damageEnemy(s,*e,damage,rnd,&p,p.color==0,false,elem,"familiar");
    if(p.color==2&&!e->boss){e->rootFor=std::max(e->rootFor,.35);e->rootBy=p.id;}
    ev.points.push_back(e->x);ev.points.push_back(e->y);
  }
  if(!ev.points.empty()){pet.x+=(ev.points[0]-pet.x)*.35;pet.y+=(ev.points[1]-pet.y)*.35;}pushEvent(s,std::move(ev));
}


void updateWeapons(GameState& s,double dt,Random& rnd,const PlayerList& alive){
  for(auto* p:alive){if(!p->pendingPowers.empty())continue;if(int r=rankOf(*p,"orbit"))updateOrbit(s,*p,r,dt,rnd);if(int r=rankOf(*p,"aura"))updateAura(s,*p,r,dt,rnd,alive);if(int r=rankOf(*p,"chain"))updateChain(s,*p,r,dt,rnd);if(int r=rankOf(*p,"runes"))updateRunes(s,*p,r,dt);if(int r=rankOf(*p,"familiar"))updateFamiliar(s,*p,r,dt,rnd);}
  for(auto& rune:s.runes){rune.ttl-=dt;rune.arm-=dt;if(rune.arm>0||rune.ttl<=0)continue;bool trigger=false;for(auto& e:s.enemies)if(e.hp>0&&distanceSq({rune.x,rune.y},{e.x,e.y})<46.0*46.0){trigger=true;break;}if(!trigger)continue;rune.ttl=0;Player* owner=nullptr;auto oi=s.players.find(rune.owner);if(oi!=s.players.end())owner=&oi->second;for(auto& e:s.enemies)if(e.hp>0&&distanceSq({rune.x,rune.y},{e.x,e.y})<rune.radius*rune.radius)damageEnemy(s,e,rune.damage,rnd,owner,false,false,"fire","runes");pushEvent(s,"boom",rune.x,rune.y,rune.color);
    if(owner&&rankOf(*owner,"stormrunes")){native::StaticVector<std::uint64_t,4> hit;Vec2 from{rune.x,rune.y};Event ev;ev.kind="chain";ev.color=owner->color;ev.points={from.x,from.y};for(int n=0;n<3;n++){Enemy* t=nullptr;double bd=170.0*170.0;for(auto& e:s.enemies)if(e.hp>0&&std::find(hit.begin(),hit.end(),e.id)==hit.end()){double d=distanceSq(from,{e.x,e.y});if(d<bd){bd=d;t=&e;}}if(!t)break;hit.push_back(t->id);ev.points.push_back(t->x);ev.points.push_back(t->y);damageEnemy(s,*t,owner->damage*.7,rnd,owner,false,false,"lightning","runes");from={t->x,t->y};}if(ev.points.size()>2)pushEvent(s,std::move(ev));}}
  retain(s.runes,[](const Rune&r){return r.ttl>0;});
  for(auto& z:s.zones){z.ttl-=dt;Player* owner=nullptr;auto oi=s.players.find(z.owner);if(oi!=s.players.end())owner=&oi->second;if(z.follow){if(!owner||!owner->alive){z.ttl=0;continue;}z.x=owner->x;z.y=owner->y;retain(s.enemyShots,[&](const EnemyShot& sh){return distanceSq({sh.x,sh.y},{z.x,z.y})>z.radius*z.radius;});}if(z.kind=="meteor"&&z.warning>0){z.warning-=dt;if(z.warning>0)continue;for(auto& e:s.enemies)if(e.hp>0&&distanceSq({z.x,z.y},{e.x,e.y})<z.radius*z.radius)damageEnemy(s,e,z.damage,rnd,owner,false,false,"fire","special");pushEvent(s,"boom",z.x,z.y,1);}std::string kind=(z.kind!="burn"&&!z.kind.empty())?"special":"burn",elem=(z.kind=="burn"||z.kind=="meteor"||z.kind=="flameshield")?"fire":z.kind=="vortex"?"moon":"";for(auto& e:s.enemies)if(e.hp>0){double d=distanceSq({z.x,z.y},{e.x,e.y});if(d<z.radius*z.radius){if(z.kind=="roots"){e.rootFor=.5;if(owner)e.rootBy=owner->id;}if(z.pull>0&&!e.boss&&d>400){double dist=std::sqrt(d),step=std::min(dist-20,z.pull*dt);e.x+=(z.x-e.x)/dist*step;e.y+=(z.y-e.y)/dist*step;}damageEnemy(s,e,z.dps*dt,rnd,owner,z.slow,false,elem,kind);}}if(z.kind=="vortex"&&z.ttl<=0){for(auto& e:s.enemies)if(e.hp>0&&distanceSq({z.x,z.y},{e.x,e.y})<z.radius*z.radius)damageEnemy(s,e,z.damage,rnd,owner,false,false,"moon","special");pushEvent(s,"boom",z.x,z.y,3);}}
  retain(s.zones,[](const Zone&z){return z.ttl>0;});
}

} // namespace

bool activateSpecial(GameState& s,const std::string& id,Random& rnd){auto it=s.players.find(id);if(it==s.players.end())return false;auto& p=it->second;if(!p.alive||s.over||s.phaseStatus=="transition"||!p.pendingPowers.empty()||p.specialCharge<100||p.specialCooldown>0)return false;bool alt=p.specialVariant==1;if((alt&&p.color==2&&s.shots.size()+16>MAX_SHOTS)||(!alt&&p.color==3&&s.shots.size()+8>MAX_SHOTS))return false;if((alt?p.color!=2:(p.color==1||p.color==2))&&s.zones.size()>=MAX_ZONES)return false;p.specialCharge=0;p.specialCooldown=8;++p.castCount;Event ev;ev.kind="special";ev.x=p.x;ev.y=p.y;ev.color=p.color;ev.variant=alt?1:0;
 if(alt)castAltSpecial(s,p,rnd,ev);else if(p.color==0){for(auto& e:s.enemies)if(e.hp>0&&distanceSq({p.x,p.y},{e.x,e.y})<280.0*280.0){e.freezeFor=e.boss?0:2;damageEnemy(s,e,p.damage*4,rnd,&p,true,false,"","special");}retain(s.enemyShots,[&](const EnemyShot& sh){return distanceSq({p.x,p.y},{sh.x,sh.y})>220.0*220.0;});}
 else if(p.color==1){Enemy* t=nearestEnemy(s,{p.x,p.y},500);auto d=dashDirection(p,{p.input.x,p.input.y});Zone z;z.x=t?t->x:p.x+d.x*180;z.y=t?t->y:p.y+d.y*180;z.radius=165;z.ttl=3.6;z.warning=.6;z.kind="meteor";z.damage=p.damage*9;z.dps=p.damage*.5;z.owner=p.id;z.color=1;addZone(s,z);ev.x=z.x;ev.y=z.y;}
 else if(p.color==2){Zone z;z.x=p.x;z.y=p.y;z.radius=190;z.ttl=4;z.kind="roots";z.dps=p.damage*2;z.owner=p.id;z.color=2;addZone(s,z);}
 else {auto d=dashDirection(p,{p.input.x,p.input.y});double fx=p.x,fy=p.y;p.x+=d.x*170;p.y+=d.y*170;++p.motionId;p.invulnerableFor=std::max(p.invulnerableFor,.3);for(int n=0;n<8;n++){auto sh=playerShot(p,n*PI/4,true);sh.x=fx;sh.y=fy;sh.boomerang=true;s.shots.push_back(std::move(sh));}ev.x=p.x;ev.y=p.y;}
 pushEvent(s,std::move(ev));converge(s,p,rnd);return true;}
bool activateSpecial(GameState& s,const std::string& id){static DefaultRandom rng;return activateSpecial(s,id,rng);}

namespace {

std::string pickType(int phase,double clock,Random& rnd){double roll=rnd.next(),cursor=0;struct S{const char*t;double after,w;};native::StaticVector<S,2> specials;switch(phase){case 0:specials={{"slime",60,.18}};break;case 1:specials={{"eye",45,.16}};break;case 2:specials={{"bat",45,.16},{"brute",150,.08}};break;case 3:specials={{"slime",45,.18},{"scorpion",150,.08}};break;case 4:specials={{"revenant",60,.16},{"brute",150,.08}};break;default:specials={{"seer",45,.18},{"revenant",150,.1}};}
 for(auto& sp:specials){if(clock<sp.after)continue;cursor+=sp.w;if(roll<cursor)return sp.t;}double core=(roll-cursor)/(1-cursor);return phases()[phase].enemies[core<.6?0:1];}
Enemy* spawnAround(GameState& s,const Difficulty& d,Player& focus,double angle,const std::string& type,Random& rnd,double distance=-1,bool elite=false){double dist=distance>=0?distance:520+rnd.next()*120;return spawnEnemy(s,type,focus.x+std::cos(angle)*dist,focus.y+std::sin(angle)*dist,d.hpScale,elite);}
void runSchedule(GameState& s,const Difficulty& d,const PlayerList& alive,Random& rnd){static const std::array<std::pair<double,const char*>,6> beats={{{6,"opening"},{90,"elite"},{135,"ring"},{180,"elite"},{230,"ring"},{270,"elite"}}};while(s.scheduleCursor<beats.size()&&phaseClock(s)>=beats[s.scheduleCursor].first){auto [at,kind]=beats[s.scheduleCursor++];if(phaseClock(s)-at>2)continue;if(std::string(kind)=="opening"){for(auto* p:alive)for(int n=0;n<8;n++)spawnAround(s,d,*p,n*PI/4+rnd.next()*.3,phases()[s.phase].enemies[0],rnd);}else if(std::string(kind)=="elite"){int count=hasCurse(s,"nobility")?2:1;for(int n=0;n<count;n++){auto* p=alive[(std::size_t)(rnd.next()*alive.size())%alive.size()];auto type=phases()[s.phase].enemies[(std::size_t)(rnd.next()*2)%2];if(auto* e=spawnAround(s,d,*p,rnd.next()*PI*2,type,rnd,480,true))pushEvent(s,"elite",e->x,e->y);}}else{auto* p=alive[(std::size_t)(rnd.next()*alive.size())%alive.size()];int count=std::min<int>(16+(int)alive.size()*4,MAX_ENEMIES-(int)s.enemies.size());for(int n=0;n<count;n++)spawnAround(s,d,*p,n*PI*2/count,phases()[s.phase].enemies[1],rnd,440);if(count)pushEvent(s,"ring",p->x,p->y);}}}
void spawnHorde(GameState& s,double dt,const Difficulty& d,const PlayerList& alive,Random& rnd){runSchedule(s,d,alive,rnd);s.spawn-=dt;double crowd=hasCurse(s,"swarm")?1.2:1;int limit=std::min(MAX_ENEMIES,(int)std::floor((45+std::floor(phaseClock(s)*.3)+s.phase*20+alive.size()*18)*crowd));if(s.spawn>0||(int)s.enemies.size()>=limit)return;s.spawn=d.spawnInterval;int batch=std::min({d.spawnCount,limit-(int)s.enemies.size(),6});double off=rnd.next()*PI*2;for(int n=0;n<batch;n++){auto* p=alive[(s.spawnCursor+n)%alive.size()];double a=off+n*(PI*2/batch)+rnd.next()*.25;spawnAround(s,d,*p,a,pickType(s.phase,phaseClock(s),rnd),rnd);}s.spawnCursor=(s.spawnCursor+batch)%alive.size();}

void addHazard(GameState& s,Hazard h){if(s.hazards.size()<MAX_HAZARDS){h.warn0=h.warning;s.hazards.push_back(std::move(h));}}
void fireBoss(GameState& s,const Enemy& e,double a,const std::string& sprite,double speed){if(s.enemyShots.size()>=MAX_ENEMY_SHOTS)return;auto& d=enemyDefs().at(e.type);s.enemyShots.push_back({e.x,e.y,std::cos(a)*speed,std::sin(a)*speed,4,d.damage*.5,10,sprite});}
int stageOf(const Enemy& e){double r=e.hp/e.maxHp;return r>.66?1:r>.33?2:3;}
void enterStage(GameState& s,Enemy& e,int stage,const Difficulty& diff,const PlayerList& alive,Random& rnd){e.stage=stage;auto& def=enemyDefs().at(e.type);Hazard h;h.x=e.x;h.y=e.y;h.radius=210;h.warning=1;h.ttl=1.35;h.damage=def.damage+12;addHazard(s,h);int count=4+(int)alive.size()*2;for(int n=0;n<count;n++){double a=n*PI*2/count+rnd.next()*.2;spawnEnemy(s,phases()[s.phase].enemies[0],e.x+std::cos(a)*240,e.y+std::sin(a)*240,1+s.phase*.8,false,true);}e.attackCooldown=std::min(e.attackCooldown,2.0);Event ev;ev.kind="stage";ev.x=e.x;ev.y=e.y;ev.stage=stage;pushEvent(s,std::move(ev));}
void areaAttack(GameState& s,Enemy& e,Player& target,const PlayerList& alive,Random& rnd){double damage=enemyDefs().at(e.type).damage+8;auto hazard=[&](double x,double y,double r,double warning=1.3){Hazard h;h.x=x;h.y=y;h.radius=r;h.damage=damage;h.warning=warning;h.ttl=warning+.35;addHazard(s,h);};int stage=e.stage;if(e.type=="treant"){hazard(e.x,e.y,175);if(stage>=2){double a=std::atan2(target.y-e.y,target.x-e.x);for(int n=1;n<=4;n++)hazard(e.x+std::cos(a)*(140+n*110),e.y+std::sin(a)*(140+n*110),70,1+n*.15);}}
 else if(e.type=="lich"){if(stage>=3){for(auto* p:alive)hazard(p->x,p->y,100);}else{hazard(target.x,target.y,100);if(stage>=2&&alive.size()==1)hazard(target.x+(rnd.next()-.5)*300,target.y+(rnd.next()-.5)*300,100);}}
 else if(e.type=="bogwarden"){int count=stage==3?8:6;for(int n=0;n<count;n++){double a=n*PI*2/count;hazard(target.x+std::cos(a)*170,target.y+std::sin(a)*170,62,1.4+n*.08);}if(stage>=2)hazard(target.x,target.y,75,1.8);}
 else if(e.type=="archon"){static constexpr std::array<Vec2,9> c={{{0,0},{-150,0},{150,0},{0,-150},{0,150},{-150,-150},{150,-150},{-150,150},{150,150}}};int count=stage==3?9:5;for(int i=0;i<count;i++){auto q=c[i];hazard(target.x+q.x,target.y+q.y,65,1.4);}}
 else if(e.type=="umbra"){double a=std::atan2(target.y-e.y,target.x-e.x);for(int line=0;line<(stage>=2?2:1);line++){double dir=a+line*PI/2;for(int n=-2;n<=2;n++){if(line&&n==0)continue;hazard(target.x+std::cos(dir)*n*120,target.y+std::sin(dir)*n*120,58,1.1+(n+2)*.18);}}}
 else {for(int n=-1;n<=1;n++)hazard(target.x+n*140,target.y,100);if(stage>=3)for(int n=0;n<3;n++)hazard(target.x+(rnd.next()-.5)*520,target.y+(rnd.next()-.5)*520,80,1.1);}}
void rangedAttack(GameState& s,Enemy& e,Player& target){int stage=e.stage;double angle=std::atan2(target.y-e.y,target.x-e.x);std::string sprite=(e.type=="treant"||e.type=="bogwarden")?"thorn":(e.type=="lich"||e.type=="archon")?"bolt":e.type=="umbra"?"blade":"fire";double speed=(e.type=="lich"||e.type=="archon")?230:200;auto radial=[&](int c,double o=0){for(int n=0;n<c;n++)fireBoss(s,e,o+n*PI*2/c,sprite,speed*.85);};auto fan=[&](int c){for(int n=0;n<c;n++)fireBoss(s,e,angle+(n-(c-1)/2.0)*.23,sprite,speed);};if(e.type=="bogwarden"){fan(3+stage*2);if(stage==3)radial(8,s.time);}else if(e.type=="archon"){radial(4+stage*4,s.time*.45);if(stage==3)fan(3);}else if(e.type=="umbra"){radial(6+stage*4,s.time*.6);if(stage>=2)fan(5);}else if(stage==1)fan(e.type=="demon"?5:3);else if(e.type=="lich")radial(stage==2?12:16,s.time);else if(e.type=="treant"){fan(5);if(stage==3)radial(10,s.time);}else fan(7);}
void bossBrain(GameState& s,Enemy& e,Player& target,double dt,const Difficulty& diff,const PlayerList& alive,Random& rnd){int st=stageOf(e);if(st>e.stage)enterStage(s,e,st,diff,alive,rnd);static const double pace[3]={1,.8,.65};static const std::unordered_map<std::string,double> area={{"treant",4.5},{"lich",4.5},{"demon",3.6},{"bogwarden",4.8},{"archon",4.5},{"umbra",4.6}},range={{"treant",3.6},{"lich",3.2},{"demon",2.6},{"bogwarden",3.4},{"archon",3.6},{"umbra",3.2}};e.attackCooldown-=dt;if(e.attackCooldown<=0){e.attackCooldown=area.at(e.type)*pace[e.stage-1];areaAttack(s,e,target,alive,rnd);}e.rangedCooldown-=dt;if(e.rangedCooldown<=0){e.rangedCooldown=range.at(e.type)*pace[e.stage-1];rangedAttack(s,e,target);}if((e.type=="demon"&&e.stage>=2)||(e.type=="umbra"&&e.stage==3)){e.dashTimer-=dt;if(e.dashWarn>0){e.dashWarn-=dt;if(e.dashWarn<=0){e.dash=.5;e.dashSpeed=560;}}else if(e.dash>0)e.dash-=dt;else if(e.dashTimer<=0){e.dashTimer=6;e.dashWarn=.8;e.dashAngle=std::atan2(target.y-e.y,target.x-e.x);pushEvent(s,"dash",e.x,e.y);}}}

void behaveEnemy(GameState& s,Enemy& e,Player& target,double& angle,double& speed,double dt,const Difficulty& diff,Random& rnd,bool& move){const auto& def=enemyDefs().at(e.type);double d2=distanceSq({e.x,e.y},{target.x,target.y});if(def.behavior=="charger"){e.chargeTimer=(e.chargeTimer>0?e.chargeTimer:3.4*(.5+(e.id%7)/7.0))-dt;if(e.windup>0){e.windup-=dt;if(e.windup<=0)e.dash=.45;move=false;return;}if(e.dash>0){e.dash-=dt;angle=e.dashAngle;speed*=3.2;return;}if(e.chargeTimer<=0&&d2<320.0*320.0){e.chargeTimer=3.4;e.windup=.5;e.dashAngle=angle;move=false;return;}}
 else if(def.behavior=="shooter"){e.shootTimer=(e.shootTimer>0?e.shootTimer:3.5)-dt;if(e.shootTimer<=0&&d2<520.0*520.0&&s.enemyShots.size()<MAX_ENEMY_SHOTS){e.shootTimer=3.5;s.enemyShots.push_back({e.x,e.y,std::cos(angle)*170,std::sin(angle)*170,4,def.damage*diff.damageScale*.73,12,def.shotSprite.empty()?"bolt":def.shotSprite});}if(d2<260.0*260.0){angle+=PI;speed*=.6;}else if(d2<320.0*320.0){angle+=PI/2;speed*=.5;}}
 else if(def.behavior=="bomber"){if(e.fuse>0){e.fuse-=dt;if(e.fuse<=0){e.hp=0;e.exploded=true;double damage=def.damage*2.2*diff.damageScale;for(auto& [_,p]:s.players)if(p.alive&&distanceSq({e.x,e.y},{p.x,p.y})<70.0*70.0)hurt(s,p,damage);pushEvent(s,"boom",e.x,e.y);}move=false;return;}if(d2<60.0*60.0){e.fuse=.6;move=false;return;}}}

void updateEnemies(GameState& s,double dt,const Difficulty& diff,const PlayerList& alive,Random& rnd,EnemyGrid& separationGrid){++s.tick;for(auto& e:s.enemies){if(e.hp<=0)continue;e.age+=dt;e.burningFor=std::max(0.0,e.burningFor-dt);e.rootFor=std::max(0.0,e.rootFor-dt);e.freezeFor=std::max(0.0,e.freezeFor-dt);if(!e.boss&&e.freezeFor>0)continue;Player* target=nullptr;double bd=1e300;for(auto* p:alive){double d=distanceSq({e.x,e.y},{p->x,p->y});if(d<bd){bd=d;target=p;}}if(!target)continue;const auto& def=enemyDefs().at(e.type);double angle=std::atan2(target->y-e.y,target->x-e.x);e.slowFor=std::max(0.0,e.slowFor-dt);e.touchCooldown=std::max(0.0,e.touchCooldown-dt);double slow=e.slowFor>0?(e.boss?.85:.6):1,speed=def.speed*slow*diff.speedScale*(e.elite?.9:1);if(e.rootFor>0)speed*=e.boss?.75:0;bool move=true;if(e.boss){bossBrain(s,e,*target,dt,diff,alive,rnd);if(e.dash>0)speed=e.dashSpeed;double heading=e.dash>0?e.dashAngle:angle;if(e.dashWarn<=0){e.x+=std::cos(heading)*speed*dt;e.y+=std::sin(heading)*speed*dt;}}
 else if(e.thief){double flee=angle+PI+std::sin(e.age*2.3)*.6,pace=def.speed*1.45*slow*(e.rootFor>0?0:1);e.x+=std::cos(flee)*pace*dt;e.y+=std::sin(flee)*pace*dt;continue;}else{behaveEnemy(s,e,*target,angle,speed,dt,diff,rnd,move);if(move){e.x+=std::cos(angle)*speed*dt;e.y+=std::sin(angle)*speed*dt;}}
 if(e.hp<=0)continue;double reach=e.boss?def.radius+15:e.elite?44:34;if(e.touchCooldown<=0&&distanceSq({e.x,e.y},{target->x,target->y})<reach*reach){double mult=(e.elite?1.5:1)*(e.dash>0?1.5:1);if(hurt(s,*target,def.damage*diff.damageScale*.73*mult))e.touchCooldown=.8;}}
 // soft separation: uniform grid avoids the previous O(N^2) all-pairs scan.
 separationGrid.rebuild(s.enemies, [](const Enemy& e){ return e.hp > 0 && !e.distant; });
 for(std::size_t i=0;i<s.enemies.size();i++){auto& a=s.enemies[i];if(a.hp<=0||a.boss||enemyDefs().at(a.type).behavior=="flier")continue;double px=0,py=0;separationGrid.query(static_cast<float>(a.x),static_cast<float>(a.y),30.0f,[&](Enemy& b,float d2){if(&a==&b||b.hp<=0||b.boss||d2<=0)return false;double d=std::sqrt(static_cast<double>(d2)),push=(30-d)/30;px+=(a.x-b.x)/d*push;py+=(a.y-b.y)/d*push;return false;});a.x+=px*7.5;a.y+=py*7.5;}
 separationGrid.rebuild(s.enemies, [](const Enemy& e){ return e.hp > 0; });}
void updateEnemyShots(GameState& s,double dt,const PlayerList& alive){for(auto& sh:s.enemyShots){sh.x+=sh.vx*dt;sh.y+=sh.vy*dt;sh.ttl-=dt;if(sh.ttl<=0)continue;for(auto* p:alive)if(p->alive&&distanceSq({sh.x,sh.y},{p->x,p->y})<(sh.radius+18)*(sh.radius+18)){hurt(s,*p,sh.damage);sh.ttl=0;break;}}retain(s.enemyShots,[](const EnemyShot&q){return q.ttl>0;},MAX_ENEMY_SHOTS);}

void summonBoss(GameState& s,const PlayerList& alive){if(alive.empty())return;const auto& camp=campaignOf(s);double hpBase[]={4950,15400,28600,44000,63800,83600};double hp=hpBase[s.phase]*camp.bossHp*(1+(alive.size()-1)*.6)*(1+s.loop*.9)*(hasCurse(s,"tyrant")?1.4:1);s.altar.reset();s.enemies.clear();Enemy e;e.id=nextId(s);e.type=phases()[s.phase].boss;e.boss=true;e.hp=e.maxHp=hp;e.x=alive[0]->x+330;e.y=alive[0]->y-180;s.enemies.push_back(std::move(e));s.shots.clear();s.phaseStatus="boss";pushEvent(s,"boss",s.enemies[0].x,s.enemies[0].y);}

void updateObjective(GameState& s,double dt,const Difficulty& diff,const PlayerList& alive,Random& rnd){if(s.phaseStatus!="horde")return;if(!s.altar&&!s.altarSpawned&&s.phaseTime>=phaseDuration(s)*.3&&!alive.empty()){auto* f=alive[0];double a=rnd.next()*PI*2;s.altarSpawned=true;s.altar=Altar{f->x+std::cos(a)*300,f->y+std::sin(a)*300,120,0,45,"waiting",0};pushEvent(s,"altar",s.altar->x,s.altar->y);}if(!s.altar||s.altar->status=="complete"||s.altar->status=="expired")return;auto& a=*s.altar;a.ttl-=dt;if(a.ttl<=0){a.status="expired";pushEvent(s,"altarExpired");return;}bool defending=false;for(auto* p:alive)if(p->pendingPowers.empty()&&distanceSq({p->x,p->y},{a.x,a.y})<a.radius*a.radius){defending=true;break;}if(!defending)return;a.status="active";a.progress=std::min(15.0,a.progress+dt);if(a.progress>=a.wave*5&&a.wave<3){++a.wave;for(int n=0;n<6;n++){double ang=n*PI/3;spawnEnemy(s,phases()[s.phase].enemies[1],a.x+std::cos(ang)*300,a.y+std::sin(ang)*300,diff.hpScale);}}if(a.progress>=15){a.status="complete";for(auto& [_,p]:s.players){++p.pendingChests;p.coins+=20;}pushEvent(s,"altarComplete",a.x,a.y);}}

void startEncounter(GameState& s,const Difficulty& diff,const PlayerList& alive,Random& rnd){static const std::array<std::string,3> kinds={"merchant","shrine","thief"};auto kind=kinds[(std::size_t)(rnd.next()*3)%3];auto* f=alive[(std::size_t)(rnd.next()*alive.size())%alive.size()];double angle=rnd.next()*PI*2;s.encounterSpawned=true;Encounter e;e.kind=kind;if(kind=="thief"){e.x=f->x+std::cos(angle)*260;e.y=f->y+std::sin(angle)*260;e.ttl=20;e.status="active";if(auto* th=spawnEnemy(s,phases()[s.phase].enemies[0],e.x,e.y,diff.hpScale*4,true,false,true))e.enemyId=th->id;else return;}else{e.x=f->x+std::cos(angle)*320;e.y=f->y+std::sin(angle)*320;e.radius=kind=="merchant"?80:90;e.ttl=40;}s.encounter=e;pushEvent(s,"encounter",e.x,e.y);}
void updateEncounter(GameState& s,double dt,const Difficulty& diff,const PlayerList& alive,Random& rnd){if(s.phaseStatus!="horde")return;if(!s.encounter&&!s.encounterSpawned&&s.phaseTime>=phaseDuration(s)*.6&&!alive.empty())startEncounter(s,diff,alive,rnd);if(!s.encounter||s.encounter->status=="complete"||s.encounter->status=="expired")return;auto& e=*s.encounter;e.ttl-=dt;if(e.kind=="thief"){auto it=std::find_if(s.enemies.begin(),s.enemies.end(),[&](const Enemy& x){return x.id==e.enemyId;});if(it!=s.enemies.end()&&it->hp<=0)return;if(it!=s.enemies.end()){e.x=it->x;e.y=it->y;}if(it!=s.enemies.end()&&e.ttl>0)return;if(it!=s.enemies.end()){it->hp=0;it->escaped=true;}e.status="expired";pushEvent(s,"thiefEscaped",e.x,e.y);return;}if(e.ttl<=0){e.status="expired";return;}if(e.kind=="merchant"){for(auto* p:alive){bool inside=p->pendingPowers.empty()&&distanceSq({p->x,p->y},{e.x,e.y})<e.radius*e.radius,already=std::find(e.buyers.begin(),e.buyers.end(),p->id)!=e.buyers.end();if(!inside||already||p->coins<20){p->shopProgress=0;continue;}e.status="active";p->shopProgress+=dt;if(p->shopProgress>=1.5){p->shopProgress=0;p->coins-=20;++p->pendingChests;e.buyers.push_back(p->id);pushEvent(s,"merchantSale",e.x,e.y);}}if(std::all_of(s.players.begin(),s.players.end(),[&](auto& kv){return std::find(e.buyers.begin(),e.buyers.end(),kv.first)!=e.buyers.end();}))e.status="complete";}
 else {bool inside=false;for(auto* p:alive)if(p->pendingPowers.empty()&&distanceSq({p->x,p->y},{e.x,e.y})<e.radius*e.radius){inside=true;break;}if(!inside)return;e.status="active";e.progress=std::min(3.0,e.progress+dt);if(e.progress>=3){e.status="complete";s.bloodPact=true;for(auto& [_,p]:s.players){++p.pendingChests;p.coins+=15;}pushEvent(s,"shrineAccepted",e.x,e.y);}}}

void updateHazards(GameState& s,double dt,const PlayerList& alive){for(auto& h:s.hazards){h.warning-=dt;h.ttl-=dt;if(h.warning<=0&&!h.fired){h.fired=true;for(auto* p:alive)if(distanceSq({h.x,h.y},{p->x,p->y})<h.radius*h.radius)hurt(s,*p,h.damage);}}retain(s.hazards,[](const Hazard&h){return h.ttl>0;});}

void collectOne(GameState& s,Drop& g,Player& p,Random& rnd){g.dead=true;if(g.type=="heart")p.hp=std::min(p.maxHp,p.hp+g.value*healingScale(s));else if(g.type=="greenGem")p.specialCharge=std::min(100.0,p.specialCharge+g.value);else if(g.type=="coin"){for(auto& [_,a]:s.players){double earned=g.value*campaignOf(s).coins*a.coinMult+a.coinFrac;a.coins+=(int)std::floor(earned);a.coinFrac=earned-std::floor(earned);}}else if(g.type=="magnet"){for(auto& o:s.gems)if(o.type=="gem"){o.pull=p.id;o.ttl=std::max(o.ttl,10.0);}pushEvent(s,"magnet",p.x,p.y);}else if(g.type=="chest"){for(auto& [_,a]:s.players){++a.pendingChests;a.coins+=5;}pushEvent(s,"chest",p.x,p.y);}else{double reward=g.value*campaignOf(s).xp;for(auto& [_,a]:s.players)grantXp(s,a,reward,rnd);}}
void collectDrops(GameState& s,double dt,const PlayerList& survivors,Random& rnd){for(auto& g:s.gems){if(g.pull.empty())g.ttl-=dt;if(g.ttl<=0||survivors.empty())continue;Player* target=nullptr;if(!g.pull.empty()){auto it=s.players.find(g.pull);if(it!=s.players.end()&&it->second.alive)target=&it->second;else g.pull.clear();}if(!target){double bd=1e300;for(auto* p:survivors){bool ok=g.type=="heart"?p->hp<p->maxHp:g.type=="greenGem"?p->specialCharge<100:true;if(!ok)continue;double d=distanceSq({g.x,g.y},{p->x,p->y});if(d<bd){bd=d;target=p;}}}if(!target)continue;double d2=distanceSq({g.x,g.y},{target->x,target->y});if(!g.pull.empty()||d2<target->pickupRadius*target->pickupRadius){double a=std::atan2(target->y-g.y,target->x-g.x),step=std::min(std::sqrt(d2),(g.pull.empty()?350.0:620.0)*dt);g.x+=std::cos(a)*step;g.y+=std::sin(a)*step;}if(distanceSq({g.x,g.y},{target->x,target->y})<24.0*24.0)collectOne(s,g,*target,rnd);}}

void revive(GameState& s,double dt){for(auto& [_,p]:s.players)p.reviving.clear();for(auto& [id,p]:s.players)if(!p.alive){Player* helper=nullptr;auto hi=s.players.find(p.reviveBy);auto can=[&](Player& q){return q.alive&&q.pendingPowers.empty()&&q.reviving.empty()&&distanceSq({q.x,q.y},{p.x,p.y})<=44.0*44.0;};if(hi!=s.players.end()&&can(hi->second))helper=&hi->second;else for(auto& [_,q]:s.players)if(can(q)){helper=&q;break;}if(!helper){p.reviveProgress=0;p.reviveBy.clear();continue;}if(p.reviveBy!=helper->id)p.reviveProgress=0;p.reviveBy=helper->id;helper->reviving=p.id;int guard=rankOf(*helper,"guardian");p.reviveProgress=std::min(4.0,p.reviveProgress+dt*(1+guard*.5));if(p.reviveProgress>=4){p.alive=true;p.hp=p.maxHp*(.4+guard*.15);p.invulnerableFor=3;p.input={};p.hitCooldown=0;p.pendingPowers.clear();p.reviveProgress=0;p.reviveBy.clear();helper->reviving.clear();++helper->stats.revives;pushEvent(s,"revive",p.x,p.y);}}}

void startNextPhase(GameState& s,const PlayerList& alive){++s.phase;if(s.phase>=6){s.phase=0;++s.loop;pushEvent(s,"loop");}s.phaseTime=0;s.phaseStatus="horde";s.spawn=0;s.scheduleCursor=0;s.altar.reset();s.altarSpawned=false;s.encounter.reset();s.encounterSpawned=false;s.bloodPact=false;for(auto* p:alive){p->hp=std::min(p->maxHp,p->hp+p->maxHp*.35*healingScale(s));p->invulnerableFor=3;++p->pendingChests;}}

} // namespace

void updateGame(GameState& s,double dt,Random& rnd){if(s.over)return;dt=std::clamp(dt,0.0,.08);PlayerList players,alive;for(auto& [_,p]:s.players){players.push_back(&p);if(p.alive)alive.push_back(&p);}if(alive.empty()){s.over=!players.empty();return;}s.time+=dt;retain(s.events,[&](const Event&e){return e.t>=s.time-1.5;});if(s.phaseStatus=="transition"){s.transitionTime=std::max(0.0,s.transitionTime-dt);if(s.transitionTime<=0)startNextPhase(s,alive);return;}if(s.phaseStatus=="horde"){s.phaseTime=std::min(phaseDuration(s),s.phaseTime+dt);if(s.phaseTime>=phaseDuration(s))summonBoss(s,alive);}Difficulty diff=difficultyAt(phaseClock(s),(int)alive.size(),s.phase,&s);
 for(auto* p:alive){if(!p->pendingPowers.empty()){p->powerTimer+=dt;if(p->powerTimer>=15)applyPower(*p,p->pendingPowers.front());}else{if(p->pendingChests>0){--p->pendingChests;if(offerPowers(*p,rnd,s.players.size()>1)){}else{p->coins+=10;p->hp=std::min(p->maxHp,p->hp+30*healingScale(s));}}grantXp(s,*p,0,rnd);}p->hitCooldown=std::max(0.0,p->hitCooldown-dt);p->invulnerableFor=std::max(0.0,p->invulnerableFor-dt);p->attackCooldown-=dt;p->dashCooldown=std::max(0.0,p->dashCooldown-dt);p->specialCooldown=std::max(0.0,p->specialCooldown-dt);if(p->pendingPowers.empty()){if(p->input.x||p->input.y){p->moveX=p->input.x;p->moveY=p->input.y;}auto d=movementDelta(*p,dt);p->x+=d.x;p->y+=d.y;}p->dashFor=std::max(0.0,p->dashFor-dt);}
 if(s.phaseStatus=="horde")spawnHorde(s,dt,diff,alive,rnd);updateObjective(s,dt,diff,alive,rnd);updateEncounter(s,dt,diff,alive,rnd);updatePlayerAttacks(s,alive);EnemyGrid enemyGrid(96.0f);updateEnemies(s,dt,diff,alive,rnd,enemyGrid);updateHazards(s,dt,alive);updateEnemyShots(s,dt,alive);updateShots(s,dt,rnd,enemyGrid);updateWeapons(s,dt,rnd,alive);retain(s.enemies,[](const Enemy&e){return e.hp>0;});
 if(s.players.size()>1)for(auto* p:alive){int r=rankOf(*p,"lifelink");if(!r||p->lifelinkAt>s.time)continue;p->lifelinkAt=s.time+2;for(auto* q:alive)if(q!=p&&distanceSq({p->x,p->y},{q->x,q->y})<240.0*240.0)q->hp=std::min(q->maxHp,q->hp+r*2*healingScale(s));}
 revive(s,dt);PlayerList survivors;for(auto& [_,p]:s.players)if(p.alive)survivors.push_back(&p);collectDrops(s,dt,survivors,rnd);s.cleanup-=dt;if(s.cleanup<=0){s.cleanup=.75;retain(s.gems,[&](const Drop&g){if(g.dead||g.ttl<=0)return false;for(auto* p:alive)if(distanceSq({g.x,g.y},{p->x,p->y})<1500.0*1500.0)return true;return false;},MAX_DROPS);retain(s.enemies,[&](const Enemy&e){if(e.boss)return true;if(e.age>=75)return false;for(auto* p:alive)if(distanceSq({e.x,e.y},{p->x,p->y})<1450.0*1450.0)return true;return false;},MAX_ENEMIES);}else retain(s.gems,[](const Drop&g){return !g.dead&&g.ttl>0;},MAX_DROPS);
 bool any=false;for(auto& [_,p]:s.players)if(p.alive)any=true;if(!any)s.over=true;if(!s.over&&s.phaseStatus=="boss"&&std::none_of(s.enemies.begin(),s.enemies.end(),[](const Enemy&e){return e.boss&&e.hp>0;})){s.enemies.clear();s.shots.clear();s.enemyShots.clear();s.gems.clear();s.hazards.clear();s.runes.clear();s.zones.clear();pushEvent(s,"bossDown");if(s.phase==5&&campaignOf(s).endless){s.phaseStatus="transition";s.transitionTime=4;}else if(s.phase==5){s.victory=true;s.over=true;s.phaseStatus="complete";for(auto& [_,p]:s.players)p.pendingPowers.clear();}else{s.phaseStatus="transition";s.transitionTime=4;}}}

std::uint32_t dailySeedFromKey(const std::string& key){std::uint32_t seed=2166136261u;for(unsigned char c:key)seed=(seed^c)*16777619u;return seed;}
DailyChallenge dailyChallenge(const std::string& key){DailyChallenge d;d.key=key;d.seed=dailySeedFromKey(key);SeededRandom r(d.seed);std::vector<std::string> pool={"swarm","frenzy","brittle","famine","tyrant","nobility"};while(d.curses.size()<2){std::size_t i=std::min(pool.size()-1,(std::size_t)(r.next()*pool.size()));d.curses.push_back(pool[i]);pool.erase(pool.begin()+i);}d.character=std::min(3,(int)(r.next()*4));return d;}

namespace {
std::string esc(const std::string& s){std::string o;for(char c:s){switch(c){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;case '\n':o+="\\n";break;case '\r':o+="\\r";break;case '\t':o+="\\t";break;default:o+=c;}}return o;}
template<class T>void comma(std::ostringstream& o,T& first){if(!first)o<<',';first=false;}
}
std::string stateToJson(const GameState& s,const std::string& viewerId){std::ostringstream o;o<<std::setprecision(8)<<'{';o<<"\"type\":\"state\",\"campaign\":\""<<esc(s.campaign)<<"\",\"time\":"<<s.time<<",\"over\":"<<(s.over?"true":"false")<<",\"victory\":"<<(s.victory?"true":"false")<<",\"phase\":"<<s.phase<<",\"phaseTime\":"<<s.phaseTime<<",\"phaseStatus\":\""<<esc(s.phaseStatus)<<"\",\"loop\":"<<s.loop<<",\"players\":{";bool f=true;for(const auto& [id,p]:s.players){comma(o,f);o<<'"'<<esc(id)<<"\":{\"id\":\""<<esc(id)<<"\",\"name\":\""<<esc(p.name)<<"\",\"color\":"<<p.color<<",\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"hp\":"<<p.hp<<",\"maxHp\":"<<p.maxHp<<",\"xp\":"<<p.xp<<",\"level\":"<<p.level<<",\"alive\":"<<(p.alive?"true":"false")<<",\"specialCharge\":"<<p.specialCharge<<",\"coins\":"<<p.coins<<",\"inputSeq\":"<<p.inputSeq<<",\"dashCooldown\":"<<p.dashCooldown<<",\"specialCooldown\":"<<p.specialCooldown<<",\"pendingPowers\":[";for(std::size_t i=0;i<p.pendingPowers.size();++i){if(i)o<<',';o<<'\"'<<esc(p.pendingPowers[i])<<'\"';}o<<"]}";}o<<"},\"enemies\":[";f=true;for(const auto&e:s.enemies){comma(o,f);o<<"{\"id\":"<<e.id<<",\"type\":\""<<esc(e.type)<<"\",\"x\":"<<e.x<<",\"y\":"<<e.y<<",\"hp\":"<<e.hp<<",\"maxHp\":"<<e.maxHp<<",\"boss\":"<<(e.boss?"true":"false")<<",\"elite\":"<<(e.elite?"true":"false")<<'}';}o<<"],\"shots\":[";f=true;for(const auto&q:s.shots){comma(o,f);o<<"{\"x\":"<<q.x<<",\"y\":"<<q.y<<",\"vx\":"<<q.vx<<",\"vy\":"<<q.vy<<",\"color\":"<<q.color<<'}';}o<<"],\"enemyShots\":[";f=true;for(const auto&q:s.enemyShots){comma(o,f);o<<"{\"x\":"<<q.x<<",\"y\":"<<q.y<<",\"vx\":"<<q.vx<<",\"vy\":"<<q.vy<<",\"radius\":"<<q.radius<<'}';}o<<"],\"gems\":[";f=true;for(const auto&g:s.gems){comma(o,f);o<<"{\"id\":"<<g.id<<",\"x\":"<<g.x<<",\"y\":"<<g.y<<",\"type\":\""<<esc(g.type)<<"\",\"value\":"<<g.value<<'}';}o<<"]";if(!viewerId.empty())o<<",\"viewer\":\""<<esc(viewerId)<<'"';o<<'}';return o.str();}

} // namespace arcana
