#include "arcana/data.hpp"
namespace arcana {
const std::unordered_map<std::string,EnemyDef>& enemyDefs(){
  static const std::unordered_map<std::string,EnemyDef> v={
    {"slime",{"Lodo","splitter","slime","",34,58,9,2}}, {"slimelet",{"Lodinho","","slime","",10,84,5,1,40}},
    {"bat",{"Morcego de brasa","bomber","","",18,128,6,2}}, {"eye",{"Olho gelido","shooter","","thorn",30,70,12,3}},
    {"brute",{"Golem de magma","","","",140,47,22,8,92}}, {"mushroom",{"Cogumelo","","","",22,62,9}},
    {"beetle",{"Besouro de espinhos","charger","","",18,94,7}}, {"skeleton",{"Esqueleto","","","",30,70,11}},
    {"wraith",{"Espectro","flier","","",20,104,9}}, {"imp",{"Diabrete","","","",26,108,10}},
    {"scorpion",{"Escorpiao","charger","","",55,63,16}}, {"spore",{"Esporo espectral","shooter","","thorn",35,66,12,3}},
    {"revenant",{"Alma do brejo","flier","","",28,108,11,2}}, {"sentinel",{"Sentinela solar","charger","","",48,84,14,3}},
    {"seer",{"Oraculo astral","shooter","","bolt",40,72,14,3}}, {"voidling",{"Asa do vazio","bomber","","",28,124,10,2}},
    {"voidscarab",{"Escaravelho do eclipse","charger","","",48,96,14,3}},
    {"treant",{"","","","",1200,52,22,0,170,58}}, {"lich",{"","","","",2200,64,24,0,160,52}},
    {"demon",{"","","","",3400,76,28,0,185,64}}, {"bogwarden",{"","","","",4600,58,30,0,180,60}},
    {"archon",{"","","","",6000,66,32,0,175,56}}, {"umbra",{"","","","",7800,72,34,0,195,66}}
  }; return v;
}
const std::array<PhaseDef,6>& phases(){
  static const std::array<PhaseDef,6> v={{
    {"Bosque Desperto","forest","#83dfaa","treant","Raiz Ancestral",{"mushroom","beetle"}},
    {"Cripta Glacial","ice","#8cdfff","lich","Rei do Inverno",{"skeleton","wraith"}},
    {"Abismo de Brasas","lava","#ffac70","demon","Coracao da Caldeira",{"imp","scorpion"}},
    {"Pantano Espectral","swamp","#b8df7c","bogwarden","Matriarca do Brejo",{"spore","revenant"}},
    {"Cidadela Astral","astral","#ffe09b","archon","Arconte Solar",{"sentinel","seer"}},
    {"Eclipse do Vazio","void","#d3a4ff","umbra","Soberano do Eclipse",{"voidling","voidscarab"}}
  }}; return v;
}
const std::unordered_map<std::string,CampaignDef>& campaigns(){
 static const std::unordered_map<std::string,CampaignDef> v={
  {"quick",{"Ritual rapido",120,1.25,1,.8,false}}, {"classic",{"Ritual classico",300,.5,.4,1,false}}, {"endless",{"Ritual infinito",120,1.25,1,.8,true}}
 }; return v;
}
const std::unordered_map<std::string,PowerDef>& powerDefs(){
 static const std::unordered_map<std::string,PowerDef> v={
  {"arcane",{"Poder arcano","passive",5}}, {"haste",{"Cadencia","passive",5}}, {"vitality",{"Vitalidade","passive",5}},
  {"swiftness",{"Passos do vento","passive",5}}, {"multishot",{"Disparo multiplo","passive",3}}, {"magnet",{"Magnetismo","passive",4}}, {"armor",{"Armadura runica","passive",5}},
  {"orbit",{"Orbes arcanos","weapon",5}}, {"aura",{"Aura sagrada","weapon",5}}, {"chain",{"Corrente de raios","weapon",5}}, {"runes",{"Runas explosivas","weapon",5}}, {"familiar",{"Familiar arcano","weapon",5}},
  {"shatter",{"Estilhaco glacial","signature",1,0,4}}, {"burn",{"Chao em chamas","signature",1,1,4}}, {"ricochet",{"Ricochete","signature",1,2,4}}, {"boomerang",{"Lua crescente","signature",1,3,4}},
  {"bond",{"Elo arcano","coop",3}}, {"lifelink",{"Vinculo vital","coop",3}}, {"guardian",{"Guardiao","coop",2}},
  {"constellation",{"Constelacao","evolution",1,-1,0,{{"orbit",5},{"arcane",3}}}},
  {"sanctuary",{"Santuario","evolution",1,-1,0,{{"aura",5},{"vitality",3}}}},
  {"tempest",{"Tempestade","evolution",1,-1,0,{{"chain",5},{"haste",3}}}},
  {"minefield",{"Campo minado","evolution",1,-1,0,{{"runes",5},{"magnet",2}}}},
  {"covenant",{"Pacto ancestral","evolution",1,-1,0,{{"familiar",5},{"swiftness",2}}}},
  {"avalanche",{"Avalanche","evolution",1,-1,0,{{"shatter",1},{"multishot",3}}}},
  {"hellfire",{"Inferno","evolution",1,-1,0,{{"burn",1},{"aura",3}}}},
  {"bramble",{"Espinheiro","evolution",1,-1,0,{{"ricochet",1},{"chain",3}}}},
  {"fullmoon",{"Lua cheia","evolution",1,-1,0,{{"boomerang",1},{"orbit",3}}}},
  {"stormrunes",{"Runas de tempestade","evolution",1,-1,0,{{"runes",3},{"chain",3}}}},
  {"solarcrown",{"Coroa solar","evolution",1,-1,0,{{"orbit",3},{"aura",3}}}}
 }; return v;
}
}
