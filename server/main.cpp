#include "arcana/game.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace net=boost::asio; namespace beast=boost::beast; namespace ws=beast::websocket; using tcp=net::ip::tcp; using boost::property_tree::ptree;
using namespace arcana;
class Server; class Session;
static std::string quote(const std::string&s){std::string o="\"";for(char c:s){if(c=='"'||c=='\\')o+='\\';o+=c;}return o+'"';}
static std::string uuid(){static std::atomic<unsigned long long> n{1};std::ostringstream o;o<<std::hex<<std::chrono::steady_clock::now().time_since_epoch().count()<<n++;return o.str();}

struct Room:std::enable_shared_from_this<Room>{
  Server& server; std::string code,visibility{"closed"}; GameState state; bool running{}; std::string hostId; std::unordered_map<std::string,std::weak_ptr<Session>> clients; net::steady_timer timer; SeededRandom rng; int frames{};
  Room(Server&s,std::string c,std::string campaign,std::vector<std::string> curses):server(s),code(std::move(c)),state(createGameState(std::move(campaign),std::move(curses))),timer(sIo()),rng((std::uint32_t)std::chrono::steady_clock::now().time_since_epoch().count()){}
  net::io_context& sIo(); void broadcast(std::string m); void sendLobby(); void start(); void tick(); void remove(const std::string&id);
};

class Session:public std::enable_shared_from_this<Session>{
  ws::stream<beast::tcp_stream> socket_; beast::flat_buffer buffer_; Server& server_; std::deque<std::string> out_; bool writing_{};
public:
  std::string id,token; std::weak_ptr<Room> room;
  Session(tcp::socket&&s,Server&srv):socket_(std::move(s)),server_(srv){}
  void run(){socket_.set_option(ws::stream_base::timeout::suggested(beast::role_type::server));socket_.async_accept(beast::bind_front_handler(&Session::onAccept,shared_from_this()));}
  void send(std::string m){net::post(socket_.get_executor(),[self=shared_from_this(),m=std::move(m)]()mutable{self->out_.push_back(std::move(m));if(!self->writing_)self->doWrite();});}
  void close(){beast::error_code ec;socket_.close(ws::close_code::normal,ec);}
private:
  void onAccept(beast::error_code ec){if(ec)return;doRead();}
  void doRead(){socket_.async_read(buffer_,beast::bind_front_handler(&Session::onRead,shared_from_this()));}
  void onRead(beast::error_code ec,std::size_t){if(ec){onClose();return;}std::string msg=beast::buffers_to_string(buffer_.data());buffer_.consume(buffer_.size());handle(msg);doRead();}
  void doWrite(){if(out_.empty()){writing_=false;return;}writing_=true;socket_.text(true);socket_.async_write(net::buffer(out_.front()),beast::bind_front_handler(&Session::onWrite,shared_from_this()));}
  void onWrite(beast::error_code ec,std::size_t){if(ec){onClose();return;}out_.pop_front();doWrite();}
  void onClose(); void handle(const std::string&msg);
};

