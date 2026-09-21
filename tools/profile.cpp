#include "arcana/profile.hpp"
#include <iostream>
using namespace arcana;
int main(int argc,char**argv){std::string path=argc>1?argv[1]:"arcana_profile.ini";auto p=loadProfile(path);if(argc>=4&&std::string(argv[2])=="buy"){bool ok=buyUpgrade(p,argv[3]);saveProfile(p,path);std::cout<<(ok?"comprado":"nao foi possivel comprar")<<"\n";}else if(argc>=3&&std::string(argv[2])=="respec"){std::cout<<"reembolso="<<respec(p)<<"\n";saveProfile(p,path);}else{std::cout<<"coins="<<p.coins<<" invested="<<p.invested<<"\n";for(auto&[id,r]:p.upgrades.rank)if(r)std::cout<<id<<"="<<r<<"\n";}}
