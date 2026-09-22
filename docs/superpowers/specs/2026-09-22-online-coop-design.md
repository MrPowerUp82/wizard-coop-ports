# Co-op online no PC, compatível com o servidor do meu-game

Data: 2026-09-22 · Status: aprovado no brainstorming, aguardando revisão do spec

## Objetivo

Levar o co-op online da versão web (`meu-game`) para o port C++ no PC (Windows e Linux), visando o
lançamento na Steam. O cliente C++ conversa com o **mesmo servidor Node autoritativo**
(`meu-game/server/server.js`) e o **mesmo protocolo** (`meu-game/server/protocol.js`), então
jogadores do PC e da web jogam na mesma sala (cross-play).

O cliente C++ **não simula** a partida online: decodifica snapshots, desenha e prevê só o movimento
do jogador local. O que precisa bater entre as duas bases é o contrato de dados (ids, tabelas de
índice, ordem dos campos, fórmula de movimento), não a simulação inteira.

## Escopo

Dentro:
- Plataformas: Windows e Linux (`arcana_desktop`). Switch, Vita e PSP ficam só com co-op local.
- Paridade de recursos com a web: lista de salas abertas, criar sala aberta/fechada, entrar por
  código, lobby com troca de personagem, ritual e maldições escolhidos na criação, host inicia,
  entrar com partida em andamento, reconexão automática, sinais para aliados.
- Uma mudança pequena no `meu-game`: versão de protocolo e script de fixtures.

Fora (próximas etapas):
- Steamworks (convites, nome da Steam, conquistas, teclado da Steam, relay P2P).
- Online nos consoles.
- Capacidade do servidor (hoje `MAX_ROOMS=3` na VPS).
- O servidor C++ (`server/main.cpp`) e o cliente antigo (`client/`) **continuam no repositório sem
  mudanças**; não fazem parte deste trabalho.

## Arquitetura

O `arcana_core` só ganha acréscimos: `Event::player/name` e `playerMovement()`. A rede fica em módulos sem SDL, em um alvo CMake novo (`arcana_online`),
separado do `arcana_net` antigo (Beast).

```
platforms/net/
  ws_transport.{hpp,cpp}   IXWebSocket + mbedTLS numa thread própria; filas de texto entrada/saída
                           com mutex; connect/close/estado/erro. Fica atrás de uma interface
                           (Transport) para permitir outro transporte no futuro (Steam).
  protocol.{hpp,cpp}       porta 1:1 do protocol.js: decodeSnapshot(json) -> GameState; tabelas
                           ENEMY_TYPES, DROP_TYPES, SPRITES, STATUSES, ENCOUNTER_KINDS e flags na
                           mesma ordem do JS; builders das mensagens do cliente.
  session.{hpp,cpp}        porta do net.js: estado da sala/lobby, buffer de 30 snapshots,
                           interpolação, predição/reconciliação, ping/RTT, reconexão por token.
  room_list.{hpp,cpp}      conexão curta para `listRooms`.
platforms/online/
  online_menu.{hpp,cpp}    Tela Online, criação de sala, Lobby, entrada de texto/código. Fora de
                           platforms/sdl/ porque o Makefile do Switch compila platforms/sdl/*.cpp
                           por wildcard, e o Switch não tem online.
platforms/sdl/
  frontend.{hpp,cpp}       modo online: em vez de updateGame, grava a view da Session em state_.
```

Compilação condicional: `ARCANA_HAS_ONLINE` só é definido quando `arcana_online` está linkado; sem
ele o item "Online" não aparece no título.

### Integração com o Frontend

- Em modo online, `updatePlaying` não chama `updateGame`: chama `session.frame(now, dt, input)` e
  copia a view em `state_`. Renderer, HUD, câmera, `observeEvents` (sons, anúncios, flashes) e a
  tela de escolha de poder continuam iguais.
- Ações do jogador viram mensagens em vez de chamadas ao core:
  `choosePower`, `reroll`, `special`, `dash {x,y}`, `signal {signal, x?, y?}`, `input {x,y,seq}`, `leave`.
