#include "network.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <utility>
namespace net=boost::asio; namespace beast=boost::beast; namespace ws=beast::websocket; using tcp=net::ip::tcp;
namespace arcana_client {
struct NetworkClient::Impl:std::enable_shared_from_this<Impl>{
  net::io_context io; tcp::resolver resolver{io}; ws::stream<beast::tcp_stream> socket{io}; beast::flat_buffer buffer; std::thread thread;
  std::deque<std::string> outgoing,incoming; mutable std::mutex inMx,errMx; bool writing{}; std::atomic_bool up{}; std::string host,path,err;
  void setError(std::string e){std::lock_guard l(errMx);err=std::move(e);up=false;}
  void start(std::string h,std::string port,std::string p){host=std::move(h);path=std::move(p);resolver.async_resolve(host,port,[this](beast::error_code ec,tcp::resolver::results_type res){if(ec)return setError(ec.message());beast::get_lowest_layer(socket).expires_after(std::chrono::seconds(10));beast::get_lowest_layer(socket).async_connect(res,[this](beast::error_code ec,const tcp::resolver::results_type::endpoint_type&){if(ec)return setError(ec.message());socket.set_option(ws::stream_base::timeout::suggested(beast::role_type::client));socket.async_handshake(host,path,[this](beast::error_code ec){if(ec)return setError(ec.message());up=true;read();});});});thread=std::thread([this]{io.run();});}
  void read(){socket.async_read(buffer,[this](beast::error_code ec,std::size_t){if(ec)return setError(ec.message());{std::lock_guard l(inMx);incoming.push_back(beast::buffers_to_string(buffer.data()));}buffer.consume(buffer.size());read();});}
  void send(std::string m){net::post(io,[this,m=std::move(m)]()mutable{outgoing.push_back(std::move(m));if(!writing)write();});}
  void write(){if(outgoing.empty()){writing=false;return;}writing=true;socket.text(true);socket.async_write(net::buffer(outgoing.front()),[this](beast::error_code ec,std::size_t){if(ec)return setError(ec.message());outgoing.pop_front();write();});}
  void stop(){if(io.stopped()){if(thread.joinable())thread.join();return;}net::post(io,[this]{beast::error_code ec;if(up)socket.close(ws::close_code::normal,ec);up=false;io.stop();});if(thread.joinable())thread.join();}
};
NetworkClient::NetworkClient():impl_(new Impl){} NetworkClient::~NetworkClient(){impl_->stop();}
void NetworkClient::connect(const std::string&h,const std::string&p,const std::string&path){impl_->start(h,p,path);}void NetworkClient::send(std::string t){impl_->send(std::move(t));}
std::vector<std::string> NetworkClient::poll(){std::lock_guard l(impl_->inMx);std::vector<std::string>v(impl_->incoming.begin(),impl_->incoming.end());impl_->incoming.clear();return v;}
bool NetworkClient::connected()const{return impl_->up;}std::string NetworkClient::error()const{std::lock_guard l(impl_->errMx);return impl_->err;}
}