class Server{
public:
  net::io_context io; tcp::acceptor acceptor; std::unordered_map<std::string,std::shared_ptr<Room>> rooms; int maxRooms{3};
  explicit Server(unsigned short port):acceptor(io,{tcp::v4(),port}){accept();}
  void accept(){acceptor.async_accept([this](beast::error_code ec,tcp::socket s){if(!ec)std::make_shared<Session>(std::move(s),*this)->run();accept();});}
  std::string code(){static const char a[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";static std::mt19937 gen(std::random_device{}());std::uniform_int_distribution<int>d(0,(int)sizeof(a)-2);std::string c;do{c.clear();for(int i=0;i<5;i++)c+=a[d(gen)];}while(rooms.contains(c));return c;}
  void eraseIfEmpty(const std::shared_ptr<Room>&r){if(r->clients.empty())rooms.erase(r->code);}
};
net::io_context& Room::sIo(){return server.io;}
void Room::broadcast(std::string m){for(auto it=clients.begin();it!=clients.end();){if(auto s=it->second.lock()){s->send(m);++it;}else it=clients.erase(it);}}
void Room::sendLobby(){std::ostringstream o;o<<"{\"type\":\"lobby\",\"count\":"<<clients.size()<<",\"visibility\":"<<quote(visibility)<<",\"hostId\":"<<quote(hostId)<<",\"running\":"<<(running?"true":"false")<<",\"campaign\":"<<quote(state.campaign)<<",\"players\":[";bool first=true;for(auto&[id,p]:state.players){if(!first)o<<',';first=false;o<<"{\"id\":"<<quote(id)<<",\"name\":"<<quote(p.name)<<",\"color\":"<<p.color<<"}";}o<<"]}";broadcast(o.str());}
void Room::start(){if(running)return;running=true;broadcast("{\"type\":\"start\",\"state\":"+stateToJson(state)+"}");timer.expires_after(std::chrono::milliseconds(33));timer.async_wait([self=shared_from_this()](beast::error_code ec){if(!ec)self->tick();});}
void Room::tick(){if(!running)return;if(!clients.empty())updateGame(state,1.0/30.0,rng);if(++frames%2==0||state.over)broadcast(stateToJson(state));if(state.over){running=false;return;}timer.expires_after(std::chrono::milliseconds(33));timer.async_wait([self=shared_from_this()](beast::error_code ec){if(!ec)self->tick();});}
void Room::remove(const std::string&id){clients.erase(id);auto it=state.players.find(id);if(it!=state.players.end())state.players.erase(it);if(hostId==id)hostId=clients.empty()?"":clients.begin()->first;sendLobby();server.eraseIfEmpty(shared_from_this());}

static std::optional<ptree> parse(const std::string&s){try{std::stringstream ss(s);ptree p;boost::property_tree::read_json(ss,p);return p;}catch(...){return std::nullopt;}}
static std::vector<std::string> stringArray(const ptree&p,const std::string&key){std::vector<std::string>v;auto c=p.get_child_optional(key);if(c)for(auto&x:*c)v.push_back(x.second.get_value<std::string>());return v;}
void Session::onClose(){if(auto r=room.lock())r->remove(id);room.reset();}
void Session::handle(const std::string&raw){auto pp=parse(raw);if(!pp)return;auto&p=*pp;auto type=p.get<std::string>("type","");if(type=="ping"){send("{\"type\":\"pong\",\"t\":"+std::to_string(p.get<double>("t",0))+"}");return;}if(type=="listRooms"){std::ostringstream o;o<<"{\"type\":\"rooms\",\"rooms\":[";bool f=true;for(auto&[c,r]:server_.rooms)if(r->visibility=="open"){if(!f)o<<',';f=false;o<<"{\"code\":"<<quote(c)<<",\"count\":"<<r->clients.size()<<",\"running\":"<<(r->running?"true":"false")<<"}";}o<<"],\"capacity\":{\"used\":"<<server_.rooms.size()<<",\"max\":"<<server_.maxRooms<<"}}";send(o.str());return;}
 // server/server.js: the Aurora Guardian needs the client to report its Classic victory.
 if((type=="create"||type=="join")&&room.expired()&&std::clamp(p.get<int>("color",0),0,CHARACTER_COUNT-1)==AURORA&&!p.get<bool>("unlocks.aurora",false)){send("{\"type\":\"error\",\"code\":\"CHARACTER_LOCKED\",\"message\":\"Venca o modo Classico para desbloquear o Guardiao da Aurora.\"}");return;}
 if(type=="create"){if(!room.expired())return;if((int)server_.rooms.size()>=server_.maxRooms){send("{\"type\":\"error\",\"message\":\"Servidor sem vagas para novas salas.\"}");return;}auto campaign=p.get<std::string>("campaign","classic");if(campaign!="quick"&&campaign!="classic"&&campaign!="endless")campaign="quick";auto r=std::make_shared<Room>(server_,server_.code(),campaign,stringArray(p,"curses"));r->visibility=p.get<std::string>("visibility","closed")=="open"?"open":"closed";server_.rooms[r->code]=r;id=uuid();token=uuid();int color=std::clamp(p.get<int>("color",0),0,CHARACTER_COUNT-1);std::string name=p.get<std::string>("name","Arcanista");if(name.size()>16)name.resize(16);r->state.players[id]=createPlayer(id,name,color);r->clients[id]=shared_from_this();r->hostId=id;room=r;send("{\"type\":\"joined\",\"room\":"+quote(r->code)+",\"playerId\":"+quote(id)+",\"token\":"+quote(token)+",\"color\":"+std::to_string(color)+"}");r->sendLobby();return;}
 if(type=="join"){if(!room.expired())return;std::string c=p.get<std::string>("room","");for(char&x:c)x=(char)std::toupper((unsigned char)x);auto ri=server_.rooms.find(c);if(ri==server_.rooms.end()){send("{\"type\":\"error\",\"message\":\"Sala nao encontrada.\"}");return;}auto r=ri->second;if(r->clients.size()>=4){send("{\"type\":\"error\",\"message\":\"Sala cheia.\"}");return;}int color=std::clamp(p.get<int>("color",0),0,CHARACTER_COUNT-1);for(auto&[_,pl]:r->state.players)if(pl.color==color){send("{\"type\":\"error\",\"code\":\"CHARACTER_TAKEN\"}");return;}id=uuid();token=uuid();std::string name=p.get<std::string>("name","Arcanista");if(name.size()>16)name.resize(16);Player pl=createPlayer(id,name,color);if(r->running)addLatePlayer(r->state,std::move(pl));else r->state.players[id]=std::move(pl);r->clients[id]=shared_from_this();room=r;send("{\"type\":\"joined\",\"room\":"+quote(r->code)+",\"playerId\":"+quote(id)+",\"token\":"+quote(token)+",\"color\":"+std::to_string(color)+"}");r->sendLobby();return;}
 auto r=room.lock();if(!r)return;auto pi=r->state.players.find(id);if(pi==r->state.players.end())return;auto&pl=pi->second;
 if(type=="start"&&r->hostId==id)r->start();else if(type=="ready"&&r->running)send("{\"type\":\"start\",\"state\":"+stateToJson(r->state,id)+"}");else if(type=="input"){double x=std::clamp(p.get<double>("x",0.0),-1.0,1.0),y=std::clamp(p.get<double>("y",0.0),-1.0,1.0),len=std::hypot(x,y);if(len>1){x/=len;y/=len;}pl.input.x=x;pl.input.y=y;pl.inputSeq=std::max<std::uint64_t>(pl.inputSeq,p.get<std::uint64_t>("seq",0));}else if(type=="choosePower")applyPower(pl,p.get<std::string>("power",""));else if(type=="reroll"){DefaultRandom rr;rerollPowers(pl,rr,r->state.players.size()>1);}else if(type=="special")activateSpecial(r->state,id,r->rng);else if(type=="dash")activateDash(r->state,id,{p.get<double>("x",0),p.get<double>("y",0)});else if(type=="signal")sendSignal(r->state,id,p.get<std::string>("signal",""));else if(type=="leave"){r->remove(id);room.reset();}
}
int main(int argc,char**argv){unsigned short port=8081;if(const char*e=std::getenv("PORT"))port=(unsigned short)std::stoi(e);if(argc>1)port=(unsigned short)std::stoi(argv[1]);try{Server server(port);std::cout<<"Arcana Survivors C++ server listening on :"<<port<<"\n";server.io.run();}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