- Um jogador local por cliente no online (como a web).
- Meta-progressão: `create`/`join` enviam `meta` (ranks do Grimório) e `loadout`, como a web; o
  servidor valida. No fim da partida, as moedas do jogador local são depositadas no save local pelo
  mesmo caminho do offline (`depositRun`).
- Nome do jogador salvo no `profile.ini` (`pref.name=`), até 16 caracteres.
- Servidor: padrão `wss://vps65228.publiccloud.com.br/ws` (igual a `DEFAULT_SERVER` do
  `meu-game/src/menu.js`); sobrescrito por `--server <url>`, ou servidor alternativo em
  `pref.server=` no `profile.ini`.

## Telas e fluxo

```
Título -> Online (só PC)
  primeira vez: pede o nome
  Tela Online: lista de salas abertas (atualiza a cada 5 s; código, host, n/4, ritual, "em jogo"),
               Criar sala, Entrar com código, Nome; rodapé com capacidade do servidor e ping.
    Criar sala: aberta/fechada, ritual (rápido/clássico/infinito*), maldições.
    Entrar com código: 6 caracteres.
  Lobby: código, visibilidade, ritual, maldições; jogadores com personagem, host, desconectado;
         esquerda/direita troca personagem (`selectCharacter`); Iniciar (só host); Sair.
  Partida online -> Fim de jogo -> Tela Online
```
\* Infinito só aparece se desbloqueado no Grimório (o servidor também confere).

- Entrada de texto: teclado via `SDL_TEXTINPUT` + Ctrl+V; controle via teclado em grade desenhado
  pelo jogo (código: só `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`; nome: letras e números).
- Sem pausa online: Esc/Start abre "Continuar / Sair da sala" por cima; a partida segue.
- Sinais: teclado Q (venham), E (ajuda), X (cuidado), C (olhem ali, na direção em que o jogador
  está virado). Controle: segurar Alt + direção (cima venham, esquerda ajuda, direita cuidado, baixo
  olhem ali). Os valores de `signal` são os mesmos que a web envia.
- Reconexão: faixa "Reconectando…" durante as tentativas; ao desistir, mostra o erro e volta à Tela
  Online.
- Erros do servidor (`type:error`) viram toast. `CHARACTER_TAKEN`: escolhe um personagem livre com
  base em `players` e repete o pedido, como a web.

## Rede e sincronização

Por frame (60 Hz no cliente):
1. Thread de rede: recebe texto e enfileira; o parse do JSON e o `decodeSnapshot` rodam no
   `Session::update()` da thread principal (≈0,3 ms por snapshot no PC).
2. Thread principal: drena a fila; envia input; interpola em `renderT = (now - clockOffset) - 0.1 s`;
   prevê o jogador local; escreve a view em `state_`.

Algoritmos portados do `net.js`, com as mesmas constantes (duas diferenças propositais, comentadas
no código: o input tem um piso de 50 ms entre envios, para caber no limite do servidor, e a
extrapolação de tiros para no máximo 0,5 s à frente, para uma reconexão longa não arrastar o mundo):
- `clockOffset = min(sample, clockOffset + 4 ms)`, com `sample = chegada - state.time`.
- RTT: ping a cada 2 s, média `rtt*0.7 + amostra*0.3`.
- Interpolação linear de jogadores, inimigos, gemas e familiar por id; `orbitAngle` por ângulo;
  tiros avançam por `vx/vy`; `hazard.warning` desconta o tempo à frente.
- Predição: `movementDelta` (a mesma fórmula de `src/game.cpp` e `movement.js`, exposta no header)
  aplicada ao input local; histórico de 1,5 s; reconciliação comparando com a posição prevista há
  cerca de um RTT, fator 0,35; erro > 220 unidades corrige de vez; mudança de `motionId` (esquiva)
  volta para a posição do servidor.
- O renderer só usa campos presentes no snapshot (conferido), então nada é preservado entre
  snapshots.

Envio de input: quantizado em passos de 1/32, no máximo 20 envios/s quando muda, mais keep-alive a
cada 100 ms. Assim o cliente fica bem abaixo do limite de 50 mensagens/s do servidor, que descarta o
excesso.

Reconexão: em queda durante a partida (código < 4000, partida não terminada, com token), tenta
`resume` com atrasos 400, 800, 1500, 2500, 4000, 6000, 8000, 8000 ms. Códigos ≥ 4000, `leave` e fim
de jogo não reconectam. O token fica só na memória.

## Contrato de compatibilidade com o meu-game

1. **Versão do protocolo (mudança no meu-game).** `export const PROTOCOL_VERSION = 1` em
   `server/protocol.js`. Os clientes enviam `v` em `create`, `join` e `resume`; o servidor responde
   `{type:'error', code:'PROTOCOL_MISMATCH', message:'Atualize o jogo para jogar online.'}` se `v`
   for diferente. `listRooms` também devolve `v`, e o cliente C++ esconde salas de outra versão.
   O cliente web usa a constante, então nada muda para ele. A versão sobe sempre que a forma do
   snapshot ou das mensagens mudar.
2. **Fixtures geradas pelo JS.** `meu-game/scripts/export-protocol-fixtures.mjs` roda partidas com
   bots (1 e 4 jogadores, fases e chefes variados, fim de jogo) e grava em
   `wizard_coop_cpp/tests/fixtures/protocol/`: pares `encodeState` (entrada) / `decodeState` (saída
   esperada), as tabelas de índice e a `PROTOCOL_VERSION`.
3. **Teste C++ `online_protocol`.** Decodifica cada fixture e compara campo a campo com o esperado
   (números com tolerância de arredondamento); confere as tabelas e a versão.
4. **Teste de ponta a ponta.** `arcana_desktop --online-bot --server ws://… [--create|--join CODE]`
   conecta e joga com o bot existente por N segundos, headless. O alvo Docker `online-e2e` sobe o
   `server.js` de `../meu-game` num container Node e roda 2 bots C++ na mesma sala; passa se os dois
   recebem snapshots com 2 jogadores e a partida avança sem erro.

## Erros e robustez

- Conexão: timeout de 10 s; mensagens distintas para falha de rede ("Não foi possível alcançar o
  servidor") e de TLS ("Certificado do servidor inválido").
- Snapshot malformado ou fora de limite: descartado e contado (overlay F3). Listas acima das
  capacidades fixas (`cfg::MAX_ENEMIES` etc.) são truncadas; índices de tabela fora do intervalo
  ignoram a entidade. Nenhum caminho de decodificação pode derrubar o jogo.
- `bufferedAmount`: o servidor pula clientes lentos; o cliente tolera buracos entre snapshots.

## Segurança

- TLS com verificação de cadeia e de hostname usando `assets/cacert.pem` (bundle da Mozilla).
- `ws://` só para `localhost`/`127.0.0.1` ou com `--insecure-ws`.
- Nome de jogador vindo do servidor: cortado em 16 caracteres, caracteres fora da fonte viram `?`.

## Build e release

- Opção `ARCANA_BUILD_ONLINE` (padrão ON quando `ARCANA_BUILD_DESKTOP` está ligado); ligada nos
  alvos Docker `host` e `windows`; desligada em Switch, Vita e PSP.
- `FetchContent` com URL + SHA256 fixos: IXWebSocket, mbedTLS, zlib, nlohmann/json. Tudo estático:
  o `.exe` do Windows continua só com as DLLs do SDL.
- Release: `assets/cacert.pem` e `THIRD_PARTY_NOTICES.txt` (BSD-3, Apache-2.0, zlib, MIT) no zip
  do Windows e no tar.gz do Linux.
- Novo alvo Docker `online-e2e` (precisa de `../meu-game`), fora do `build-all` padrão.
- README: seção "Online" (como jogar, servidor, flags) e tabela de testes atualizada.

## Testes

| Teste | O que garante |
|---|---|
| `online_protocol` | decodificação igual ao `decodeState` do JS, tabelas e versão |
| `online_session` | interpolação, relógio, reconciliação, limite de envio de input, política de reconexão (com transporte falso) |
| `online_text` | teclado em grade e validação de código/nome |
| `frontend` (existente) | continua passando; modo offline intocado |
| `native_hotpath` (existente) | continua passando (o modo online não entra nesse teste) |
| `online-e2e` (Docker) | cross-play real contra o `server.js` do meu-game |
