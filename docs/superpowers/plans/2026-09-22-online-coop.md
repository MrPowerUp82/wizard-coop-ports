# Co-op online no PC (cross-play com o meu-game) — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** o `arcana_desktop` (Windows/Linux) joga online no mesmo servidor Node autoritativo do `meu-game`, com paridade de recursos com a web (lista de salas, criar/entrar, lobby, reconexão, sinais).

**Architecture:** o cliente C++ não simula a partida online: decodifica os snapshots compactos de `meu-game/server/protocol.js` para `arcana::GameState`, interpola, prevê só o jogador local e entrega o resultado ao renderer/HUD existentes. A rede fica em `platforms/net` (sem SDL, testável com transporte falso), a UI online em `platforms/online`, e o `Frontend` só conhece uma interface abstrata `OnlinePort` — os builds de console não compilam nada disso.

**Tech Stack:** C++20, CMake `FetchContent` (IXWebSocket 11.4.6, Mbed TLS 3.6.4, zlib 1.3.1, nlohmann/json 3.12.0), SDL2 (frontend existente), Node 22 + `node:test` (meu-game), Docker (builds e teste ponta a ponta).

Spec: [docs/superpowers/specs/2026-09-22-online-coop-design.md](../specs/2026-09-22-online-coop-design.md)

## Global Constraints

- Repositórios: `wizard_coop_cpp` (este) e `../meu-game`. Trabalhe no branch `feat/online-coop` nos **dois** (crie a partir de `main` antes da Task 1).
- `arcana_core`, Switch, Vita e PSP continuam compilando sem nenhuma dependência nova. O Makefile do Switch compila `platforms/sdl/*.cpp` por wildcard: **nenhum código de rede ou de UI online entra em `platforms/sdl/`** (só a interface abstrata `OnlinePort` em `frontend.hpp`).
- Servidor padrão: `wss://vps65228.publiccloud.com.br/ws`. `ws://` só para `localhost`/`127.0.0.1` ou com `--insecure-ws`.
- Protocolo: `PROTOCOL_VERSION = 1`. Cliente sem `v` é tratado como versão 1 (web antiga continua funcionando).
- Constantes do `net.js` portadas sem mudança: atraso de interpolação 100 ms, 30 snapshots, ping a cada 2 s, RTT `rtt*0.7 + amostra*0.3`, reconciliação 0,35, corte 220 unidades, histórico 1,5 s, reconexão 400/800/1500/2500/4000/6000/8000/8000 ms.
- Input: quantizado em 1/32, no máximo um envio a cada 50 ms quando muda, keep-alive a cada 100 ms (≤ 20 msg/s).
- Nomes de jogador: até 16 codepoints, sem caracteres de controle.
- Textos de interface em português; comentários de código em inglês, no estilo do arquivo vizinho.
- Testes C++ usam `assert()` com `#undef NDEBUG` no topo (padrão de `tests/*.cpp`).
- Commits terminam com `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- Depois de mexer em código do wizard_coop_cpp, rode `graphify update .` (regra do CLAUDE.md do usuário).

## Mudanças em relação ao spec (decididas ao planejar)

1. **Decodificação na thread principal.** O `WsTransport` entrega texto; a `Session` faz o parse no `update()`. No PC isso custa ~0,3 ms por snapshot (15/s); o overlay F3 continua mostrando o tempo de frame. Mais simples de testar e sem fila de objetos entre threads.
2. **Sem "campos preservados por id".** Conferido: o renderer só usa campos que o snapshot traz (`gem.ttl` e `zone.ttl` já vêm fixos do `decodeState`), então não é preciso guardar nada entre snapshots.
3. **`Event` ganha `player` e `name`** (aditivo em `arcana_core`), para o toast "Ana: preciso de ajuda!" e para não avisar o próprio sinal. `game.hpp` também expõe `playerMovement()` para a predição.
4. **Bug corrigido de passagem:** `Frontend::startRun()` chamava `depositRun()` depois de destruir `state_`; a ordem é invertida na Task 13.

A Task 16 atualiza o spec com esses quatro pontos.

## Mapa de arquivos

meu-game:
- Modificar `server/protocol.js` — exporta `PROTOCOL_VERSION` e as tabelas de índice.
- Modificar `server/server.js` — recusa versão diferente; `listRooms` devolve `v`.
- Modificar `src/net.js`, `src/main.js` — enviam `v`.
- Criar `scripts/export-protocol-fixtures.mjs` — gera as fixtures do C++.
- Criar `server/fixtures.test.js`; modificar `server/server.test.js`, `package.json`.

wizard_coop_cpp:
- `CMakeLists.txt` — opção `ARCANA_BUILD_ONLINE`, dependências, alvos `arcana_online`, `arcana_online_menu`, testes.
- `include/arcana/game.hpp`, `src/game.cpp` — `Event::player/name`, `playerMovement()`.
- `include/arcana/profile.hpp`, `src/profile.cpp` — `Preferences::name/server`.
- `platforms/net/transport.{hpp,cpp}` — interface `Transport` + política de URL.
- `platforms/net/protocol.{hpp,cpp}` — tabelas, `decodeSnapshot`, mensagens.
- `platforms/net/interpolation.{hpp,cpp}` — `Interpolator`.
- `platforms/net/prediction.{hpp,cpp}` — `quantizeInput`, `InputSender`, `Predictor`.
- `platforms/net/ws_transport.{hpp,cpp}` — `WsTransport` (IXWebSocket).
- `platforms/net/session.{hpp,cpp}` — `Session`.
- `platforms/net/room_list.{hpp,cpp}` — `RoomList`.
- `platforms/net/text_entry.{hpp,cpp}` — `TextEntry` (teclado em grade).
- `platforms/online/online_menu.{hpp,cpp}` — `OnlineMenu : sdl::OnlinePort`.
- `platforms/sdl/frontend.{hpp,cpp}` — `OnlinePort`, modo online, sinais, HUD.
- `platforms/sdl/animator.{hpp,cpp}`, `platforms/sdl/world_renderer.cpp` — efeito e seta de sinal.
- `platforms/desktop/main.cpp` — flags, texto, sinais no teclado, criação do `OnlineMenu`.
- `tests/fake_transport.hpp`, `tests/online_*_tests.cpp`, `tests/fixtures/protocol/snapshots.json`.
- `assets/cacert.pem`, `THIRD_PARTY_NOTICES.txt`, `tools/release/third-party-notices.sh`, `tools/docker/online-e2e.sh`, scripts de build/release, `README.md`.

---

### Task 1: Versão do protocolo no meu-game

**Files:**
- Modify: `../meu-game/server/protocol.js:3-24`
- Modify: `../meu-game/server/server.js` (import no topo, handler de mensagens, `listRooms`)
- Modify: `../meu-game/src/net.js:61-62`
- Modify: `../meu-game/src/main.js:368-369`
- Test: `../meu-game/server/server.test.js`

**Interfaces:**
- Produces: `export const PROTOCOL_VERSION = 1` e `export`s de `ENEMY_TYPES`, `DROP_TYPES`, `SPRITES`, `STATUSES`, `ENCOUNTER_KINDS`, `PLAYER_KEYS` em `server/protocol.js`. Erro do servidor `{type:'error', code:'PROTOCOL_MISMATCH', message:'Atualize o jogo para jogar online.'}`. Resposta `rooms` ganha `v`.

- [ ] **Step 1: Escrever o teste que falha** — acrescente ao fim de `server/server.test.js`:

```js
test('recusa cliente de outra versão do protocolo e informa a versão na lista de salas', { timeout: 10000 }, async t => {
  const child = spawn(process.execPath, ['server/server.js'], {
    env: { ...process.env, PORT: '0' }, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true
  });
  t.after(() => child.kill());
  const [output] = await once(child.stdout, 'data');
  const port = String(output).match(/:(\d+)/)?.[1];
  const ws = new WebSocket(`ws://127.0.0.1:${port}`);
  t.after(() => ws.terminate());
  await once(ws, 'open');
  const messages = [];
  ws.on('message', raw => messages.push(JSON.parse(raw)));
  async function receive(type) {
    while (!messages.some(message => message.type === type)) await once(ws, 'message');
    return messages.splice(messages.findIndex(message => message.type === type), 1)[0];
  }
  ws.send(JSON.stringify({ type: 'listRooms' }));
  assert.equal((await receive('rooms')).v, PROTOCOL_VERSION);
  ws.send(JSON.stringify({ type: 'create', v: PROTOCOL_VERSION + 1 }));
  const error = await receive('error');
  assert.equal(error.code, 'PROTOCOL_MISMATCH');
  assert.match(error.message, /Atualize/);
  ws.send(JSON.stringify({ type: 'create', v: PROTOCOL_VERSION, name: 'Ana' }));
  assert.ok((await receive('joined')).playerId);
});
```

E troque o import do topo:

```js
import { PROTOCOL_VERSION, decodeState } from './protocol.js';
```

- [ ] **Step 2: Rodar e ver falhar**

Run (em `../meu-game`): `node --test server/server.test.js`
Expected: FAIL — `PROTOCOL_VERSION` não é exportado (`SyntaxError: The requested module './protocol.js' does not provide an export named 'PROTOCOL_VERSION'`).

- [ ] **Step 3: Implementar** — em `server/protocol.js`, logo depois do `import { ENEMIES } ...`:

```js
/** Bump whenever the snapshot layout or any client/server message changes shape (native clients check it). */
export const PROTOCOL_VERSION = 1;
```

e adicione `export` às constantes existentes (sem mudar valores):

```js
export const VIEW_RADIUS = 1250;
export const ENEMY_TYPES = Object.keys(ENEMIES);
export const DROP_TYPES = ['gem', 'heart', 'greenGem', 'coin', 'magnet', 'chest'];
export const SPRITES = ['bolt', 'fire', 'thorn', 'blade'];
export const STATUSES = ['horde', 'boss', 'transition', 'complete'];
```
```js
export const ENCOUNTER_KINDS = ['merchant', 'shrine', 'thief'];
```
```js
export const PLAYER_KEYS = ['id', 'name', /* ...lista existente, inalterada... */];
```

(`VIEW_RADIUS` já era exportado; só confira.)

Em `server/server.js`, troque `import { encodeState } from './protocol.js';` por:

```js
import { PROTOCOL_VERSION, encodeState } from './protocol.js';
```

No handler, logo depois do bloco `if (message.type === 'listRooms') { ... }`, acrescente:

```js
    // Native clients ship on their own schedule: refuse a different wire format instead of misreading it.
    // Web clients from before versioning send no `v` and speak version 1.
    if (['create', 'join', 'resume'].includes(message.type) && (message.v ?? 1) !== PROTOCOL_VERSION) {
      return send(ws, { type: 'error', code: 'PROTOCOL_MISMATCH', message: 'Atualize o jogo para jogar online.' });
    }
```

e troque a resposta de `listRooms` por:

```js
      return send(ws, { type: 'rooms', rooms: openRoomList(), capacity: { used: rooms.size, max: MAX_ROOMS }, v: PROTOCOL_VERSION });
```

Em `src/net.js`, troque a primeira linha por `import { PROTOCOL_VERSION, decodeState } from '../server/protocol.js';` e as linhas 61-62 por:

```js
      if (resume) ws.send(JSON.stringify({ type: 'resume', v: PROTOCOL_VERSION, room: session.room, token: session.token }));
      else ws.send(JSON.stringify({ type: entry.action, v: PROTOCOL_VERSION, room: entry.code, name: entry.name, visibility: entry.visibility, color: entry.color, meta: entry.meta, campaign: entry.campaign, curses: entry.curses, loadout: entry.loadout }));
```

Em `src/main.js`, adicione `import { PROTOCOL_VERSION } from '../server/protocol.js';` junto aos outros imports e troque `requestEntry` por:

```js
  const requestEntry = color => current.send({ type: action, v: PROTOCOL_VERSION, room: code, name: playerName(), visibility, color, meta: wallet.upgrades,
    campaign: menu.campaign, curses: menu.curses, loadout: menu.loadout });
```

- [ ] **Step 4: Rodar tudo**

Run: `npm run check`
Expected: lint, typecheck e todos os testes passam (os testes antigos criam salas sem `v` e continuam passando).

- [ ] **Step 5: Commit**

```bash
git -C ../meu-game add server/protocol.js server/server.js server/server.test.js src/net.js src/main.js
git -C ../meu-game commit -m "feat(protocol): version the wire format and refuse mismatched clients

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Fixtures do protocolo geradas pelo JS

**Files:**
- Create: `../meu-game/scripts/export-protocol-fixtures.mjs`
- Create: `../meu-game/server/fixtures.test.js`
- Modify: `../meu-game/package.json` (script `fixtures:cpp`)
- Create (gerado): `tests/fixtures/protocol/snapshots.json` (neste repositório)

**Interfaces:**
- Consumes: exports da Task 1.
- Produces: `buildFixtures()` → `{ version, tables: { enemyTypes, dropTypes, sprites, statuses, encounterKinds }, cases: [{ name, viewer, encoded, decoded }] }`. O arquivo JSON segue exatamente esse formato; a Task 5 o lê.

- [ ] **Step 1: Escrever o teste que falha** — `server/fixtures.test.js`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { buildFixtures } from '../scripts/export-protocol-fixtures.mjs';
import { PROTOCOL_VERSION, decodeState } from './protocol.js';

test('fixtures do cliente C++ cobrem chefe, co-op de 4, sinais e fim de partida', { timeout: 120000 }, () => {
  const { version, tables, cases } = buildFixtures();
  assert.equal(version, PROTOCOL_VERSION);
  assert.equal(tables.enemyTypes.length, 23);
  for (const c of cases) assert.deepEqual(JSON.parse(JSON.stringify(decodeState(c.encoded))), c.decoded, c.name);
  assert.ok(cases.some(c => c.encoded.e.some(row => row[6] & 1)), 'algum caso com chefe');
  assert.ok(cases.some(c => c.encoded.p.length === 4), 'co-op de 4');
  assert.ok(cases.some(c => c.encoded.o === 1), 'fim de partida');
  assert.ok(cases.some(c => c.encoded.ev.some(e => e.kind === 'signal')), 'sinal');
  assert.ok(cases.some(c => c.encoded.a || c.encoded.en), 'altar ou encontro');
});
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `node --test server/fixtures.test.js`
Expected: FAIL — `Cannot find module '.../scripts/export-protocol-fixtures.mjs'`.

- [ ] **Step 3: Implementar** — `scripts/export-protocol-fixtures.mjs`:

```js
// Exports protocol fixtures for the native C++ client (wizard_coop_cpp): real matches are encoded with
// encodeState() and decoded with decodeState(), so the C++ decoder can be checked field by field.
// Usage: node scripts/export-protocol-fixtures.mjs <out.json>   (npm run fixtures:cpp)
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';
import { activateDash, activateSpecial, applyPower, createGameState, createPlayer, sendSignal, updateGame } from '../server/game.js';
import { DROP_TYPES, ENCOUNTER_KINDS, ENEMY_TYPES, PROTOCOL_VERSION, SPRITES, STATUSES, decodeState, encodeState } from '../server/protocol.js';

const TICK = 1 / 30;

function seeded(seed) {
  return () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
}

/** Circles around, takes the first power offered, fires specials when charged and dodges now and then. */
function drive(s, random) {
  Object.values(s.players).forEach((p, i) => {
    const a = s.time * 0.7 + i * 1.6;
    p.input = { x: Math.cos(a) * 0.8, y: Math.sin(a) * 0.8 };
    if (p.pendingPowers?.length) applyPower(p, p.pendingPowers[0]);
    if (p.specialCharge >= 100) activateSpecial(s, p.id, random);
    if (random() < 0.01) activateDash(s, p.id, p.input);
  });
}

function play({ players, campaign = 'quick', curses = [], seconds, seed }) {
  const random = seeded(seed);
  const s = createGameState(campaign, { curses });
  for (let i = 0; i < players; i++) {
    const p = createPlayer(`p${i + 1}`, `Arcanista ${i + 1}`, i);
    s.players[p.id] = p;
  }
  for (let t = 0; t < seconds && !s.over; t += TICK) {
    drive(s, random);
    updateGame(s, TICK, random);
  }
  return s;
}

export function buildFixtures() {
  const cases = [];
  const add = (name, s, viewer) => {
    const encoded = JSON.parse(JSON.stringify(encodeState(s, viewer)));
    cases.push({ name, viewer, encoded, decoded: JSON.parse(JSON.stringify(decodeState(encoded))) });
  };
  add('solo-start', play({ players: 1, seconds: 3, seed: 1 }), 'p1');
  add('solo-horde', play({ players: 1, seconds: 90, seed: 2 }), 'p1');
  add('solo-boss', play({ players: 1, seconds: 135, seed: 3 }), 'p1'); // quick hordes last 120 s
  const coop = play({ players: 4, campaign: 'classic', curses: ['swarm', 'tyrant'], seconds: 200, seed: 4 });
  sendSignal(coop, 'p2', 'help');
  sendSignal(coop, 'p3', 'look', { x: coop.players.p3.x + 200, y: coop.players.p3.y });
  add('coop4-viewer-p1', coop, 'p1');
  add('coop4-viewer-p3', coop, 'p3');
  add('coop4-spectator', coop, 'nobody');
  const over = play({ players: 2, seconds: 20, seed: 5 });
  for (const p of Object.values(over.players)) { p.hp = 0; p.alive = false; p.phoenix = 0; }
  for (let i = 0; i < 10 && !over.over; i++) updateGame(over, TICK);
  add('coop2-over', over, 'p1');
  return {
    version: PROTOCOL_VERSION,
    tables: { enemyTypes: ENEMY_TYPES, dropTypes: DROP_TYPES, sprites: SPRITES, statuses: STATUSES, encounterKinds: ENCOUNTER_KINDS },
    cases
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const out = process.argv[2];
  if (!out) { console.error('uso: node scripts/export-protocol-fixtures.mjs <saida.json>'); process.exit(1); }
  const fixtures = buildFixtures();
  fs.writeFileSync(out, JSON.stringify(fixtures));
  console.log(`${fixtures.cases.length} casos gravados em ${out}`);
}
```

Em `package.json`, dentro de `"scripts"`, acrescente:

```json
    "fixtures:cpp": "node scripts/export-protocol-fixtures.mjs ../wizard_coop_cpp/tests/fixtures/protocol/snapshots.json",
```

- [ ] **Step 4: Rodar o teste**

Run: `node --test server/fixtures.test.js`
Expected: PASS. Se a asserção "algum caso com chefe" falhar (o bot morreu antes de 120 s), aumente `seconds` de `solo-boss` até o chefe aparecer; se "altar ou encontro" falhar, aumente `seconds` de `solo-horde` (o altar surge durante a horda de 120 s).

- [ ] **Step 5: Gerar o arquivo e rodar a checagem completa**

```bash
mkdir -p ../wizard_coop_cpp/tests/fixtures/protocol
npm run fixtures:cpp
npm run check
```
Expected: `8 casos gravados em ../wizard_coop_cpp/tests/fixtures/protocol/snapshots.json`; `npm run check` passa.

- [ ] **Step 6: Commit (nos dois repositórios)**

```bash
git -C ../meu-game add scripts/export-protocol-fixtures.mjs server/fixtures.test.js package.json
git -C ../meu-game commit -m "test(protocol): export JS-generated fixtures for the native client

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
git add tests/fixtures/protocol/snapshots.json
git commit -m "test(online): protocol fixtures generated by meu-game

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Dependências, `ARCANA_BUILD_ONLINE` e a interface de transporte

**Files:**
- Modify: `CMakeLists.txt` (opção no topo; bloco novo depois de `arcana_core`; testes)
- Create: `platforms/net/transport.hpp`, `platforms/net/transport.cpp`
- Create: `tests/online_transport_tests.cpp`
- Create: `assets/cacert.pem`
- Modify: `tools/docker/host-build.sh`, `tools/docker/windows-build.sh`, `tools/docker/build-all.sh`, `tools/docker/build-all.ps1`

**Interfaces:**
- Produces (namespace `arcana::online`):
  - `struct TransportEvent { enum class Type { Open, Message, Close, Error }; Type type; std::string text; int code; bool tls; }`
  - `class Transport { virtual void open(const std::string& url); virtual void send(std::string text); virtual void close(); virtual void poll(std::vector<TransportEvent>& out); }`
  - `using TransportFactory = std::function<std::unique_ptr<Transport>()>;`
  - `enum class UrlCheck { Ok, Invalid, InsecureBlocked }; UrlCheck checkServerUrl(std::string_view url, bool allowInsecure);`
  - CMake: alvos `arcana_online` (glob de `platforms/net/*.cpp`), `arcana_ixwebsocket`, `arcana_zlib`; função `arcana_online_test(name libs...)`.

- [ ] **Step 1: Escrever o teste que falha** — `tests/online_transport_tests.cpp`:

```cpp
// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "transport.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <mbedtls/version.h>
#include <nlohmann/json.hpp>
#include <zlib.h>

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace arcana::online;

int main() {
  // Server URL policy: TLS always; plain ws:// only for this machine unless explicitly allowed.
  assert(checkServerUrl("wss://vps65228.publiccloud.com.br/ws", false) == UrlCheck::Ok);
  assert(checkServerUrl("ws://localhost:8081", false) == UrlCheck::Ok);
  assert(checkServerUrl("ws://127.0.0.1:8081/ws", false) == UrlCheck::Ok);
  assert(checkServerUrl("ws://example.com/ws", false) == UrlCheck::InsecureBlocked);
  assert(checkServerUrl("ws://example.com/ws", true) == UrlCheck::Ok);
  assert(checkServerUrl("https://example.com", false) == UrlCheck::Invalid);
  assert(checkServerUrl("wss://", false) == UrlCheck::Invalid);
  assert(checkServerUrl("", false) == UrlCheck::Invalid);

  // The fetched dependencies build and link (versions pinned in CMakeLists.txt).
  assert(std::strcmp(MBEDTLS_VERSION_STRING, "3.6.4") == 0);
  assert(std::strcmp(zlibVersion(), "1.3.1") == 0);
  assert(nlohmann::json::parse(R"({"a":[1,2]})")["a"][1] == 2);
  ix::WebSocket socket;
  socket.setUrl("wss://localhost/ws");
  std::puts("online_transport: ok");
  return 0;
}
```

- [ ] **Step 2: Declarar a opção, as dependências e o teste no CMake** — em `CMakeLists.txt`, depois de `option(ARCANA_REAL_FLOAT ...)`:

```cmake
option(ARCANA_BUILD_ONLINE "Online co-op client for the PC (IXWebSocket + Mbed TLS, fetched by CMake)" OFF)
```

Logo depois do bloco `if(MSVC) ... else() ... endif()` que configura `arcana_core` (antes de `add_executable(arcana_sim ...)`), acrescente:

```cmake
if(ARCANA_BUILD_ONLINE)
  # Everything is fetched at pinned versions and linked statically: the Windows .exe still ships
  # only the SDL2 DLLs. IXWebSocket and zlib are compiled from their sources with our own flags.
  enable_language(C)
  find_package(Threads REQUIRED)
  include(FetchContent)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(GEN_FILES OFF CACHE BOOL "" FORCE)
  set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
  set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
  set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
  set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.4/mbedtls-3.6.4.tar.bz2
    URL_HASH SHA256=ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
  FetchContent_Declare(zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
  FetchContent_Declare(ixwebsocket
    URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v11.4.6.tar.gz
    URL_HASH SHA256=c024334f8e45980836c67008979a884d6dcc5ef067dd2eb1fa7241f4c17ddc32)
  FetchContent_MakeAvailable(mbedtls nlohmann_json)
  foreach(dep zlib ixwebsocket) # sources only
    FetchContent_GetProperties(${dep})
    if(NOT ${dep}_POPULATED)
      FetchContent_Populate(${dep})
    endif()
  endforeach()

  # zlib: only the deflate/inflate streams permessage-deflate needs (no gz* file API).
  add_library(arcana_zlib STATIC
    ${zlib_SOURCE_DIR}/adler32.c ${zlib_SOURCE_DIR}/compress.c ${zlib_SOURCE_DIR}/crc32.c ${zlib_SOURCE_DIR}/deflate.c
    ${zlib_SOURCE_DIR}/infback.c ${zlib_SOURCE_DIR}/inffast.c ${zlib_SOURCE_DIR}/inflate.c ${zlib_SOURCE_DIR}/inftrees.c
    ${zlib_SOURCE_DIR}/trees.c ${zlib_SOURCE_DIR}/uncompr.c ${zlib_SOURCE_DIR}/zutil.c)
  target_include_directories(arcana_zlib PUBLIC ${zlib_SOURCE_DIR})

  # IXWebSocket: every source is guarded by its IXWEBSOCKET_USE_* macro, so globbing them is safe.
  file(GLOB ARCANA_IX_SOURCES ${ixwebsocket_SOURCE_DIR}/ixwebsocket/*.cpp)
  add_library(arcana_ixwebsocket STATIC ${ARCANA_IX_SOURCES})
  target_include_directories(arcana_ixwebsocket PUBLIC ${ixwebsocket_SOURCE_DIR})
  target_compile_definitions(arcana_ixwebsocket PUBLIC IXWEBSOCKET_USE_TLS IXWEBSOCKET_USE_MBED_TLS IXWEBSOCKET_USE_ZLIB)
  target_link_libraries(arcana_ixwebsocket PUBLIC mbedtls mbedx509 mbedcrypto arcana_zlib Threads::Threads)
  if(WIN32)
    target_link_libraries(arcana_ixwebsocket PUBLIC ws2_32 wsock32 shlwapi crypt32 bcrypt)
  endif()

  file(GLOB ARCANA_ONLINE_SOURCES CONFIGURE_DEPENDS platforms/net/*.cpp)
  add_library(arcana_online ${ARCANA_ONLINE_SOURCES})
  target_include_directories(arcana_online PUBLIC platforms/net)
  target_link_libraries(arcana_online PUBLIC arcana_core nlohmann_json::nlohmann_json arcana_ixwebsocket)
  if(NOT MSVC)
    target_compile_options(arcana_online PRIVATE -Wall -Wextra)
  endif()
endif()
```

No bloco `if(ARCANA_BUILD_TESTS)`, antes do `endif()` final, acrescente:

```cmake
  if(TARGET arcana_online)
    function(arcana_online_test name)
      add_executable(arcana_${name}_tests tests/${name}_tests.cpp)
      target_link_libraries(arcana_${name}_tests PRIVATE ${ARGN})
      target_include_directories(arcana_${name}_tests PRIVATE tests)
      target_compile_definitions(arcana_${name}_tests PRIVATE ARCANA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
      add_test(NAME ${name} COMMAND arcana_${name}_tests)
    endfunction()
    arcana_online_test(online_transport arcana_online)
  endif()
```

- [ ] **Step 3: Rodar e ver falhar**

Run (no Docker do host, Git Bash): `MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W)":/src -w /src arcana-host bash -c "cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DARCANA_BUILD_SERVER=OFF -DARCANA_BUILD_NET=OFF -DARCANA_BUILD_CLIENT=OFF -DARCANA_BUILD_TESTS=ON -DARCANA_BUILD_DESKTOP=ON -DARCANA_BUILD_ONLINE=ON && cmake --build build-linux --target arcana_online_transport_tests"`
Expected: FAIL no configure/compile — `add_library cannot create target "arcana_online" ... No SOURCES given` ou `transport.hpp: No such file`.

(Se a imagem `arcana-host` não existir: `docker build -t arcana-host -f tools/docker/host.Dockerfile tools/docker`.)

- [ ] **Step 4: Implementar o transporte** — `platforms/net/transport.hpp`:

```cpp
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arcana::online {

// What a network thread observed, handed to the game thread by Transport::poll().
struct TransportEvent {
  enum class Type { Open, Message, Close, Error };
  Type type{Type::Message};
  std::string text;  // Message: payload; Close: reason; Error: description
  int code{};        // Close: WebSocket close code
  bool tls{};        // Error: the TLS handshake/certificate failed
};

// A WebSocket connection. Implementations deliver events from their own thread through poll(), so
// the game (and the tests, with a fake) consume them on the main thread only.
class Transport {
public:
  virtual ~Transport() = default;
  virtual void open(const std::string& url) = 0;
  virtual void send(std::string text) = 0;
  // Polite close; no further events are delivered.
  virtual void close() = 0;
  // Appends the events queued since the last call.
  virtual void poll(std::vector<TransportEvent>& out) = 0;
};

using TransportFactory = std::function<std::unique_ptr<Transport>()>;

enum class UrlCheck { Ok, Invalid, InsecureBlocked };
// wss:// always; plain ws:// only for localhost/127.0.0.1 unless `allowInsecure` (--insecure-ws).
UrlCheck checkServerUrl(std::string_view url, bool allowInsecure);

} // namespace arcana::online
```

`platforms/net/transport.cpp`:

```cpp
#include "transport.hpp"

namespace arcana::online {

UrlCheck checkServerUrl(std::string_view url, bool allowInsecure) {
  bool secure = false;
  std::string_view rest;
  if (url.substr(0, 6) == "wss://") { secure = true; rest = url.substr(6); }
  else if (url.substr(0, 5) == "ws://") rest = url.substr(5);
  else return UrlCheck::Invalid;
  const std::string_view host = rest.substr(0, rest.find_first_of(":/"));
  if (host.empty()) return UrlCheck::Invalid;
  if (secure || allowInsecure || host == "localhost" || host == "127.0.0.1") return UrlCheck::Ok;
  return UrlCheck::InsecureBlocked;
}

} // namespace arcana::online
```

- [ ] **Step 5: Baixar o bundle de certificados**

```bash
curl -fsSL -o assets/cacert.pem https://curl.se/ca/cacert.pem
head -c 400 assets/cacert.pem
```
Expected: cabeçalho `## Bundle of CA Root Certificates` com a data do bundle.

- [ ] **Step 6: Ligar a opção nos builds Docker**

Em `tools/docker/host-build.sh` e `tools/docker/windows-build.sh`, acrescente `-DARCANA_BUILD_ONLINE=ON` à linha que tem `-DARCANA_BUILD_DESKTOP=ON`. Em `windows-build.sh`, depois de `cp assets/fonts/* "$BUNDLE/assets/fonts/"`, acrescente:

```bash
cp assets/cacert.pem "$BUNDLE/assets/"
```

Em `tools/docker/build-all.sh`, no caso `host)`, depois de `cp assets/native_atlas_128.png assets/terrain_tiles.png dist/linux/`:

```bash
        cp assets/cacert.pem dist/linux/
```

Em `tools/docker/build-all.ps1`, depois da linha `Copy-Item assets\native_atlas_128.png, assets\terrain_tiles.png dist\linux\ -Force`:

```powershell
        Copy-Item assets\cacert.pem dist\linux\ -Force
```

- [ ] **Step 7: Rodar os testes (Linux) e o build do Windows**

Run: `tools/docker/build-all.sh host windows`
Expected: `host` ok com `online_transport` passando no `ctest`; `windows` ok e a lista "DLLs importadas" continua só com DLLs do Windows + `SDL2*.dll` (aparecem `WS2_32.dll`, `CRYPT32.dll`, `bcrypt.dll`, que são do sistema).

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt platforms/net/transport.hpp platforms/net/transport.cpp tests/online_transport_tests.cpp assets/cacert.pem tools/docker
git commit -m "build(online): fetch IXWebSocket/mbedTLS/zlib/json and add the transport interface

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 4: Acréscimos no core e no perfil

**Files:**
- Modify: `include/arcana/game.hpp` (struct `Event`; declaração nova depois de `activateDash`)
- Modify: `src/game.cpp` (fim do arquivo)
- Modify: `include/arcana/profile.hpp` (struct `Preferences`)
- Modify: `src/profile.cpp` (`loadProfile`, `saveProfile`)
- Test: `tests/core_tests.cpp`, `tests/meta_tests.cpp`

**Interfaces:**
- Produces: `Event::player`, `Event::name` (`std::string`); `Vec2 playerMovement(const Player& p, real dt);` (mesma fórmula de `movement.js movementDelta`, usando `p.input`); `Preferences::name`, `Preferences::server` persistidos como `pref.name=` e `pref.server=`.

- [ ] **Step 1: Escrever os testes que falham**

Em `tests/core_tests.cpp`, antes do `return 0;` final de `main()`:

```cpp
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
```

(Se `core_tests.cpp` ainda não inclui `<cmath>`, acrescente `#include <cmath>`.)

Em `tests/meta_tests.cpp`, antes de `std::remove(kPath);` no fim de `main()`:

```cpp
  // Online preferences survive a save/load round trip.
  {
    Profile p;
    p.prefs.name = "Ana Lú";
    p.prefs.server = "wss://example.com/ws";
    assert(saveProfileAtomic(p, kPath));
    const Profile q = loadProfile(kPath);
    assert(q.prefs.name == "Ana Lú");
    assert(q.prefs.server == "wss://example.com/ws");
  }
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL de compilação — `'playerMovement' was not declared`, `'struct arcana::Event' has no member named 'player'`, `'struct arcana::Preferences' has no member named 'name'`.

- [ ] **Step 3: Implementar**

Em `include/arcana/game.hpp`, troque a linha do `struct Event` por:

```cpp
struct Event { std::uint64_t id{}; std::string kind; real t{},x{},y{},r{},a{}; int color{},stage{},variant{}; std::string text; native::StaticVector<real, 32> points;
  std::string player, name; }; // player/name: who sent a signal (filled by the online decoder)
```

e, logo depois de `bool activateDash(GameState& s, const std::string& playerId, Vec2 input);`:

```cpp
// One step of a player's own movement (walk + dash burst) from p.input, as the server applies it.
// The online client uses it to predict the local player between snapshots.
Vec2 playerMovement(const Player& p, real dt);
```

Em `src/game.cpp`, imediatamente antes da última linha (`} // namespace arcana`):

```cpp
Vec2 playerMovement(const Player& p, real dt) { return movementDelta(p, dt); }

```

Em `include/arcana/profile.hpp`, troque a linha do `struct Preferences` por:

```cpp
struct Preferences { std::array<int,cfg::MAX_PLAYERS> characters{0,1,2,3}; std::string campaign{"quick"}, weapon; int special{}; bool muted{};
  std::string name, server; }; // online: player name and a server URL overriding the default
```

Em `src/profile.cpp`, em `loadProfile`, antes de `else if(k=="pref.muted")p.prefs.muted=v=="1";`, acrescente:

```cpp
else if(k=="pref.name")p.prefs.name=v.substr(0,64);else if(k=="pref.server")p.prefs.server=v;
```

e em `saveProfile`, troque `f<<"pref.campaign="<<p.prefs.campaign<<"\npref.weapon="<<p.prefs.weapon<<"\npref.special="<<p.prefs.special<<"\npref.muted="<<(p.prefs.muted?1:0)<<'\n';` por:

```cpp
f<<"pref.campaign="<<p.prefs.campaign<<"\npref.weapon="<<p.prefs.weapon<<"\npref.special="<<p.prefs.special<<"\npref.muted="<<(p.prefs.muted?1:0)<<"\npref.name="<<p.prefs.name<<"\npref.server="<<p.prefs.server<<'\n';
```

- [ ] **Step 4: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `host ok (testes passaram)` — `core`, `meta` e os demais passam.

- [ ] **Step 5: Conferir que o console ainda compila**

Run: `tools/docker/build-all.sh switch`
Expected: `switch ok`.

- [ ] **Step 6: Commit**

```bash
git add include/arcana/game.hpp src/game.cpp include/arcana/profile.hpp src/profile.cpp tests/core_tests.cpp tests/meta_tests.cpp
git commit -m "feat(core): expose playerMovement, signal sender on events, online prefs

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 5: Decodificador de snapshots (`protocol.cpp`)

**Files:**
- Create: `platforms/net/protocol.hpp`, `platforms/net/protocol.cpp`
- Create: `tests/online_protocol_tests.cpp`
- Modify: `CMakeLists.txt` (uma linha de teste)

**Interfaces:**
- Consumes: `Event::player/name` (Task 4); fixtures (Task 2).
- Produces (namespace `arcana::online`):
  - `constexpr int kProtocolVersion = 1;`
  - `extern const std::array<const char*, 23> kEnemyTypes; ... kDropTypes (6), kSprites (4), kStatuses (4), kEncounterKinds (3)`
  - `struct DecodeStats { int truncated{}; };`
  - `bool decodeSnapshot(const nlohmann::json& compact, GameState& out, DecodeStats* stats = nullptr);` — `out` deve estar recém-construído; nunca lança.
  - `std::string sanitizeName(std::string_view utf8);`

- [ ] **Step 1: Escrever o teste que falha** — `tests/online_protocol_tests.cpp`:

```cpp
// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

using namespace arcana;
using namespace arcana::online;
using nlohmann::json;

namespace {
std::string current;
[[noreturn]] void fail(const std::string& what) { std::fprintf(stderr, "[%s] %s\n", current.c_str(), what.c_str()); std::exit(1); }
void near(double actual, double expected, const std::string& what) {
  if (std::fabs(actual - expected) > 1e-6) fail(what + ": " + std::to_string(actual) + " != " + std::to_string(expected));
}
void same(const std::string& actual, const std::string& expected, const std::string& what) {
  if (actual != expected) fail(what + ": '" + actual + "' != '" + expected + "'");
}
double num(const json& o, const char* key, double fallback = 0) {
  const auto it = o.find(key);
  return it != o.end() && it->is_number() ? it->get<double>() : fallback;
}
bool yes(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && ((it->is_boolean() && it->get<bool>()) || (it->is_number() && it->get<double>() != 0));
}
std::string str(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

void comparePlayer(const Player& p, const json& e) {
  const std::string at = "player " + p.id + " ";
  same(p.id, str(e, "id"), at + "id");
  same(p.name, str(e, "name"), at + "name");
  near(p.color, num(e, "color"), at + "color");
  for (const auto& [key, value] : std::initializer_list<std::pair<const char*, double>>{
         {"x", p.x}, {"y", p.y}, {"hp", p.hp}, {"maxHp", p.maxHp}, {"xp", p.xp}, {"level", p.level}, {"speed", p.speed},
         {"specialCharge", p.specialCharge}, {"coins", p.coins}, {"reviveProgress", p.reviveProgress}, {"castCount", p.castCount},
         {"castAngle", p.castAngle}, {"invulnerableFor", p.invulnerableFor}, {"orbitAngle", p.orbitAngle}, {"rerolls", p.rerolls},
         {"phoenix", p.phoenix}, {"inputSeq", static_cast<double>(p.inputSeq)}, {"powerTimer", p.powerTimer}, {"dashFor", p.dashFor},
         {"dashCooldown", p.dashCooldown}, {"dashX", p.dashX}, {"dashY", p.dashY}, {"moveX", p.moveX}, {"moveY", p.moveY},
         {"specialCooldown", p.specialCooldown}, {"motionId", static_cast<double>(p.motionId)}, {"specialVariant", p.specialVariant},
         {"shopProgress", p.shopProgress}})
    near(value, num(e, key), at + key);
  near(p.alive, yes(e, "alive"), at + "alive");
  near(p.connected, yes(e, "connected"), at + "connected");
  same(p.reviveBy, str(e, "reviveBy"), at + "reviveBy");
  same(p.reviving, str(e, "reviving"), at + "reviving");
  const json& powers = e.at("powers");
  if (p.powers.size() != powers.size()) fail(at + "powers size");
  for (const auto& [id, rank] : powers.items()) near(p.powers.at(id), rank.get<double>(), at + "power " + id);
  const json& pending = e.at("pendingPowers");
  if (pending.is_null() ? !p.pendingPowers.empty() : pending.get<std::vector<std::string>>() != p.pendingPowers) fail(at + "pendingPowers");
  const json& stats = e.at("stats");
  near(p.stats.kills, num(stats, "kills"), at + "kills");
  near(p.stats.damage, num(stats, "damage"), at + "damage");
  near(p.stats.revives, num(stats, "revives"), at + "revives");
  near(p.stats.taken, num(stats, "taken"), at + "taken");
  if (stats.contains("by")) for (const auto& [kind, v] : stats["by"].items()) near(p.stats.by.at(kind), v.get<double>(), at + "by " + kind);
  const json& familiar = e.at("familiar");
  if (familiar.is_null() != !p.familiar.has_value()) fail(at + "familiar presence");
  if (p.familiar) { near(p.familiar->x, num(familiar, "x"), at + "familiar.x"); near(p.familiar->y, num(familiar, "y"), at + "familiar.y"); }
}

void compareCase(const json& c) {
  current = c.at("name").get<std::string>();
  auto state = std::make_unique<GameState>();
  if (!decodeSnapshot(c.at("encoded"), *state)) fail("decode failed");
  const json& d = c.at("decoded");
  near(state->time, num(d, "time"), "time");
  same(state->campaign, str(d, "campaign"), "campaign");
  near(state->over, yes(d, "over"), "over");
  near(state->victory, yes(d, "victory"), "victory");
  near(state->phase, num(d, "phase"), "phase");
  near(state->phaseTime, num(d, "phaseTime"), "phaseTime");
  same(state->phaseStatus, str(d, "phaseStatus"), "phaseStatus");
  near(state->transitionTime, num(d, "transitionTime"), "transitionTime");
  near(state->loop, num(d, "loop"), "loop");
  near(state->bloodPact, yes(d, "bloodPact"), "bloodPact");
  if (state->curses != d.at("curses").get<std::vector<std::string>>()) fail("curses");

  const bool hasAltar = d.contains("altar") && d["altar"].is_object();
  if (state->altar.has_value() != hasAltar) fail("altar presence");
  if (hasAltar) {
    const json& a = d["altar"];
    near(state->altar->x, num(a, "x"), "altar.x"); near(state->altar->y, num(a, "y"), "altar.y");
    near(state->altar->radius, num(a, "radius"), "altar.radius"); near(state->altar->progress, num(a, "progress"), "altar.progress");
    near(state->altar->ttl, num(a, "ttl"), "altar.ttl"); same(state->altar->status, str(a, "status"), "altar.status");
  }
  const bool hasEncounter = d.contains("encounter") && d["encounter"].is_object();
  if (state->encounter.has_value() != hasEncounter) fail("encounter presence");
  if (hasEncounter) {
    const json& en = d["encounter"];
    same(state->encounter->kind, str(en, "kind"), "encounter.kind"); same(state->encounter->status, str(en, "status"), "encounter.status");
    near(state->encounter->x, num(en, "x"), "encounter.x"); near(state->encounter->progress, num(en, "progress"), "encounter.progress");
    near(state->encounter->buyers.size(), en.at("buyers").size(), "encounter.buyers");
  }

  const json& players = d.at("players");
  if (state->players.size() != players.size()) fail("player count");
  for (const auto& [id, e] : players.items()) {
    const auto it = state->players.find(id);
    if (it == state->players.end()) fail("missing player " + id);
    comparePlayer(it->second, e);
  }

  const json& enemies = d.at("enemies");
  if (state->enemies.size() != enemies.size()) fail("enemy count");
  for (std::size_t i = 0; i < enemies.size(); ++i) {
    const Enemy& g = state->enemies[i];
    const json& e = enemies[i];
    const std::string at = "enemy " + std::to_string(i) + " ";
    near(static_cast<double>(g.id), num(e, "id"), at + "id"); same(g.type, str(e, "type"), at + "type");
    near(g.x, num(e, "x"), at + "x"); near(g.y, num(e, "y"), at + "y"); near(g.hp, num(e, "hp"), at + "hp"); near(g.maxHp, num(e, "maxHp"), at + "maxHp");
    near(g.boss, yes(e, "boss"), at + "boss"); near(g.elite, yes(e, "elite"), at + "elite"); near(g.thief, yes(e, "thief"), at + "thief");
    near(g.slowFor, num(e, "slowFor"), at + "slowFor"); near(g.windup, num(e, "windup"), at + "windup"); near(g.fuse, num(e, "fuse"), at + "fuse");
    near(g.dashWarn, num(e, "dashWarn"), at + "dashWarn"); near(g.rootFor, num(e, "rootFor"), at + "rootFor");
    near(g.freezeFor, num(e, "freezeFor"), at + "freezeFor"); near(g.burningFor, num(e, "burningFor"), at + "burningFor");
    if (g.boss) { near(g.stage, num(e, "stage", 1), at + "stage"); near(g.dashAngle, num(e, "dashAngle"), at + "dashAngle"); }
  }

  const json& shots = d.at("shots");
  if (state->shots.size() != shots.size()) fail("shot count");
  for (std::size_t i = 0; i < shots.size(); ++i) {
    const Shot& g = state->shots[i];
    const json& e = shots[i];
    near(g.x, num(e, "x"), "shot.x"); near(g.vy, num(e, "vy"), "shot.vy"); near(g.color, num(e, "color"), "shot.color");
    near(g.special, yes(e, "special"), "shot.special"); near(g.shard, yes(e, "shard"), "shot.shard");
    near(g.returning, yes(e, "returning"), "shot.returning"); near(g.fullmoon, yes(e, "fullmoon"), "shot.fullmoon");
  }
  const json& enemyShots = d.at("enemyShots");
  if (state->enemyShots.size() != enemyShots.size()) fail("enemy shot count");
  for (std::size_t i = 0; i < enemyShots.size(); ++i) {
    near(state->enemyShots[i].x, num(enemyShots[i], "x"), "enemyShot.x");
    same(state->enemyShots[i].sprite, str(enemyShots[i], "sprite"), "enemyShot.sprite");
  }
  const json& gems = d.at("gems");
  if (state->gems.size() != gems.size()) fail("gem count");
  for (std::size_t i = 0; i < gems.size(); ++i) {
    near(static_cast<double>(state->gems[i].id), num(gems[i], "id"), "gem.id");
    same(state->gems[i].type, str(gems[i], "type"), "gem.type");
    near(state->gems[i].value, num(gems[i], "value"), "gem.value");
  }
  const json& hazards = d.at("hazards");
  if (state->hazards.size() != hazards.size()) fail("hazard count");
  for (std::size_t i = 0; i < hazards.size(); ++i) {
    near(state->hazards[i].radius, num(hazards[i], "radius"), "hazard.radius");
    near(state->hazards[i].warning, num(hazards[i], "warning"), "hazard.warning");
    near(state->hazards[i].fired, yes(hazards[i], "fired"), "hazard.fired");
  }
  const json& runes = d.at("runes");
  if (state->runes.size() != runes.size()) fail("rune count");
  for (std::size_t i = 0; i < runes.size(); ++i) near(state->runes[i].arm, num(runes[i], "arm"), "rune.arm");
  const json& zones = d.at("zones");
  if (state->zones.size() != zones.size()) fail("zone count");
  for (std::size_t i = 0; i < zones.size(); ++i) {
    same(state->zones[i].kind, str(zones[i], "kind"), "zone.kind");
    near(state->zones[i].warning, num(zones[i], "warning"), "zone.warning");
  }
  const json& events = d.at("events");
  if (state->events.size() != events.size()) fail("event count");
  for (std::size_t i = 0; i < events.size(); ++i) {
    const Event& g = state->events[i];
    const json& e = events[i];
    near(static_cast<double>(g.id), num(e, "id"), "event.id"); same(g.kind, str(e, "kind"), "event.kind");
    near(g.x, num(e, "x"), "event.x"); near(g.y, num(e, "y"), "event.y");
    same(g.player, str(e, "player"), "event.player");
    if (g.kind == "signal") same(g.text, str(e, "signal"), "event.signal");
    if (e.contains("points")) near(g.points.size(), e["points"].size(), "event.points");
  }
}
} // namespace

int main() {
  std::ifstream file(ARCANA_FIXTURES_DIR "/protocol/snapshots.json");
  if (!file) fail("fixtures ausentes: rode `npm run fixtures:cpp` no meu-game");
  const json fixtures = json::parse(file);

  current = "tables";
  near(fixtures.at("version").get<int>(), kProtocolVersion, "version");
  auto table = [](const json& list, const auto& ours, const char* name) {
    if (list.size() != ours.size()) fail(std::string(name) + " size");
    for (std::size_t i = 0; i < ours.size(); ++i) same(ours[i], list[i].get<std::string>(), name);
  };
  const json& tables = fixtures.at("tables");
  table(tables.at("enemyTypes"), kEnemyTypes, "enemyTypes");
  table(tables.at("dropTypes"), kDropTypes, "dropTypes");
  table(tables.at("sprites"), kSprites, "sprites");
  table(tables.at("statuses"), kStatuses, "statuses");
  table(tables.at("encounterKinds"), kEncounterKinds, "encounterKinds");

  for (const auto& c : fixtures.at("cases")) compareCase(c);

  current = "robustness";
  {
    auto s = std::make_unique<GameState>();
    assert(!decodeSnapshot(json::parse("[]"), *s));
    assert(!decodeSnapshot(json::parse(R"({"t":1})"), *s)); // no players list
    assert(!decodeSnapshot(json::parse(R"({"t":1,"p":"nope"})"), *s));
  }
  {
    // 200 enemies, one with an unknown type index: skipped; the rest fills the fixed capacity.
    json c = json::parse(R"({"t":1,"p":[],"e":[],"s":[],"es":[],"g":[],"h":[],"r":[],"z":[],"ev":[]})");
    for (int i = 0; i < 200; ++i) c["e"].push_back(json::array({i + 1, i == 5 ? 99 : 0, 0, 0, 10, 10, 0}));
    auto s = std::make_unique<GameState>();
    DecodeStats stats;
    assert(decodeSnapshot(c, *s, &stats));
    assert(s->enemies.size() == static_cast<std::size_t>(cfg::MAX_ENEMIES));
    assert(stats.truncated == 199 - cfg::MAX_ENEMIES);
  }
  assert(sanitizeName("Ana\x01L\xC3\xBA") == "AnaL\xC3\xBA");
  assert(sanitizeName("abcdefghijklmnopqrstu") == "abcdefghijklmnop");
  assert(sanitizeName(std::string(20, 'a') + "\xC3").size() == 16);
  std::puts("online_protocol: ok");
  return 0;
}
```

No `CMakeLists.txt`, depois de `arcana_online_test(online_transport arcana_online)`:

```cmake
    arcana_online_test(online_protocol arcana_online)
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL de compilação — `protocol.hpp: No such file or directory`.

- [ ] **Step 3: Implementar** — `platforms/net/protocol.hpp`:

```cpp
#pragma once

#include "arcana/game.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <string>
#include <string_view>

namespace arcana::online {

// Must match PROTOCOL_VERSION in meu-game/server/protocol.js.
constexpr int kProtocolVersion = 1;

// Index tables of server/protocol.js, in the same order: the wire sends positions in these lists.
extern const std::array<const char*, 23> kEnemyTypes;
extern const std::array<const char*, 6> kDropTypes;
extern const std::array<const char*, 4> kSprites;
extern const std::array<const char*, 4> kStatuses;
extern const std::array<const char*, 3> kEncounterKinds;

struct DecodeStats { int truncated{}; };  // entities dropped because a fixed-capacity list was full

// Port of decodeState() (server/protocol.js): rebuilds the compact snapshot `c` into `out`, which
// must be freshly constructed. Unknown table indices skip the entity; lists longer than the fixed
// capacities are cut. Returns false for a malformed snapshot. Never throws.
bool decodeSnapshot(const nlohmann::json& compact, GameState& out, DecodeStats* stats = nullptr);

// Player names from the server: control characters removed, at most 16 codepoints.
std::string sanitizeName(std::string_view utf8);

} // namespace arcana::online
```

`platforms/net/protocol.cpp`:

```cpp
#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace arcana::online {

using nlohmann::json;

const std::array<const char*, 23> kEnemyTypes = {
  "slime", "slimelet", "bat", "eye", "brute", "mushroom", "beetle", "skeleton", "wraith", "imp", "scorpion", "treant",
  "lich", "demon", "spore", "revenant", "sentinel", "seer", "voidling", "voidscarab", "bogwarden", "archon", "umbra"};
const std::array<const char*, 6> kDropTypes = {"gem", "heart", "greenGem", "coin", "magnet", "chest"};
const std::array<const char*, 4> kSprites = {"bolt", "fire", "thorn", "blade"};
const std::array<const char*, 4> kStatuses = {"horde", "boss", "transition", "complete"};
const std::array<const char*, 3> kEncounterKinds = {"merchant", "shrine", "thief"};

namespace {

// ENEMY_FLAGS / SHOT_FLAGS of server/protocol.js.
enum EnemyFlag : int { FBoss = 1, FElite = 2, FSlowed = 4, FWindup = 8, FFuse = 16, FDashWarn = 32, FRoot = 64, FFreeze = 128, FBurning = 256, FThief = 512 };
enum ShotFlag : int { SSpecial = 1, SShard = 2, SReturning = 4, SFullmoon = 8 };

const json& emptyArray() { static const json empty = json::array(); return empty; }
const json& list(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && it->is_array() ? *it : emptyArray();
}
double num(const json& row, std::size_t i, double fallback = 0) {
  return i < row.size() && row[i].is_number() ? row[i].get<double>() : fallback;
}
std::string str(const json& row, std::size_t i) {
  return i < row.size() && row[i].is_string() ? row[i].get<std::string>() : std::string{};
}
bool truthy(const json& v) { return (v.is_boolean() && v.get<bool>()) || (v.is_number() && v.get<double>() != 0); }
bool flag(const json& row, std::size_t i) { return i < row.size() && truthy(row[i]); }
double field(const json& o, const char* key, double fallback = 0) {
  const auto it = o.find(key);
  return it != o.end() && it->is_number() ? it->get<double>() : fallback;
}
std::string text(const json& o, const char* key) {
  const auto it = o.find(key);
  return it != o.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
template <class Table> const char* lookup(const Table& table, double index) {
  if (!(index >= 0) || index >= static_cast<double>(table.size()) || index != std::floor(index)) return nullptr;
  return table[static_cast<std::size_t>(index)];
}
std::uint64_t id(double v) { return v > 0 ? static_cast<std::uint64_t>(v) : 0; }
template <class List, class T> void add(List& to, T&& item, DecodeStats* stats) {
  if (!to.push_back(std::forward<T>(item)) && stats) ++stats->truncated;
}

// Row layout: PLAYER_KEYS of server/protocol.js.
Player decodePlayer(const json& row) {
  Player p;
  p.id = str(row, 0);
  p.name = sanitizeName(str(row, 1));
  p.color = std::clamp(static_cast<int>(num(row, 2)), 0, 3);
  p.x = num(row, 3); p.y = num(row, 4); p.hp = num(row, 5); p.maxHp = num(row, 6, 100);
  p.xp = num(row, 7); p.level = static_cast<int>(num(row, 8, 1)); p.alive = flag(row, 9); p.speed = num(row, 10, 190);
  if (row.size() > 11 && row[11].is_object())
    for (const auto& [power, rank] : row[11].items()) if (rank.is_number()) p.powers[power] = rank.get<int>();
  if (row.size() > 12 && row[12].is_array())
    for (const auto& power : row[12]) if (power.is_string()) p.pendingPowers.push_back(power.get<std::string>());
  p.specialCharge = num(row, 13); p.coins = static_cast<int>(num(row, 14)); p.reviveProgress = num(row, 15);
  p.reviveBy = str(row, 16); p.reviving = str(row, 17);
  p.castCount = static_cast<int>(num(row, 18)); p.castAngle = num(row, 19); p.invulnerableFor = num(row, 20);
  p.orbitAngle = num(row, 21); p.rerolls = static_cast<int>(num(row, 22)); p.phoenix = static_cast<int>(num(row, 23));
  p.inputSeq = id(num(row, 24)); p.powerTimer = num(row, 25); p.connected = row.size() > 26 ? flag(row, 26) : true;
  p.dashFor = num(row, 27); p.dashCooldown = num(row, 28); p.dashX = num(row, 29); p.dashY = num(row, 30);
  p.moveX = num(row, 31); p.moveY = num(row, 32); p.specialCooldown = num(row, 33); p.motionId = id(num(row, 34));
  if (row.size() > 35 && row[35].is_object()) {
    const json& st = row[35];
    p.stats.damage = field(st, "damage"); p.stats.kills = static_cast<int>(field(st, "kills"));
    p.stats.revives = static_cast<int>(field(st, "revives")); p.stats.taken = field(st, "taken");
    if (const auto by = st.find("by"); by != st.end() && by->is_object())
      for (const auto& [kind, amount] : by->items()) if (amount.is_number()) p.stats.by[kind] = amount.get<double>();
  }
  if (row.size() > 36 && row[36].is_array() && row[36].size() >= 2) p.familiar = Familiar{num(row[36], 0), num(row[36], 1)};
  p.specialVariant = static_cast<int>(num(row, 37)); p.shopProgress = num(row, 38);
  return p;
}

bool decode(const json& c, GameState& s, DecodeStats* stats) {
  if (!c.is_object()) return false;
  const auto players = c.find("p");
  if (players == c.end() || !players->is_array()) return false;
  s.campaign = text(c, "ca").empty() ? "classic" : text(c, "ca");
  if (const auto cu = c.find("cu"); cu != c.end() && cu->is_array())
    for (const auto& curse : *cu) if (curse.is_string()) s.curses.push_back(curse.get<std::string>());
  s.loop = static_cast<int>(field(c, "lp"));
  s.bloodPact = c.contains("bp") && truthy(c["bp"]);
  if (const auto en = c.find("en"); en != c.end() && en->is_array()) {
    Encounter e;
    if (const char* kind = lookup(kEncounterKinds, num(*en, 0, -1))) e.kind = kind;
    e.x = num(*en, 1); e.y = num(*en, 2); e.radius = num(*en, 3); e.progress = num(*en, 4); e.ttl = num(*en, 5); e.status = str(*en, 6);
    if (en->size() > 7 && (*en)[7].is_array())
      for (const auto& buyer : (*en)[7]) if (buyer.is_string()) e.buyers.push_back(buyer.get<std::string>());
    s.encounter = std::move(e);
  }
  if (const auto a = c.find("a"); a != c.end() && a->is_array()) {
    Altar altar;
    altar.x = num(*a, 0); altar.y = num(*a, 1); altar.radius = num(*a, 2); altar.progress = num(*a, 3); altar.ttl = num(*a, 4);
    altar.status = str(*a, 5);
    s.altar = altar;
  }
  s.time = field(c, "t"); s.over = c.contains("o") && truthy(c["o"]); s.victory = c.contains("v") && truthy(c["v"]);
  s.phase = static_cast<int>(field(c, "ph")); s.phaseTime = field(c, "pt");
  const char* status = lookup(kStatuses, field(c, "st"));
  s.phaseStatus = status ? status : "horde";
  s.transitionTime = field(c, "tt");

  for (const auto& row : *players) {
    if (!row.is_array()) continue;
    Player p = decodePlayer(row);
    if (!p.id.empty()) s.players[p.id] = std::move(p);
  }
  for (const auto& row : list(c, "e")) {
    const char* type = row.is_array() ? lookup(kEnemyTypes, num(row, 1, -1)) : nullptr;
    if (!type) continue;
    Enemy e;
    e.id = id(num(row, 0)); e.type = type; e.x = num(row, 2); e.y = num(row, 3); e.hp = num(row, 4); e.maxHp = num(row, 5);
    const int f = static_cast<int>(num(row, 6));
    e.boss = f & FBoss; e.elite = f & FElite; e.thief = f & FThief;
    e.slowFor = f & FSlowed ? 1 : 0; e.windup = f & FWindup ? 1 : 0; e.fuse = f & FFuse ? 1 : 0; e.dashWarn = f & FDashWarn ? 1 : 0;
    e.rootFor = f & FRoot ? 1 : 0; e.freezeFor = f & FFreeze ? 1 : 0; e.burningFor = f & FBurning ? 1 : 0;
    e.stage = static_cast<int>(num(row, 7, 1)); e.dashAngle = num(row, 8);
    add(s.enemies, std::move(e), stats);
  }
  for (const auto& row : list(c, "s")) {
    if (!row.is_array()) continue;
    Shot shot;
    shot.x = num(row, 0); shot.y = num(row, 1); shot.vx = num(row, 2); shot.vy = num(row, 3); shot.color = static_cast<int>(num(row, 4));
    const int f = static_cast<int>(num(row, 5));
    shot.special = f & SSpecial; shot.shard = f & SShard; shot.returning = f & SReturning; shot.fullmoon = f & SFullmoon;
    add(s.shots, std::move(shot), stats);
  }
  for (const auto& row : list(c, "es")) {
    if (!row.is_array()) continue;
    EnemyShot shot;
    shot.x = num(row, 0); shot.y = num(row, 1); shot.vx = num(row, 2); shot.vy = num(row, 3);
    if (const char* sprite = lookup(kSprites, num(row, 4, -1))) shot.sprite = sprite;
    add(s.enemyShots, std::move(shot), stats);
  }
  for (const auto& row : list(c, "g")) {
    const char* type = row.is_array() ? lookup(kDropTypes, num(row, 3, -1)) : nullptr;
    if (!type) continue;
    Drop drop;
    drop.id = id(num(row, 0)); drop.x = num(row, 1); drop.y = num(row, 2); drop.type = type; drop.value = num(row, 4); drop.ttl = 24;
    add(s.gems, std::move(drop), stats);
  }
  for (const auto& row : list(c, "h")) {
    if (!row.is_array()) continue;
    Hazard h;
    h.x = num(row, 0); h.y = num(row, 1); h.radius = num(row, 2); h.warning = num(row, 3); h.warn0 = num(row, 4, 1.3); h.fired = flag(row, 5);
    add(s.hazards, h, stats);
  }
  for (const auto& row : list(c, "r")) {
    if (!row.is_array()) continue;
    Rune r;
    r.id = id(num(row, 0)); r.x = num(row, 1); r.y = num(row, 2); r.color = static_cast<int>(num(row, 3));
    r.arm = flag(row, 4) ? 0 : 1; r.radius = num(row, 5);
    add(s.runes, std::move(r), stats);
  }
  for (const auto& row : list(c, "z")) {
    if (!row.is_array()) continue;
    Zone z;
    z.id = id(num(row, 0)); z.x = num(row, 1); z.y = num(row, 2); z.radius = num(row, 3); z.color = static_cast<int>(num(row, 4));
    z.kind = str(row, 5).empty() ? "burn" : str(row, 5); z.warning = num(row, 6); z.ttl = 1;
    add(s.zones, std::move(z), stats);
  }
  for (const auto& o : list(c, "ev")) {
    if (!o.is_object()) continue;
    Event e;
    e.id = id(field(o, "id")); e.kind = text(o, "kind"); e.t = field(o, "t");
    e.x = field(o, "x"); e.y = field(o, "y"); e.r = field(o, "r"); e.a = field(o, "a");
    e.color = static_cast<int>(field(o, "color")); e.stage = static_cast<int>(field(o, "stage"));
    // The JS events carry their detail under different keys; the C++ Event has one slot for each.
    e.variant = static_cast<int>(std::max({field(o, "variant"), field(o, "team"), field(o, "evolved")}));
    for (const char* key : {"reaction", "signal", "type", "encounter"})
      if (std::string t = text(o, key); !t.empty()) { e.text = std::move(t); break; }
    if (const auto points = o.find("points"); points != o.end() && points->is_array())
      for (const auto& v : *points) if (!v.is_number() || !e.points.push_back(v.get<double>())) break;
    e.player = text(o, "player");
    e.name = sanitizeName(text(o, "name"));
    add(s.events, std::move(e), stats);
  }
  return true;
}

} // namespace

bool decodeSnapshot(const json& compact, GameState& out, DecodeStats* stats) {
  try {
    return decode(compact, out, stats);
  } catch (const json::exception&) {
    return false;
  }
}

std::string sanitizeName(std::string_view in) {
  std::string out;
  int codepoints = 0;
  for (std::size_t i = 0; i < in.size() && codepoints < 16;) {
    const auto lead = static_cast<unsigned char>(in[i]);
    const std::size_t len = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
    if (i + len > in.size()) break; // cut in the middle of a character
    if (!(len == 1 && (lead < 0x20 || lead == 0x7F))) { out.append(in.substr(i, len)); ++codepoints; }
    i += len;
  }
  return out;
}

} // namespace arcana::online
```

- [ ] **Step 4: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `online_protocol: ok` e todos os testes passam. Se um campo divergir, a mensagem mostra `[nome-do-caso] campo: valor != esperado`; corrija o decodificador, não a fixture.

- [ ] **Step 5: Commit**

```bash
git add platforms/net/protocol.hpp platforms/net/protocol.cpp tests/online_protocol_tests.cpp CMakeLists.txt
git commit -m "feat(online): decode meu-game snapshots into GameState

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 6: Mensagens cliente/servidor

**Files:**
- Modify: `platforms/net/protocol.hpp` (acrescentar antes de `} // namespace arcana::online`)
- Modify: `platforms/net/protocol.cpp` (acrescentar antes de `} // namespace arcana::online`)
- Create: `tests/online_messages_tests.cpp`
- Modify: `CMakeLists.txt` (uma linha)

**Interfaces:**
- Produces:
  - `struct EntryRequest { std::string action{"create"}, room, name{"Arcanista"}, visibility{"closed"}, campaign{"quick"}; int color{}; std::vector<std::string> curses; MetaRanks meta; Loadout loadout; };`
  - `std::string encodeEntry(const EntryRequest&); encodeResume(room, token); encodeInput(double x, double y, std::uint64_t seq); encodePing(double t); encodeSimple(std::string_view type); encodeChoosePower(const std::string&); encodeDash(double x, double y); encodeSignal(const std::string& kind, std::optional<Vec2> at); encodeSelectCharacter(int color);`
  - `struct LobbyPlayer { std::string id, name; int color{}; bool connected{true}; };`
  - `struct LobbyInfo { int count{}; std::string visibility, hostId, campaign; std::vector<std::string> curses; std::vector<LobbyPlayer> players; bool running{}; };`
  - `struct RoomInfo { std::string code, host, campaign; int count{}; bool running{}; std::vector<std::string> curses; };`
  - `struct RoomsInfo { std::vector<RoomInfo> rooms; int used{}, max{}, version{1}; };`
  - `struct Joined { std::string room, playerId, token, visibility; int color{}; bool resumed{}; };`
  - `struct ServerError { std::string code, message; std::vector<LobbyPlayer> players; };`
  - `enum class ServerKind { Unknown, Pong, Joined, Lobby, Error, Start, State, Rooms, BadSnapshot };`
  - `struct ServerMessage { ServerKind kind; double pongT; Joined joined; LobbyInfo lobby; ServerError error; RoomsInfo rooms; std::shared_ptr<GameState> state; };`
  - `ServerMessage parseServerMessage(std::string_view text, DecodeStats* stats = nullptr);`

- [ ] **Step 1: Escrever o teste que falha** — `tests/online_messages_tests.cpp`:

```cpp
// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdio>

using namespace arcana;
using namespace arcana::online;
using nlohmann::json;

int main() {
  // Client -> server: the same fields src/net.js sends, plus the protocol version.
  {
    EntryRequest r;
    r.action = "join"; r.room = "ABC234"; r.name = "Ana"; r.color = 2; r.campaign = "classic"; r.curses = {"swarm"};
    r.meta.rank["vigor"] = 3; r.loadout = {"orbit", 1};
    const json j = json::parse(encodeEntry(r));
    assert(j["type"] == "join" && j["v"] == kProtocolVersion && j["room"] == "ABC234" && j["name"] == "Ana");
    assert(j["color"] == 2 && j["campaign"] == "classic" && j["curses"][0] == "swarm" && j["visibility"] == "closed");
    assert(j["meta"]["vigor"] == 3 && j["loadout"]["weapon"] == "orbit" && j["loadout"]["special"] == 1);
    r.action = "create";
    assert(!json::parse(encodeEntry(r)).contains("room"));
    r.name = std::string("Ana\xFF");  // invalid UTF-8 never throws
    assert(!encodeEntry(r).empty());
  }
  {
    const json resume = json::parse(encodeResume("ABC234", "tok"));
    assert(resume["type"] == "resume" && resume["v"] == kProtocolVersion && resume["token"] == "tok");
    const json input = json::parse(encodeInput(0.5, -1, 7));
    assert(input["type"] == "input" && input["x"] == 0.5 && input["y"] == -1 && input["seq"] == 7);
    assert(json::parse(encodeSimple("start"))["type"] == "start");
    assert(json::parse(encodeChoosePower("arcane"))["power"] == "arcane");
    assert(json::parse(encodeDash(0, 1))["y"] == 1);
    const json look = json::parse(encodeSignal("look", Vec2{10.4, 20.6}));
    assert(look["signal"] == "look" && look["x"] == 10 && look["y"] == 21);
    assert(!json::parse(encodeSignal("help", std::nullopt)).contains("x"));
    assert(json::parse(encodeSelectCharacter(3))["color"] == 3);
    assert(json::parse(encodePing(12.5))["t"] == 12.5);
  }
  // Server -> client.
  {
    const auto pong = parseServerMessage(R"({"type":"pong","t":42})");
    assert(pong.kind == ServerKind::Pong && pong.pongT == 42);
    const auto joined = parseServerMessage(R"({"type":"joined","room":"ABC234","playerId":"u1","token":"t1","color":1,"count":1,"visibility":"open","resumed":true})");
    assert(joined.kind == ServerKind::Joined && joined.joined.room == "ABC234" && joined.joined.playerId == "u1");
    assert(joined.joined.token == "t1" && joined.joined.color == 1 && joined.joined.resumed && joined.joined.visibility == "open");
    const auto lobby = parseServerMessage(R"({"type":"lobby","count":2,"visibility":"open","hostId":"u1","running":false,"campaign":"quick","curses":["tyrant"],
      "players":[{"id":"u1","name":"Ana","color":0,"connected":true},{"id":"u2","name":"Beto","color":3,"connected":false}]})");
    assert(lobby.kind == ServerKind::Lobby && lobby.lobby.hostId == "u1" && lobby.lobby.players.size() == 2);
    assert(lobby.lobby.players[1].color == 3 && !lobby.lobby.players[1].connected && lobby.lobby.curses[0] == "tyrant");
    const auto error = parseServerMessage(R"({"type":"error","code":"CHARACTER_TAKEN","message":"Em uso","players":[{"id":"u1","name":"Ana","color":0}]})");
    assert(error.kind == ServerKind::Error && error.error.code == "CHARACTER_TAKEN" && error.error.players.size() == 1);
    const auto rooms = parseServerMessage(R"({"type":"rooms","rooms":[{"code":"XYZ789","count":2,"running":true,"host":"Ana","campaign":"classic","curses":[]}],
      "capacity":{"used":1,"max":3},"v":1})");
    assert(rooms.kind == ServerKind::Rooms && rooms.rooms.rooms.size() == 1 && rooms.rooms.rooms[0].code == "XYZ789");
    assert(rooms.rooms.rooms[0].running && rooms.rooms.used == 1 && rooms.rooms.max == 3 && rooms.rooms.version == 1);
    assert(parseServerMessage(R"({"type":"rooms","rooms":[],"capacity":{"used":0,"max":3}})").rooms.version == 1); // pre-versioning server
    const auto state = parseServerMessage(R"({"type":"state","state":{"t":2.5,"p":[["u1","Ana",0,1,2,100,100,0,1,true,190,{},null]],
      "e":[],"s":[],"es":[],"g":[],"h":[],"r":[],"z":[],"ev":[]}})");
    assert(state.kind == ServerKind::State && state.state && state.state->time == 2.5 && state.state->players.at("u1").x == 1);
    assert(parseServerMessage(R"({"type":"start","state":[]})").kind == ServerKind::BadSnapshot);
    assert(parseServerMessage("{nope").kind == ServerKind::Unknown);
    assert(parseServerMessage(R"({"type":"lobby","players":"x"})").kind == ServerKind::Unknown);
  }
  std::puts("online_messages: ok");
  return 0;
}
```

No `CMakeLists.txt`: `    arcana_online_test(online_messages arcana_online)`.

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL de compilação — `'EntryRequest' was not declared in this scope`.

- [ ] **Step 3: Implementar** — em `platforms/net/protocol.hpp`, acrescente os includes `<memory>`, `<optional>`, `<vector>` no topo e, antes de `} // namespace arcana::online`:

```cpp
// ---- Messages (server/server.js) -------------------------------------------------------------

// create / join, with the fields src/net.js sends.
struct EntryRequest {
  std::string action{"create"};  // "create" or "join"
  std::string room;              // join only
  std::string name{"Arcanista"}, visibility{"closed"}, campaign{"quick"};
  int color{};
  std::vector<std::string> curses;
  MetaRanks meta;                // Grimório ranks; the server sanitizes them
  Loadout loadout;
};

std::string encodeEntry(const EntryRequest& request);
std::string encodeResume(const std::string& room, const std::string& token);
std::string encodeInput(double x, double y, std::uint64_t seq);
std::string encodePing(double t);
std::string encodeSimple(std::string_view type);  // start, ready, leave, reroll, special, listRooms
std::string encodeChoosePower(const std::string& power);
std::string encodeDash(double x, double y);
std::string encodeSignal(const std::string& kind, std::optional<Vec2> at);
std::string encodeSelectCharacter(int color);

struct LobbyPlayer { std::string id, name; int color{}; bool connected{true}; };
struct LobbyInfo {
  int count{};
  std::string visibility, hostId, campaign;
  std::vector<std::string> curses;
  std::vector<LobbyPlayer> players;
  bool running{};
};
struct RoomInfo { std::string code, host, campaign; int count{}; bool running{}; std::vector<std::string> curses; };
struct RoomsInfo { std::vector<RoomInfo> rooms; int used{}, max{}, version{1}; };
struct Joined { std::string room, playerId, token, visibility; int color{}; bool resumed{}; };
struct ServerError { std::string code, message; std::vector<LobbyPlayer> players; };

enum class ServerKind { Unknown, Pong, Joined, Lobby, Error, Start, State, Rooms, BadSnapshot };
struct ServerMessage {
  ServerKind kind{ServerKind::Unknown};
  double pongT{};
  Joined joined;
  LobbyInfo lobby;
  ServerError error;
  RoomsInfo rooms;
  std::shared_ptr<GameState> state;  // Start / State (heap: a GameState is ~200 KB)
};

// Never throws: malformed text is Unknown, a start/state with a bad snapshot is BadSnapshot.
ServerMessage parseServerMessage(std::string_view text, DecodeStats* stats = nullptr);
```

Em `platforms/net/protocol.cpp`, antes de `} // namespace arcana::online`:

```cpp
namespace {
// Names typed on a keyboard may carry bytes that are not UTF-8: replace them instead of throwing.
std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

std::vector<LobbyPlayer> lobbyPlayers(const json& o) {
  std::vector<LobbyPlayer> out;
  for (const auto& p : list(o, "players")) {
    if (!p.is_object()) continue;
    out.push_back({text(p, "id"), sanitizeName(text(p, "name")), std::clamp(static_cast<int>(field(p, "color")), 0, 3),
                   !p.contains("connected") || truthy(p["connected"])});
  }
  return out;
}
std::vector<std::string> strings(const json& o, const char* key) {
  std::vector<std::string> out;
  for (const auto& v : list(o, key)) if (v.is_string()) out.push_back(v.get<std::string>());
  return out;
}
} // namespace

std::string encodeEntry(const EntryRequest& r) {
  json j = {{"type", r.action}, {"v", kProtocolVersion}, {"name", r.name}, {"visibility", r.visibility},
            {"color", r.color}, {"campaign", r.campaign}, {"curses", r.curses}};
  if (r.action == "join") j["room"] = r.room;
  json meta = json::object();
  for (const auto& [upgrade, rank] : r.meta.rank) meta[upgrade] = rank;
  j["meta"] = std::move(meta);
  j["loadout"] = {{"weapon", r.loadout.weapon}, {"special", r.loadout.special}};
  return dump(j);
}
std::string encodeResume(const std::string& room, const std::string& token) {
  return dump({{"type", "resume"}, {"v", kProtocolVersion}, {"room", room}, {"token", token}});
}
std::string encodeInput(double x, double y, std::uint64_t seq) { return dump({{"type", "input"}, {"x", x}, {"y", y}, {"seq", seq}}); }
std::string encodePing(double t) { return dump({{"type", "ping"}, {"t", t}}); }
std::string encodeSimple(std::string_view type) { return dump({{"type", std::string(type)}}); }
std::string encodeChoosePower(const std::string& power) { return dump({{"type", "choosePower"}, {"power", power}}); }
std::string encodeDash(double x, double y) { return dump({{"type", "dash"}, {"x", x}, {"y", y}}); }
std::string encodeSignal(const std::string& kind, std::optional<Vec2> at) {
  json j = {{"type", "signal"}, {"signal", kind}};
  if (at) { j["x"] = std::lround(at->x); j["y"] = std::lround(at->y); }
  return dump(j);
}
std::string encodeSelectCharacter(int color) { return dump({{"type", "selectCharacter"}, {"color", color}}); }

ServerMessage parseServerMessage(std::string_view raw, DecodeStats* stats) {
  ServerMessage m;
  const json j = json::parse(raw, nullptr, false);
  if (!j.is_object()) return m;
  try {
    const std::string type = text(j, "type");
    if (type == "pong") {
      m.kind = ServerKind::Pong;
      m.pongT = field(j, "t");
    } else if (type == "joined") {
      m.kind = ServerKind::Joined;
      m.joined = {text(j, "room"), text(j, "playerId"), text(j, "token"), text(j, "visibility"),
                  std::clamp(static_cast<int>(field(j, "color")), 0, 3), j.contains("resumed") && truthy(j["resumed"])};
    } else if (type == "lobby") {
      if (j.contains("players") && !j["players"].is_array()) return m;
      m.kind = ServerKind::Lobby;
      m.lobby = {static_cast<int>(field(j, "count")), text(j, "visibility"), text(j, "hostId"), text(j, "campaign"),
                 strings(j, "curses"), lobbyPlayers(j), j.contains("running") && truthy(j["running"])};
    } else if (type == "error") {
      m.kind = ServerKind::Error;
      m.error = {text(j, "code"), text(j, "message"), lobbyPlayers(j)};
      if (m.error.message.empty()) m.error.message = "Erro do servidor.";
    } else if (type == "rooms") {
      m.kind = ServerKind::Rooms;
      for (const auto& r : list(j, "rooms")) {
        if (!r.is_object()) continue;
        m.rooms.rooms.push_back({text(r, "code"), sanitizeName(text(r, "host")), text(r, "campaign"), static_cast<int>(field(r, "count")),
                                 r.contains("running") && truthy(r["running"]), strings(r, "curses")});
      }
      if (const auto cap = j.find("capacity"); cap != j.end() && cap->is_object()) {
        m.rooms.used = static_cast<int>(field(*cap, "used"));
        m.rooms.max = static_cast<int>(field(*cap, "max"));
      }
      m.rooms.version = static_cast<int>(field(j, "v", 1));
    } else if (type == "start" || type == "state") {
      auto state = std::make_shared<GameState>();
      const auto body = j.find("state");
      if (body == j.end() || !decodeSnapshot(*body, *state, stats)) { m.kind = ServerKind::BadSnapshot; return m; }
      m.kind = type == "start" ? ServerKind::Start : ServerKind::State;
      m.state = std::move(state);
    }
  } catch (const json::exception&) {
    m = ServerMessage{};
  }
  return m;
}
```

(`list`, `text`, `field`, `truthy` já existem no namespace anônimo da Task 5 — este bloco fica no mesmo arquivo, depois dele.)

- [ ] **Step 4: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `online_messages: ok`; todos passam.

- [ ] **Step 5: Commit**

```bash
git add platforms/net/protocol.hpp platforms/net/protocol.cpp tests/online_messages_tests.cpp CMakeLists.txt
git commit -m "feat(online): client/server message encoding and parsing

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 7: Interpolação de snapshots

**Files:**
- Create: `platforms/net/interpolation.hpp`, `platforms/net/interpolation.cpp`
- Create: `tests/online_sync_tests.cpp`
- Modify: `CMakeLists.txt` (uma linha)

**Interfaces:**
- Produces: `struct Snapshot { double t{}; double at{}; std::shared_ptr<const GameState> state; };` e `class Interpolator { void apply(const std::deque<Snapshot>& snapshots, double renderT, GameState& out); };`

- [ ] **Step 1: Escrever o teste que falha** — `tests/online_sync_tests.cpp` (a Task 8 acrescenta mais blocos a este arquivo):

```cpp
// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "interpolation.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>

using namespace arcana;
using namespace arcana::online;

namespace {
bool close(double a, double b) { return std::fabs(a - b) < 1e-9; }

std::shared_ptr<GameState> snapshot(double t, double px, double ex, double shotX) {
  auto s = std::make_shared<GameState>();
  s->time = t;
  Player p = createPlayer("u1", "Ana", 0);
  p.x = px; p.orbitAngle = 3.0;
  s->players["u1"] = p;
  Enemy e; e.id = 7; e.type = "slime"; e.x = ex;
  s->enemies.push_back(e);
  Shot shot; shot.x = shotX; shot.vx = 100;
  s->shots.push_back(shot);
  Hazard h; h.warning = 1;
  s->hazards.push_back(h);
  return s;
}
} // namespace

int main() {
  std::deque<Snapshot> snaps;
  snaps.push_back({1.0, 0, snapshot(1.0, 0, 100, 0)});
  snaps.push_back({1.1, 0, snapshot(1.1, 10, 200, 10)});
  auto out = std::make_unique<GameState>();
  Interpolator interp;

  // Halfway between the two snapshots.
  interp.apply(snaps, 1.05, *out);
  assert(close(out->players.at("u1").x, 5));
  assert(close(out->enemies[0].x, 150));
  assert(close(out->shots[0].x, 10 + 100 * (1.05 - 1.1)));   // shots come from the newer one, moved by velocity
  assert(close(out->hazards[0].warning, 1));                 // renderT behind the newest: no countdown yet
  assert(close(out->time, 1.1));                             // everything else from the newest snapshot

  // Past the newest snapshot: hold positions, extrapolate shots, count hazards down.
  interp.apply(snaps, 1.3, *out);
  assert(close(out->players.at("u1").x, 10));
  assert(close(out->shots[0].x, 10 + 100 * 0.2));
  assert(close(out->hazards[0].warning, 0.8));

  // Before the oldest snapshot: the oldest is shown.
  interp.apply(snaps, 0.5, *out);
  assert(close(out->enemies[0].x, 100));

  // Angles take the short way round (3.0 -> -3.0 crosses pi).
  auto wrap = snapshot(1.2, 10, 200, 10);
  wrap->players["u1"].orbitAngle = -3.0;
  snaps.push_back({1.2, 0, wrap});
  interp.apply(snaps, 1.15, *out);
  assert(std::fabs(out->players.at("u1").orbitAngle) > 3.0);

  // A new enemy (not in the older snapshot) appears where the server put it.
  auto spawn = snapshot(1.3, 10, 200, 10);
  Enemy fresh; fresh.id = 8; fresh.type = "bat"; fresh.x = 999;
  spawn->enemies.push_back(fresh);
  snaps.push_back({1.3, 0, spawn});
  interp.apply(snaps, 1.25, *out);
  assert(out->enemies.size() == 2 && close(out->enemies[1].x, 999));

  std::puts("online_sync: ok");
  return 0;
}
```

No `CMakeLists.txt`: `    arcana_online_test(online_sync arcana_online)`.

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL — `interpolation.hpp: No such file or directory`.

- [ ] **Step 3: Implementar** — `platforms/net/interpolation.hpp`:

```cpp
#pragma once

#include "arcana/game.hpp"

#include <deque>
#include <memory>
#include <unordered_map>

namespace arcana::online {

struct Snapshot {
  double t{};   // server time (seconds)
  double at{};  // local arrival (ms)
  std::shared_ptr<const GameState> state;
};

// Port of src/net.js interpolate(): `out` gets the newest snapshot, with players, enemies, drops and
// familiars blended by id between the two snapshots around `renderT` (server seconds), projectiles
// advanced by their velocity past the newer one and hazard warnings counted down.
class Interpolator {
public:
  void apply(const std::deque<Snapshot>& snapshots, double renderT, GameState& out);

private:
  std::unordered_map<std::uint64_t, Vec2> enemies_, gems_;  // reused lookups (no per-frame rehash growth)
};

} // namespace arcana::online
```

`platforms/net/interpolation.cpp`:

```cpp
#include "interpolation.hpp"

#include <algorithm>
#include <cmath>

namespace arcana::online {
namespace {
real lerp(real a, real b, real t) { return a + (b - a) * t; }
real lerpAngle(real a, real b, real t) {
  real delta = std::fmod(b - a, PI * 2);
  if (delta > PI) delta -= PI * 2;
  if (delta < -PI) delta += PI * 2;
  return a + delta * t;
}
} // namespace

void Interpolator::apply(const std::deque<Snapshot>& snaps, double renderT, GameState& out) {
  if (snaps.empty()) return;
  const Snapshot* a = &snaps.back();
  const Snapshot* b = a;
  for (std::size_t i = snaps.size() - 1; i > 0; --i) {
    if (snaps[i - 1].t <= renderT) { a = &snaps[i - 1]; b = &snaps[i]; break; }
    a = b = &snaps[i - 1];
  }
  const real alpha = b->t > a->t ? std::clamp((renderT - a->t) / (b->t - a->t), 0.0, 1.0) : 0.0;
  const real ahead = renderT - b->t;
  const GameState& latest = *snaps.back().state;
  const GameState& from = *a->state;
  const GameState& to = *b->state;

  out = latest;
  for (auto& [id, p] : out.players) {
    const auto pa = from.players.find(id);
    if (pa == from.players.end()) continue;
    const auto pbIt = to.players.find(id);
    const Player& pb = pbIt == to.players.end() ? latest.players.at(id) : pbIt->second;
    p.x = lerp(pa->second.x, pb.x, alpha);
    p.y = lerp(pa->second.y, pb.y, alpha);
    p.orbitAngle = lerpAngle(pa->second.orbitAngle, pb.orbitAngle, alpha);
    if (pa->second.familiar && pb.familiar && p.familiar) {
      p.familiar->x = lerp(pa->second.familiar->x, pb.familiar->x, alpha);
      p.familiar->y = lerp(pa->second.familiar->y, pb.familiar->y, alpha);
    }
  }
  enemies_.clear();
  for (const auto& e : from.enemies) enemies_[e.id] = {e.x, e.y};
  out.enemies = to.enemies;
  for (auto& e : out.enemies)
    if (const auto it = enemies_.find(e.id); it != enemies_.end()) { e.x = lerp(it->second.x, e.x, alpha); e.y = lerp(it->second.y, e.y, alpha); }
  gems_.clear();
  for (const auto& g : from.gems) gems_[g.id] = {g.x, g.y};
  out.gems = to.gems;
  for (auto& g : out.gems)
    if (const auto it = gems_.find(g.id); it != gems_.end()) { g.x = lerp(it->second.x, g.x, alpha); g.y = lerp(it->second.y, g.y, alpha); }
  out.shots = to.shots;
  for (auto& s : out.shots) { s.x += s.vx * ahead; s.y += s.vy * ahead; }
  out.enemyShots = to.enemyShots;
  for (auto& s : out.enemyShots) { s.x += s.vx * ahead; s.y += s.vy * ahead; }
  out.hazards = to.hazards;
  for (auto& h : out.hazards) h.warning -= std::max<real>(0, ahead);
  out.runes = to.runes;
  out.zones = to.zones;
}

} // namespace arcana::online
```

- [ ] **Step 4: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `online_sync: ok`.

- [ ] **Step 5: Commit**

```bash
git add platforms/net/interpolation.hpp platforms/net/interpolation.cpp tests/online_sync_tests.cpp CMakeLists.txt
git commit -m "feat(online): snapshot interpolation (port of net.js)

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 8: Predição do jogador local e envio de input

**Files:**
- Create: `platforms/net/prediction.hpp`, `platforms/net/prediction.cpp`
- Modify: `tests/online_sync_tests.cpp` (include + blocos novos antes do `std::puts`)

**Interfaces:**
- Consumes: `playerMovement()` (Task 4), `encodeInput()` (Task 6).
- Produces:
  - `Vec2 quantizeInput(Vec2 input);`
  - `class InputSender { std::optional<std::string> update(Vec2 input, double nowMs); std::uint64_t seq() const; static constexpr double kMinGapMs = 50, kKeepAliveMs = 100; };`
  - `class Predictor { void reset(); void reconcile(const Player& server, double nowMs, double rttMs); Vec2 step(const Player& server, double ageSeconds, Vec2 input, double dt, bool canMove, double nowMs); };`

- [ ] **Step 1: Escrever os testes que falham** — em `tests/online_sync_tests.cpp`, acrescente `#include "prediction.hpp"` e `#include <nlohmann/json.hpp>`, e antes de `std::puts("online_sync: ok");`:

```cpp
  // Input quantization: 1/32 steps, never longer than 1.
  {
    const Vec2 q = quantizeInput({0.7071, 0.7071});
    assert(std::hypot(q.x, q.y) <= 1.0 + 1e-12);
    assert(close(quantizeInput({0.5, -0.26}).x, 0.5) && close(quantizeInput({0.5, -0.26}).y, -8.0 / 32));
    assert(close(quantizeInput({3, 0}).x, 1));
  }
  // Input sender: sends on change (at most every 50 ms) and as a 100 ms keep-alive; seq counts changes.
  {
    InputSender sender;
    auto first = sender.update({1, 0}, 0);
    assert(first && nlohmann::json::parse(*first)["seq"] == 1);
    assert(!sender.update({1, 0}, 30));                     // nothing new, keep-alive not due
    auto keepAlive = sender.update({1, 0}, 100);
    assert(keepAlive && nlohmann::json::parse(*keepAlive)["seq"] == 1);
    assert(!sender.update({0, 1}, 110));                    // changed, but only 10 ms after the last send
    auto changed = sender.update({0, 1}, 150);
    assert(changed && nlohmann::json::parse(*changed)["seq"] == 2);
    int sent = 0;
    for (int frame = 0; frame < 60; ++frame)                // a stick that changes every frame for a second
      if (sender.update({std::sin(frame * 0.3), 0}, 1000 + frame * (1000.0 / 60))) ++sent;
    assert(sent <= 21);
  }
  // Prediction: walks with the server formula, reconciles softly, snaps on big errors and on dashes.
  {
    Player server = createPlayer("u1", "Ana", 0);
    Predictor predictor;
    Vec2 pos{};
    for (int i = 0; i < 60; ++i) pos = predictor.step(server, 0, {1, 0}, 1.0 / 60, true, i * (1000.0 / 60));
    assert(std::fabs(pos.x - server.speed) < 1e-6);         // one second at full speed
    // The server says we are 50 units behind what we predicted ~now (rtt 0): 35% of it is applied,
    // and a visual offset hides the jump.
    Player behind = server;
    behind.x = pos.x - 50;
    predictor.reconcile(behind, 1000, 0);
    const Vec2 after = predictor.step(server, 0, {0, 0}, 0, true, 1000);
    assert(std::fabs(after.x - pos.x) < 1e-6);               // no visible jump this frame
    // More than 220 units off: take the server position.
    Player far = server;
    far.x = pos.x + 500;
    predictor.reconcile(far, 1000, 0);
    assert(std::fabs(predictor.step(server, 0, {0, 0}, 0, true, 1000).x - far.x) < 1e-6);
    // A dash on the server (motionId changes) restarts from the server position.
    Player dashed = server;
    dashed.x = 42; dashed.motionId = 1;
    predictor.reconcile(dashed, 2000, 0);
    assert(std::fabs(predictor.step(dashed, 0, {0, 0}, 0, true, 2000).x - 42) < 1e-6);
    // Cannot move (dead, choosing a power...): show the server position.
    Player still = server;
    still.x = -5;
    assert(std::fabs(predictor.step(still, 0, {1, 0}, 1.0 / 60, false, 3000).x + 5) < 1e-6);
  }
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL — `prediction.hpp: No such file or directory`.

- [ ] **Step 3: Implementar** — `platforms/net/prediction.hpp`:

```cpp
#pragma once

#include "arcana/game.hpp"

#include <deque>
#include <optional>
#include <string>

namespace arcana::online {

// Stick input as it goes on the wire: 1/32 steps, length <= 1 (the prediction runs on exactly what
// the server receives).
Vec2 quantizeInput(Vec2 input);

// Rate-limits `input` messages: a change is sent at most every 50 ms and, without changes, a
// keep-alive goes every 100 ms (src/net.js). Keeps a player under the server's 50 messages/s.
class InputSender {
public:
  static constexpr double kMinGapMs = 50, kKeepAliveMs = 100;
  std::optional<std::string> update(Vec2 input, double nowMs);
  [[nodiscard]] std::uint64_t seq() const { return seq_; }

private:
  Vec2 last_{99, 99};
  double lastAt_{-1e9};
  std::uint64_t seq_{};
};

// Client-side prediction of the local player (src/net.js frame()/reconcile()).
class Predictor {
public:
  static constexpr double kHistoryMs = 1500, kSnapDistance = 220, kBlend = 0.35;
  void reset() { has_ = false; history_.clear(); offset_ = {}; motionId_ = 0; }
  // A snapshot arrived: compare with where we predicted ourselves about one round trip ago.
  void reconcile(const Player& server, double nowMs, double rttMs);
  // One render frame; returns where to draw the local player. `ageSeconds`: how old the newest
  // snapshot is (its dash has partly elapsed).
  Vec2 step(const Player& server, double ageSeconds, Vec2 input, double dt, bool canMove, double nowMs);

private:
  struct Sample { double at; Vec2 pos; };
  bool has_{};
  Vec2 predicted_{}, offset_{};
  std::uint64_t motionId_{};
  std::deque<Sample> history_;
};

} // namespace arcana::online
```

`platforms/net/prediction.cpp`:

```cpp
#include "prediction.hpp"
#include "protocol.hpp"

#include <algorithm>
#include <cmath>

namespace arcana::online {

Vec2 quantizeInput(Vec2 v) {
  auto q = [](real x) { return std::round(std::clamp<real>(x, -1, 1) * 32) / 32; };
  Vec2 out{q(v.x), q(v.y)};
  const real len = std::hypot(out.x, out.y);
  if (len > 1) { out.x /= len; out.y /= len; }
  return out;
}

std::optional<std::string> InputSender::update(Vec2 in, double now) {
  const bool changed = in.x != last_.x || in.y != last_.y;
  if (now - lastAt_ < (changed ? kMinGapMs : kKeepAliveMs)) return std::nullopt;
  if (changed) ++seq_;
  last_ = in;
  lastAt_ = now;
  return encodeInput(in.x, in.y, seq_);
}

void Predictor::reconcile(const Player& me, double now, double rtt) {
  if (!has_) return;
  if (motionId_ != me.motionId) {
    motionId_ = me.motionId;
    predicted_ = {me.x, me.y};
    offset_ = {};
    history_.clear();
    return;
  }
  if (history_.empty()) return;
  const double target = now - rtt;
  const Sample* past = &history_.front();
  for (const auto& s : history_) { if (s.at > target) break; past = &s; }
  const real ex = me.x - past->pos.x, ey = me.y - past->pos.y;
  if (std::hypot(ex, ey) > kSnapDistance) {
    offset_ = {};
    predicted_ = {me.x, me.y};
    history_.clear();
    return;
  }
  predicted_.x += ex * kBlend; predicted_.y += ey * kBlend;
  for (auto& s : history_) { s.pos.x += ex * kBlend; s.pos.y += ey * kBlend; }
  offset_.x -= ex * kBlend; offset_.y -= ey * kBlend;
}

Vec2 Predictor::step(const Player& server, double age, Vec2 input, double dt, bool canMove, double now) {
  if (!canMove || !has_) {
    predicted_ = {server.x, server.y};
    offset_ = {};
    history_.clear();
    if (!has_) motionId_ = server.motionId;
    has_ = true;
  }
  if (canMove) {
    Player p = server;
    p.input.x = input.x; p.input.y = input.y;
    p.dashFor = std::max<real>(0, server.dashFor - age);
    const Vec2 d = playerMovement(p, dt);
    predicted_.x += d.x; predicted_.y += d.y;
    history_.push_back({now, predicted_});
    while (!history_.empty() && history_.front().at < now - kHistoryMs) history_.pop_front();
  }
  const real decay = std::min<real>(1, dt * 10);
  offset_.x -= offset_.x * decay; offset_.y -= offset_.y * decay;
  return {predicted_.x + offset_.x, predicted_.y + offset_.y};
}

} // namespace arcana::online
```

- [ ] **Step 4: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `online_sync: ok`.

- [ ] **Step 5: Commit**

```bash
git add platforms/net/prediction.hpp platforms/net/prediction.cpp tests/online_sync_tests.cpp
git commit -m "feat(online): local player prediction and rate-limited input

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 9: `WsTransport` (IXWebSocket) e o transporte falso dos testes

**Files:**
- Create: `platforms/net/ws_transport.hpp`, `platforms/net/ws_transport.cpp`
- Create: `tests/fake_transport.hpp`
- Modify: `tests/online_transport_tests.cpp` (um bloco)

**Interfaces:**
- Consumes: `Transport` (Task 3).
- Produces:
  - `class WsTransport final : public Transport { explicit WsTransport(std::string caFile); }` — `caFile` vazio usa o repositório do sistema ("SYSTEM" do IXWebSocket).
  - `tests/fake_transport.hpp`: `struct FakeWire { std::string url; std::vector<std::string> sent; std::vector<TransportEvent> pending; bool closed{}; }`, `class FakeTransport`, `struct FakeNet { std::vector<std::shared_ptr<FakeWire>> wires; TransportFactory factory(); }` e helpers `open(wire)`, `message(wire, text)`, `closeWith(wire, code, reason)`, `error(wire, tls)`.

- [ ] **Step 1: Escrever o teste que falha** — em `tests/online_transport_tests.cpp`, acrescente `#include "ws_transport.hpp"` e `#include <chrono>`, `#include <thread>`, e antes do `std::puts`:

```cpp
  // A connection nobody answers ends in one Error event, delivered through poll() (no reconnect loop).
  {
    WsTransport transport("");
    transport.open("ws://127.0.0.1:1/");
    std::vector<TransportEvent> events;
    for (int i = 0; i < 100 && events.empty(); ++i) {
      transport.poll(events);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(events.size() == 1 && events[0].type == TransportEvent::Type::Error && !events[0].tls);
    transport.close();
  }
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL — `ws_transport.hpp: No such file or directory`.

- [ ] **Step 3: Implementar** — `platforms/net/ws_transport.hpp`:

```cpp
#pragma once

#include "transport.hpp"

#include <memory>
#include <string>

namespace arcana::online {

// WebSocket over TLS (Mbed TLS) with permessage-deflate, on IXWebSocket's own thread. Events are
// queued under a mutex and handed out by poll(). Never reconnects by itself (the Session decides).
class WsTransport final : public Transport {
public:
  // `caFile`: PEM bundle used to verify the server (assets/cacert.pem); empty = system store.
  explicit WsTransport(std::string caFile);
  ~WsTransport() override;
  void open(const std::string& url) override;
  void send(std::string text) override;
  void close() override;
  void poll(std::vector<TransportEvent>& out) override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string caFile_;
};

} // namespace arcana::online
```

`platforms/net/ws_transport.cpp`:

```cpp
#include "ws_transport.hpp"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace arcana::online {
namespace {
bool looksLikeTls(std::string reason) {
  std::transform(reason.begin(), reason.end(), reason.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const char* word : {"certificate", "x509", "handshake", "tls", "ssl"})
    if (reason.find(word) != std::string::npos) return true;
  return false;
}
} // namespace

struct WsTransport::Impl {
  ix::WebSocket socket;
  std::mutex mutex;
  std::vector<TransportEvent> queue;
};

WsTransport::WsTransport(std::string caFile) : impl_(std::make_unique<Impl>()), caFile_(std::move(caFile)) {
  static const bool netReady = ix::initNetSystem(); // WSAStartup on Windows, once per process
  (void)netReady;
}

WsTransport::~WsTransport() { close(); }

void WsTransport::open(const std::string& url) {
  auto& s = impl_->socket;
  s.setUrl(url);
  ix::SocketTLSOptions tls;
  tls.caFile = caFile_.empty() ? "SYSTEM" : caFile_;
  s.setTLSOptions(tls);
  s.disableAutomaticReconnection();
  s.setHandshakeTimeout(10);
  s.setPerMessageDeflateOptions(ix::WebSocketPerMessageDeflateOptions(true));
  s.setOnMessageCallback([impl = impl_.get()](const ix::WebSocketMessagePtr& msg) {
    TransportEvent e;
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: e.type = TransportEvent::Type::Open; break;
      case ix::WebSocketMessageType::Message: e.type = TransportEvent::Type::Message; e.text = msg->str; break;
      case ix::WebSocketMessageType::Close:
        e.type = TransportEvent::Type::Close; e.code = msg->closeInfo.code; e.text = msg->closeInfo.reason; break;
      case ix::WebSocketMessageType::Error:
        e.type = TransportEvent::Type::Error; e.text = msg->errorInfo.reason; e.tls = looksLikeTls(msg->errorInfo.reason); break;
      default: return; // ping, pong, fragments
    }
    std::lock_guard lock(impl->mutex);
    impl->queue.push_back(std::move(e));
  });
  s.start();
}

void WsTransport::send(std::string text) { impl_->socket.sendText(text); }

void WsTransport::close() {
  impl_->socket.stop(); // joins IXWebSocket's thread: no callback runs after this
  std::lock_guard lock(impl_->mutex);
  impl_->queue.clear();
}

void WsTransport::poll(std::vector<TransportEvent>& out) {
  std::lock_guard lock(impl_->mutex);
  for (auto& e : impl_->queue) out.push_back(std::move(e));
  impl_->queue.clear();
}

} // namespace arcana::online
```

`tests/fake_transport.hpp`:

```cpp
#pragma once
// In-memory Transport for the online tests: the test plays the server by pushing events into a
// wire and reading what the client sent.
#include "transport.hpp"

#include <memory>
#include <string>
#include <vector>

namespace arcana::online::testing {

struct FakeWire {
  std::string url;
  std::vector<std::string> sent;
  std::vector<TransportEvent> pending;
  bool closed{};
};

class FakeTransport final : public Transport {
public:
  explicit FakeTransport(std::shared_ptr<FakeWire> wire) : wire_(std::move(wire)) {}
  void open(const std::string& url) override { wire_->url = url; }
  void send(std::string text) override { wire_->sent.push_back(std::move(text)); }
  void close() override { wire_->closed = true; }
  void poll(std::vector<TransportEvent>& out) override {
    out.insert(out.end(), wire_->pending.begin(), wire_->pending.end());
    wire_->pending.clear();
  }

private:
  std::shared_ptr<FakeWire> wire_;
};

// Every connection the code under test opens gets its own wire, in order.
struct FakeNet {
  std::vector<std::shared_ptr<FakeWire>> wires;
  TransportFactory factory() {
    return [this] {
      wires.push_back(std::make_shared<FakeWire>());
      return std::make_unique<FakeTransport>(wires.back());
    };
  }
};

inline void open(FakeWire& w) { w.pending.push_back({TransportEvent::Type::Open, {}, 0, false}); }
inline void message(FakeWire& w, std::string text) { w.pending.push_back({TransportEvent::Type::Message, std::move(text), 0, false}); }
inline void closeWith(FakeWire& w, int code, std::string reason = {}) { w.pending.push_back({TransportEvent::Type::Close, std::move(reason), code, false}); }
inline void error(FakeWire& w, bool tls = false) { w.pending.push_back({TransportEvent::Type::Error, "falhou", 0, tls}); }

} // namespace arcana::online::testing
```

- [ ] **Step 4: Rodar os testes e o build Windows**

Run: `tools/docker/build-all.sh host windows`
Expected: `online_transport: ok`; windows ok.

- [ ] **Step 5: Commit**

```bash
git add platforms/net/ws_transport.hpp platforms/net/ws_transport.cpp tests/fake_transport.hpp tests/online_transport_tests.cpp
git commit -m "feat(online): IXWebSocket transport and an in-memory fake for tests

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 10: `Session` (lobby, snapshots, reconexão)

**Files:**
- Create: `platforms/net/session.hpp`, `platforms/net/session.cpp`
- Create: `tests/online_session_tests.cpp`
- Modify: `CMakeLists.txt` (uma linha)

**Interfaces:**
- Consumes: Tasks 5–9.
- Produces:
  - `enum class SessionStatus { Connecting, Lobby, Playing, Reconnecting, Closed };`
  - `struct SessionConfig { std::string url; EntryRequest entry; TransportFactory transport; };`
  - `class Session` com `start(nowMs)`, `update(nowMs)`, `frame(nowMs, dt, Vec2 input, GameState& out) -> bool`, ações `selectCharacter(int)`, `startMatch()`, `choosePower(id)`, `reroll()`, `special()`, `dash(Vec2)`, `signal(kind, optional<Vec2>)`, `leave()`, e leitura `status()`, `playerId()`, `room()`, `color()`, `lobby()`, `isHost()`, `rttMs()`, `over()`, `failure()`, `droppedSnapshots()`, `takeNotice()`.

- [ ] **Step 1: Escrever o teste que falha** — `tests/online_session_tests.cpp`:

```cpp
// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "fake_transport.hpp"
#include "session.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

using namespace arcana;
using namespace arcana::online;
using namespace arcana::online::testing;
using nlohmann::json;

namespace {
// A server snapshot with one player at (x, 0); `type` is "start" or "state".
std::string snapshot(const char* type, double t, double x, bool over = false, const char* id = "u1") {
  json player = json::array({id, "Ana", 0, x, 0, 100, 100, 0, 1, true, 190, json::object(), nullptr, 0, 0, 0, nullptr, nullptr, 0, 0, 0, 0, 1, 0, 0, 0,
                             true, 0, 0, 0, 1, 0, 1, 0, 0, {{"damage", 0}, {"kills", 0}, {"revives", 0}, {"taken", 0}}, nullptr, 0, 0});
  json state = {{"t", t}, {"o", over ? 1 : 0}, {"v", 0}, {"ph", 0}, {"pt", t}, {"st", 0}, {"tt", 0}, {"p", json::array({player})},
                {"e", json::array()}, {"s", json::array()}, {"es", json::array()}, {"g", json::array()}, {"h", json::array()},
                {"r", json::array()}, {"z", json::array()}, {"ev", json::array()}};
  return json{{"type", type}, {"state", state}}.dump();
}
std::string joined(bool resumed = false) {
  return json{{"type", "joined"}, {"room", "ABC234"}, {"playerId", "u1"}, {"token", "tok"}, {"color", 0}, {"visibility", "open"},
              {"resumed", resumed}}.dump();
}
bool sentType(const FakeWire& w, const char* type) {
  for (const auto& s : w.sent) if (json::parse(s)["type"] == type) return true;
  return false;
}
json lastSent(const FakeWire& w) { return json::parse(w.sent.back()); }
SessionConfig config(FakeNet& net, const char* action) {
  SessionConfig c;
  c.url = "wss://example.com/ws";
  c.entry.action = action;
  c.entry.room = "ABC234";
  c.entry.name = "Ana";
  c.transport = net.factory();
  return c;
}
} // namespace

int main() {
  // Create -> lobby -> start -> in game, with pings and interpolation.
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    assert(net.wires.size() == 1 && net.wires[0]->url == "wss://example.com/ws" && s.status() == SessionStatus::Connecting);
    FakeWire& w = *net.wires[0];
    open(w);
    s.update(10);
    assert(json::parse(w.sent[0])["type"] == "create" && json::parse(w.sent[0])["v"] == kProtocolVersion);
    assert(sentType(w, "ping"));
    message(w, joined());
    message(w, R"({"type":"lobby","count":1,"visibility":"open","hostId":"u1","running":false,"campaign":"quick","curses":[],"players":[{"id":"u1","name":"Ana","color":0}]})");
    s.update(20);
    assert(s.status() == SessionStatus::Lobby && s.playerId() == "u1" && s.room() == "ABC234" && s.isHost());
    assert(!sentType(w, "ready"));                    // only joiners ask for the running state
    s.startMatch();
    assert(lastSent(w)["type"] == "start");
    message(w, R"({"type":"pong","t":990})");
    message(w, snapshot("start", 1.0, 0));
    s.update(1000);
    assert(s.status() == SessionStatus::Playing);
    assert(s.rttMs() < 120);                          // 0.7*120 + 0.3*(1000-990) = 87
    message(w, snapshot("state", 1.1, 10));
    s.update(1100);
    auto view = std::make_unique<GameState>();
    assert(s.frame(1150, 1.0 / 60, {0, 0}, *view));
    assert(view->players.at("u1").x >= 0 && view->players.at("u1").x <= 10);
    assert(sentType(w, "input"));
    s.update(2100);                                   // 2 s after the last ping: another one
    int pings = 0;
    for (const auto& m : w.sent) pings += json::parse(m)["type"] == "ping";
    assert(pings >= 2);
    s.special(); assert(lastSent(w)["type"] == "special");
    s.choosePower("arcane"); assert(lastSent(w)["power"] == "arcane");
    s.signal("help"); assert(lastSent(w)["signal"] == "help");
  }
  // Join: asks for the state with `ready`; a taken character switches to a free one and retries.
  {
    FakeNet net;
    Session s(config(net, "join"));
    s.start(0);
    FakeWire& w = *net.wires[0];
    open(w);
    s.update(1);
    message(w, R"({"type":"error","code":"CHARACTER_TAKEN","message":"Em uso","players":[{"id":"x","name":"Beto","color":0},{"id":"y","name":"Cris","color":1}]})");
    s.update(2);
    assert(lastSent(w)["type"] == "join" && lastSent(w)["color"] == 2 && s.color() == 2);
    assert(s.takeNotice().has_value());
    message(w, joined());
    s.update(3);
    assert(lastSent(w)["type"] == "ready");
  }
  // Errors before joining end the session with the server's message.
  {
    FakeNet net;
    Session s(config(net, "join"));
    s.start(0);
    open(*net.wires[0]);
    message(*net.wires[0], R"({"type":"error","message":"Sala não encontrada."})");
    s.update(1);
    assert(s.status() == SessionStatus::Closed && s.failure() == "Sala não encontrada.");
  }
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    error(*net.wires[0], true);
    s.update(1);
    assert(s.status() == SessionStatus::Closed && s.failure() == "Certificado do servidor inválido.");
  }
  // A drop mid-game reconnects with resume after 400 ms; a close code >= 4000 does not.
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    FakeWire& w = *net.wires[0];
    open(w);
    message(w, joined());
    message(w, snapshot("start", 1.0, 0));
    s.update(10);
    closeWith(w, 1006);
    s.update(20);
    assert(s.status() == SessionStatus::Reconnecting && net.wires.size() == 1);
    s.update(300);
    assert(net.wires.size() == 1);                    // still waiting for the first retry
    s.update(421);
    assert(net.wires.size() == 2);
    FakeWire& w2 = *net.wires[1];
    open(w2);
    s.update(430);
    assert(json::parse(w2.sent[0])["type"] == "resume" && json::parse(w2.sent[0])["token"] == "tok");
    message(w2, joined(true));
    message(w2, snapshot("start", 2.0, 5));
    s.update(440);
    assert(s.status() == SessionStatus::Playing);
    closeWith(w2, 4002, "Partida encerrada");
    s.update(450);
    assert(s.status() == SessionStatus::Closed && s.failure() == "Partida encerrada");
  }
  // Leaving sends `leave` and never reconnects; a finished match closes without a failure.
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    open(*net.wires[0]);
    s.update(1);
    s.leave();
    assert(net.wires[0]->closed && sentType(*net.wires[0], "leave") && s.status() == SessionStatus::Closed);
  }
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    FakeWire& w = *net.wires[0];
    open(w);
    message(w, joined());
    message(w, snapshot("start", 1.0, 0, true));
    s.update(10);
    assert(s.over());
    closeWith(w, 1000);
    s.update(20);
    assert(s.status() == SessionStatus::Closed && s.failure().empty() && net.wires.size() == 1);
  }
  // Broken snapshots are counted and skipped.
  {
    FakeNet net;
    Session s(config(net, "create"));
    s.start(0);
    open(*net.wires[0]);
    message(*net.wires[0], joined());
    message(*net.wires[0], R"({"type":"state","state":[]})");
    s.update(1);
    assert(s.droppedSnapshots() == 1);
  }
  std::puts("online_session: ok");
  return 0;
}
```

No `CMakeLists.txt`: `    arcana_online_test(online_session arcana_online)`.

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL — `session.hpp: No such file or directory`.

- [ ] **Step 3: Implementar** — `platforms/net/session.hpp`:

```cpp
#pragma once

#include "interpolation.hpp"
#include "prediction.hpp"
#include "protocol.hpp"
#include "transport.hpp"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace arcana::online {

enum class SessionStatus { Connecting, Lobby, Playing, Reconnecting, Closed };

struct SessionConfig {
  std::string url;
  EntryRequest entry;          // action "create" or "join"
  TransportFactory transport;
};

// One co-op connection (port of src/net.js createSession): lobby messages, a snapshot buffer drawn
// 100 ms in the past, prediction of the local player and automatic resume after a dropped
// connection. Time is passed in (monotonic milliseconds) so tests can drive it.
class Session {
public:
  static constexpr double kInterpolationDelay = 0.1;  // seconds
  static constexpr std::size_t kMaxSnapshots = 30;
  static constexpr double kPingEveryMs = 2000;
  static constexpr std::array<double, 8> kRetryDelaysMs{400, 800, 1500, 2500, 4000, 6000, 8000, 8000};

  explicit Session(SessionConfig config);
  void start(double nowMs);
  // Pumps transport events, pings and reconnect timers. Call every frame.
  void update(double nowMs);
  // In game: sends input (rate-limited) and writes the interpolated, predicted view into `out`.
  // False until the first snapshot arrived.
  bool frame(double nowMs, double dt, Vec2 input, GameState& out);

  void selectCharacter(int color);
  void startMatch();
  void choosePower(const std::string& id);
  void reroll();
  void special();
  void dash(Vec2 direction);
  void signal(const std::string& kind, std::optional<Vec2> at = {});
  void leave();

  [[nodiscard]] SessionStatus status() const { return status_; }
  [[nodiscard]] const std::string& playerId() const { return playerId_; }
  [[nodiscard]] const std::string& room() const { return room_; }
  [[nodiscard]] int color() const { return config_.entry.color; }
  [[nodiscard]] const LobbyInfo& lobby() const { return lobby_; }
  [[nodiscard]] bool isHost() const { return !playerId_.empty() && lobby_.hostId == playerId_; }
  [[nodiscard]] double rttMs() const { return rtt_; }
  [[nodiscard]] bool over() const { return over_; }
  // Why the session closed (empty when the player left or the match simply ended).
  [[nodiscard]] const std::string& failure() const { return failure_; }
  [[nodiscard]] int droppedSnapshots() const { return dropped_; }
  // One-line message for a toast (server errors after joining, character switched...).
  std::optional<std::string> takeNotice();

private:
  void openTransport(bool resume);
  void handle(const TransportEvent& event, double nowMs);
  void receive(ServerMessage& message, double nowMs);
  void closed(int code, const std::string& reason, double nowMs);
  void fail(std::string message);
  bool send(const std::string& text);

  SessionConfig config_;
  std::unique_ptr<Transport> transport_;
  bool transportOpen_{}, resumeOnOpen_{}, closedByUser_{}, inGame_{}, over_{};
  SessionStatus status_{SessionStatus::Connecting};
  std::string playerId_, room_, token_, failure_;
  std::optional<std::string> notice_;
  LobbyInfo lobby_;
  std::deque<Snapshot> snapshots_;
  std::optional<double> clockOffset_;
  double rtt_{120}, nextPingAt_{}, reconnectAt_{};
  std::size_t retries_{};
  int dropped_{};
  Interpolator interpolator_;
  InputSender inputSender_;
  Predictor predictor_;
  std::vector<TransportEvent> events_;
};

} // namespace arcana::online
```

`platforms/net/session.cpp`:

```cpp
#include "session.hpp"

#include <algorithm>
#include <utility>

namespace arcana::online {

Session::Session(SessionConfig config) : config_(std::move(config)) {}

void Session::start(double) { openTransport(false); }

void Session::openTransport(bool resume) {
  transport_ = config_.transport();
  transportOpen_ = false;
  resumeOnOpen_ = resume;
  transport_->open(config_.url);
}

bool Session::send(const std::string& text) {
  if (!transport_ || !transportOpen_) return false;
  transport_->send(text);
  return true;
}

std::optional<std::string> Session::takeNotice() { return std::exchange(notice_, std::nullopt); }

void Session::update(double now) {
  if (status_ == SessionStatus::Reconnecting && !transport_ && now >= reconnectAt_) openTransport(true);
  if (!transport_) return;
  events_.clear();
  transport_->poll(events_);
  for (const auto& e : events_) {
    handle(e, now);
    if (!transport_) return;
  }
  if (transportOpen_ && now >= nextPingAt_) {
    send(encodePing(now));
    nextPingAt_ = now + kPingEveryMs;
  }
}

void Session::handle(const TransportEvent& e, double now) {
  switch (e.type) {
    case TransportEvent::Type::Open:
      transportOpen_ = true;
      send(resumeOnOpen_ ? encodeResume(room_, token_) : encodeEntry(config_.entry));
      send(encodePing(now));
      nextPingAt_ = now + kPingEveryMs;
      break;
    case TransportEvent::Type::Message: {
      ServerMessage m = parseServerMessage(e.text);
      receive(m, now);
      break;
    }
    case TransportEvent::Type::Close: closed(e.code, e.text, now); break;
    case TransportEvent::Type::Error:
      if (!inGame_) failure_ = e.tls ? "Certificado do servidor inválido." : "Não foi possível alcançar o servidor.";
      closed(1006, {}, now);
      break;
  }
}

void Session::receive(ServerMessage& m, double now) {
  switch (m.kind) {
    case ServerKind::Pong: rtt_ = rtt_ * 0.7 + (now - m.pongT) * 0.3; break;
    case ServerKind::Joined:
      retries_ = 0;
      failure_.clear();
      playerId_ = m.joined.playerId;
      room_ = m.joined.room;
      token_ = m.joined.token;
      config_.entry.color = m.joined.color;
      status_ = inGame_ ? SessionStatus::Playing : SessionStatus::Lobby;
      if (config_.entry.action == "join" && !m.joined.resumed) send(encodeSimple("ready"));
      break;
    case ServerKind::Lobby: lobby_ = std::move(m.lobby); break;
    case ServerKind::Error:
      if (m.error.code == "CHARACTER_TAKEN" && playerId_.empty()) {
        // Like the web client: pick a character nobody in the room uses and ask again.
        for (int c = 0; c < 4; ++c) {
          const bool taken = std::any_of(m.error.players.begin(), m.error.players.end(), [&](const LobbyPlayer& p) { return p.color == c; });
          if (taken) continue;
          config_.entry.color = c;
          send(encodeEntry(config_.entry));
          notice_ = "Seu personagem já estava em uso; você entrou com outro.";
          return;
        }
      }
      if (playerId_.empty() || m.error.code == "RESUME_FAILED") fail(m.error.message);
      else notice_ = m.error.message;
      break;
    case ServerKind::Start:
    case ServerKind::State: {
      if (m.kind == ServerKind::Start) { snapshots_.clear(); predictor_.reset(); clockOffset_.reset(); }
      const GameState& state = *m.state;
      const double sample = now - state.time * 1000;
      // Track the fastest-arriving snapshot, drifting slowly so a lag spike does not stall rendering.
      clockOffset_ = clockOffset_ ? std::min(sample, *clockOffset_ + 4) : sample;
      snapshots_.push_back({state.time, now, m.state});
      if (snapshots_.size() > kMaxSnapshots) snapshots_.pop_front();
      if (const auto me = state.players.find(playerId_); me != state.players.end()) predictor_.reconcile(me->second, now, rtt_);
      over_ = state.over;
      if (m.kind == ServerKind::Start) inGame_ = true;
      if (inGame_) status_ = SessionStatus::Playing;
      break;
    }
    case ServerKind::BadSnapshot: ++dropped_; break;
    case ServerKind::Rooms:
    case ServerKind::Unknown: break;
  }
}

void Session::closed(int code, const std::string& reason, double now) {
  if (transport_) transport_->close();
  transport_.reset();
  transportOpen_ = false;
  if (closedByUser_ || status_ == SessionStatus::Closed) { status_ = SessionStatus::Closed; return; }
  if (inGame_ && !over_ && !token_.empty() && retries_ < kRetryDelaysMs.size() && code < 4000) {
    status_ = SessionStatus::Reconnecting;
    reconnectAt_ = now + kRetryDelaysMs[retries_++];
    return;
  }
  if (failure_.empty() && !over_) failure_ = code >= 4000 && !reason.empty() ? reason : "A conexão com o servidor caiu.";
  status_ = SessionStatus::Closed;
}

void Session::fail(std::string message) {
  failure_ = std::move(message);
  closedByUser_ = true;
  if (transport_) transport_->close();
  transport_.reset();
  transportOpen_ = false;
  status_ = SessionStatus::Closed;
}

bool Session::frame(double now, double dt, Vec2 input, GameState& out) {
  if (snapshots_.empty() || !clockOffset_) return false;
  const Vec2 q = quantizeInput(input);
  if (status_ == SessionStatus::Playing)
    if (auto message = inputSender_.update(q, now)) send(*message);
  const double serverNow = (now - *clockOffset_) / 1000;
  interpolator_.apply(snapshots_, serverNow - kInterpolationDelay, out);
  const GameState& latest = *snapshots_.back().state;
  const auto server = latest.players.find(playerId_);
  const auto mine = out.players.find(playerId_);
  if (server == latest.players.end() || mine == out.players.end()) return true;
  const Player& me = server->second;
  const bool canMove = me.alive && me.pendingPowers.empty() && !out.over && out.phaseStatus != "transition" && status_ != SessionStatus::Reconnecting;
  const Vec2 pos = predictor_.step(me, std::max(0.0, serverNow - latest.time), q, dt, canMove, now);
  mine->second.x = pos.x;
  mine->second.y = pos.y;
  return true;
}

void Session::selectCharacter(int color) {
  if (status_ != SessionStatus::Lobby) return;
  config_.entry.color = color;
  send(encodeSelectCharacter(color));
}
void Session::startMatch() { if (status_ == SessionStatus::Lobby && isHost()) send(encodeSimple("start")); }
void Session::choosePower(const std::string& id) { if (status_ == SessionStatus::Playing) send(encodeChoosePower(id)); }
void Session::reroll() { if (status_ == SessionStatus::Playing) send(encodeSimple("reroll")); }
void Session::special() { if (status_ == SessionStatus::Playing) send(encodeSimple("special")); }
void Session::dash(Vec2 d) { if (status_ == SessionStatus::Playing) send(encodeDash(d.x, d.y)); }
void Session::signal(const std::string& kind, std::optional<Vec2> at) { if (status_ == SessionStatus::Playing) send(encodeSignal(kind, at)); }

void Session::leave() {
  closedByUser_ = true;
  if (transport_) {
    send(encodeSimple("leave"));
    transport_->close();
    transport_.reset();
  }
  transportOpen_ = false;
  status_ = SessionStatus::Closed;
}

} // namespace arcana::online
```

- [ ] **Step 4: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `online_session: ok`.

- [ ] **Step 5: Commit**

```bash
git add platforms/net/session.hpp platforms/net/session.cpp tests/online_session_tests.cpp CMakeLists.txt
git commit -m "feat(online): session with lobby, snapshot buffer and reconnect

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 11: Lista de salas (`RoomList`)

**Files:**
- Create: `platforms/net/room_list.hpp`, `platforms/net/room_list.cpp`
- Modify: `tests/online_session_tests.cpp` (include + bloco antes do `std::puts`)

**Interfaces:**
- Produces: `class RoomList { RoomList(std::string url, TransportFactory factory); void update(double nowMs); void refresh(double nowMs); const RoomsInfo& rooms() const; bool loaded() const; const std::string& error() const; static constexpr double kRefreshMs = 5000, kTimeoutMs = 10000; };`

- [ ] **Step 1: Escrever o teste que falha** — em `tests/online_session_tests.cpp`, acrescente `#include "room_list.hpp"` e, antes de `std::puts("online_session: ok");`:

```cpp
  // Room list: a short connection per refresh, every 5 s; another protocol version hides the rooms.
  {
    FakeNet net;
    RoomList list("wss://example.com/ws", net.factory());
    list.update(0);
    assert(net.wires.size() == 1 && !list.loaded());
    open(*net.wires[0]);
    list.update(10);
    assert(json::parse(net.wires[0]->sent[0])["type"] == "listRooms");
    message(*net.wires[0], R"({"type":"rooms","rooms":[{"code":"XYZ789","count":1,"running":false,"host":"Ana","campaign":"quick","curses":[]}],"capacity":{"used":1,"max":3},"v":1})");
    list.update(20);
    assert(list.loaded() && list.rooms().rooms.size() == 1 && list.error().empty() && net.wires[0]->closed);
    list.update(3000);
    assert(net.wires.size() == 1);                     // next refresh only 5 s later
    list.update(5021);
    assert(net.wires.size() == 2);
    open(*net.wires[1]);
    message(*net.wires[1], R"({"type":"rooms","rooms":[{"code":"XYZ789"}],"capacity":{"used":1,"max":3},"v":2})");
    list.update(5030);
    assert(list.rooms().rooms.empty() && list.error().find("Atualize") != std::string::npos);
    list.refresh(5040);
    list.update(5040);
    assert(net.wires.size() == 3);
    error(*net.wires[2]);
    list.update(5050);
    assert(list.error() == "Não foi possível alcançar o servidor.");
  }
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL — `room_list.hpp: No such file or directory`.

- [ ] **Step 3: Implementar** — `platforms/net/room_list.hpp`:

```cpp
#pragma once

#include "protocol.hpp"
#include "transport.hpp"

#include <memory>
#include <string>
#include <vector>

namespace arcana::online {

// Open rooms for the online menu: a short-lived connection sends `listRooms` every 5 s (like the
// web menu's refresh). A server speaking another protocol version shows no rooms and an error.
class RoomList {
public:
  static constexpr double kRefreshMs = 5000, kTimeoutMs = 10000;
  RoomList(std::string url, TransportFactory factory);
  void update(double nowMs);
  void refresh(double nowMs) { if (!transport_) nextAt_ = nowMs; }
  [[nodiscard]] const RoomsInfo& rooms() const { return rooms_; }
  [[nodiscard]] bool loaded() const { return loaded_; }
  [[nodiscard]] const std::string& error() const { return error_; }

private:
  void finish(double nowMs);
  std::string url_;
  TransportFactory factory_;
  std::unique_ptr<Transport> transport_;
  double nextAt_{}, startedAt_{};
  RoomsInfo rooms_;
  bool loaded_{};
  std::string error_;
  std::vector<TransportEvent> events_;
};

} // namespace arcana::online
```

`platforms/net/room_list.cpp`:

```cpp
#include "room_list.hpp"

#include <utility>

namespace arcana::online {

RoomList::RoomList(std::string url, TransportFactory factory) : url_(std::move(url)), factory_(std::move(factory)) {}

void RoomList::finish(double now) {
  loaded_ = true;
  transport_->close();
  transport_.reset();
  nextAt_ = now + kRefreshMs;
}

void RoomList::update(double now) {
  if (!transport_) {
    if (now < nextAt_) return;
    transport_ = factory_();
    transport_->open(url_);
    startedAt_ = now;
    return;
  }
  events_.clear();
  transport_->poll(events_);
  for (const auto& e : events_) {
    if (e.type == TransportEvent::Type::Open) {
      transport_->send(encodeSimple("listRooms"));
    } else if (e.type == TransportEvent::Type::Message) {
      ServerMessage m = parseServerMessage(e.text);
      if (m.kind != ServerKind::Rooms) continue;
      if (m.rooms.version != kProtocolVersion) {
        rooms_ = {};
        error_ = "O servidor usa outra versão do jogo. Atualize para jogar online.";
      } else {
        rooms_ = std::move(m.rooms);
        error_.clear();
      }
      finish(now);
      return;
    } else {
      error_ = e.tls ? "Certificado do servidor inválido." : "Não foi possível alcançar o servidor.";
      finish(now);
      return;
    }
  }
  if (now - startedAt_ > kTimeoutMs) {
    error_ = "O servidor não respondeu.";
    finish(now);
  }
}

} // namespace arcana::online
```

- [ ] **Step 4: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `online_session: ok`.

- [ ] **Step 5: Commit**

```bash
git add platforms/net/room_list.hpp platforms/net/room_list.cpp tests/online_session_tests.cpp
git commit -m "feat(online): periodic open-room list

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 12: Entrada de texto (teclado e grade para controle)

**Files:**
- Create: `platforms/net/text_entry.hpp`, `platforms/net/text_entry.cpp`
- Create: `tests/online_text_tests.cpp`
- Modify: `CMakeLists.txt` (uma linha)

**Interfaces:**
- Produces: `enum class TextKind { RoomCode, Name };` e `class TextEntry { explicit TextEntry(TextKind kind = TextKind::Name, std::string initial = {}); void type(std::string_view utf8); void backspace(); void move(int dx, int dy); void press(); bool takeSubmit(); const std::string& value() const; bool valid() const; int cursor() const; int keyCount() const; std::string keyLabel(int index) const; TextKind kind() const; std::size_t maxLength() const; static constexpr int kColumns = 10; };`

- [ ] **Step 1: Escrever o teste que falha** — `tests/online_text_tests.cpp`:

```cpp
// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "text_entry.hpp"

#include <cassert>
#include <cstdio>

using namespace arcana::online;

int main() {
  // Room codes: upper-cased, only the server's alphabet (no I, O, 0, 1), exactly 6 characters.
  {
    TextEntry code(TextKind::RoomCode);
    code.type("ab1c-io2345xyz");
    assert(code.value() == "ABC234");
    assert(code.valid());
    code.backspace();
    assert(code.value() == "ABC23" && !code.valid());
    assert(code.keyCount() == 34 && code.keyLabel(0) == "A" && code.keyLabel(32) == "DEL" && code.keyLabel(33) == "OK");
  }
  // Gamepad grid: move, press a key, DEL and OK.
  {
    TextEntry code(TextKind::RoomCode);
    code.press();                                      // cursor starts on "A"
    code.move(1, 0); code.press();                     // "B"
    assert(code.value() == "AB");
    code.move(-2, 0);                                  // wraps inside the row: last key of row 0
    assert(code.cursor() == TextEntry::kColumns - 1);
    code.move(0, 5);                                   // clamps to the last row
    assert(code.cursor() >= 30);
    while (code.keyLabel(code.cursor()) != "DEL") code.move(1, 0);
    code.press();
    assert(code.value() == "A");
    code.type("BC234");
    while (code.keyLabel(code.cursor()) != "OK") code.move(1, 0);
    code.press();
    assert(code.takeSubmit() && !code.takeSubmit());
  }
  // Names: any printable text, 16 codepoints, trimmed for validity.
  {
    TextEntry name(TextKind::Name, "Ana");
    assert(name.valid());
    name.type(" L\xC3\xBA\x07");
    assert(name.value() == "Ana L\xC3\xBA");
    name.backspace();
    assert(name.value() == "Ana L");
    name.type("12345678901234567890");
    assert(name.value() == "Ana L12345678901");
    TextEntry blank(TextKind::Name, "   ");
    assert(!blank.valid());
    while (blank.keyLabel(blank.cursor()) != "OK") blank.move(1, 0); // OK on an invalid value does not submit
    blank.press();
    assert(!blank.takeSubmit());
    assert(TextEntry(TextKind::Name).keyLabel(62) == "ESP");
  }
  std::puts("online_text: ok");
  return 0;
}
```

No `CMakeLists.txt`: `    arcana_online_test(online_text arcana_online)`.

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL — `text_entry.hpp: No such file or directory`.

- [ ] **Step 3: Implementar** — `platforms/net/text_entry.hpp`:

```cpp
#pragma once

#include <string>
#include <string_view>

namespace arcana::online {

enum class TextKind { RoomCode, Name };

// Text typed on a keyboard (UTF-8 from SDL_TEXTINPUT) or picked on an on-screen grid with a gamepad
// (Steam Deck). Room codes use only the server's alphabet; names take any printable text.
class TextEntry {
public:
  static constexpr int kColumns = 10;
  static constexpr std::string_view kRoomAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  static constexpr std::string_view kNameAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ";

  explicit TextEntry(TextKind kind = TextKind::Name, std::string initial = {});
  void type(std::string_view utf8);
  void backspace();
  // Grid: dx wraps inside the row, dy moves between rows (clamped).
  void move(int dx, int dy);
  // Grid: types the key under the cursor, or DEL / OK.
  void press();
  // True once after OK was pressed on a valid value.
  bool takeSubmit();
  [[nodiscard]] const std::string& value() const { return value_; }
  [[nodiscard]] bool valid() const;
  [[nodiscard]] int cursor() const { return cursor_; }
  [[nodiscard]] int keyCount() const { return static_cast<int>(alphabet().size()) + 2; }
  [[nodiscard]] std::string keyLabel(int index) const;
  [[nodiscard]] TextKind kind() const { return kind_; }
  [[nodiscard]] std::size_t maxLength() const { return kind_ == TextKind::RoomCode ? 6 : 16; }

private:
  [[nodiscard]] std::string_view alphabet() const { return kind_ == TextKind::RoomCode ? kRoomAlphabet : kNameAlphabet; }
  [[nodiscard]] std::size_t length() const;
  TextKind kind_;
  std::string value_;
  int cursor_{};
  bool submit_{};
};

} // namespace arcana::online
```

`platforms/net/text_entry.cpp`:

```cpp
#include "text_entry.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace arcana::online {
namespace {
std::size_t sequenceLength(unsigned char lead) {
  return lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
}
} // namespace

TextEntry::TextEntry(TextKind kind, std::string initial) : kind_(kind) { type(initial); }

std::size_t TextEntry::length() const {
  std::size_t n = 0;
  for (std::size_t i = 0; i < value_.size(); i += sequenceLength(static_cast<unsigned char>(value_[i]))) ++n;
  return n;
}

void TextEntry::type(std::string_view in) {
  for (std::size_t i = 0; i < in.size();) {
    const auto lead = static_cast<unsigned char>(in[i]);
    const std::size_t len = sequenceLength(lead);
    if (i + len > in.size()) break;
    const std::string_view ch = in.substr(i, len);
    i += len;
    if (length() >= maxLength()) break;
    if (kind_ == TextKind::RoomCode) {
      const char c = static_cast<char>(std::toupper(lead));
      if (len == 1 && kRoomAlphabet.find(c) != std::string_view::npos) value_ += c;
    } else if (!(len == 1 && (lead < 0x20 || lead == 0x7F))) {
      value_.append(ch);
    }
  }
}

void TextEntry::backspace() {
  if (value_.empty()) return;
  std::size_t at = value_.size() - 1;
  while (at > 0 && (static_cast<unsigned char>(value_[at]) & 0xC0) == 0x80) --at; // start of the last character
  value_.erase(at);
}

void TextEntry::move(int dx, int dy) {
  const int count = keyCount();
  const int rows = (count + kColumns - 1) / kColumns;
  int row = cursor_ / kColumns, col = cursor_ % kColumns;
  row = std::clamp(row + dy, 0, rows - 1);
  const int inRow = std::min(kColumns, count - row * kColumns);
  col = ((col + dx) % inRow + inRow) % inRow;
  cursor_ = std::min(row * kColumns + col, count - 1);
}

void TextEntry::press() {
  const int letters = static_cast<int>(alphabet().size());
  if (cursor_ < letters) type(alphabet().substr(static_cast<std::size_t>(cursor_), 1));
  else if (cursor_ == letters) backspace();
  else if (valid()) submit_ = true;
}

bool TextEntry::takeSubmit() { return std::exchange(submit_, false); }

bool TextEntry::valid() const {
  if (kind_ == TextKind::RoomCode) return length() == 6;
  return value_.find_first_not_of(' ') != std::string::npos;
}

std::string TextEntry::keyLabel(int index) const {
  const int letters = static_cast<int>(alphabet().size());
  if (index == letters) return "DEL";
  if (index == letters + 1) return "OK";
  const char c = alphabet()[static_cast<std::size_t>(index)];
  return c == ' ' ? "ESP" : std::string(1, c);
}

} // namespace arcana::online
```

- [ ] **Step 4: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `online_text: ok`.

- [ ] **Step 5: Commit**

```bash
git add platforms/net/text_entry.hpp platforms/net/text_entry.cpp tests/online_text_tests.cpp CMakeLists.txt
git commit -m "feat(online): keyboard and gamepad-grid text entry

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 13: Modo online no `Frontend` (interface `OnlinePort`) e sinais na tela

**Files:**
- Modify: `platforms/sdl/frontend.hpp`
- Modify: `platforms/sdl/frontend.cpp`
- Modify: `platforms/sdl/animator.hpp` (enum `FxKind`), `platforms/sdl/animator.cpp` (`handleEvent`)
- Modify: `platforms/sdl/world_renderer.cpp` (passes de efeitos e setas)
- Test: `tests/frontend_tests.cpp`

**Interfaces:**
- Consumes: `Event::player/name` (Task 4).
- Produces (namespace `arcana::sdl`, em `frontend.hpp`):
  - `Action` ganha `ActSignalHere = 1u << 11, ActSignalHelp = 1u << 12, ActSignalDanger = 1u << 13, ActSignalLook = 1u << 14`.
  - `struct TextInput { std::string typed; int backspaces{}; bool submit{}; };` e `InputFrame::text`.
  - `class OnlinePort` (abaixo) e `Frontend::setOnline(std::unique_ptr<OnlinePort>)`, `Frontend::wantsTextInput() const`, `FrontendOptions::startOnline`.

- [ ] **Step 1: Escrever os testes que falham** — em `tests/frontend_tests.cpp`, acrescente `#include <optional>`, `#include <string>`, `#include <vector>` e, dentro do namespace anônimo:

```cpp
struct FakeOnline final : OnlinePort {
  std::unique_ptr<GameState> view = std::make_unique<GameState>();
  std::string id{"me"};
  bool isClosed{};
  MenuResult next{MenuResult::Stay};
  std::vector<std::string> calls;
  Vec2 lastInput{};
  void openMenu(const Profile&) override { calls.push_back("open"); }
  MenuResult updateMenu(std::uint32_t, std::uint32_t, const TextInput&, Profile&) override { return std::exchange(next, MenuResult::Stay); }
  void renderMenu(BatchRenderer&, float, float, float) override {}
  bool wantsText() const override { return false; }
  bool frame(double, Vec2 input, GameState& out) override { lastInput = input; out = *view; return true; }
  const std::string& localId() const override { return id; }
  bool reconnecting() const override { return false; }
  bool closed() const override { return isClosed; }
  std::optional<std::string> takeNotice() override { return std::nullopt; }
  void choosePower(const std::string& p) override { calls.push_back("choose:" + p); }
  void reroll() override { calls.push_back("reroll"); }
  void special() override { calls.push_back("special"); }
  void dash(Vec2) override { calls.push_back("dash"); }
  void signal(const char* kind, std::optional<Vec2>) override { calls.push_back(std::string("signal:") + kind); }
  void leave() override { calls.push_back("leave"); }
  [[nodiscard]] int count(const std::string& call) const { return static_cast<int>(std::count(calls.begin(), calls.end(), call)); }
};
// One frame with `held` on player 1 (edge-triggered like tap(), but with any combination held).
void hold(Frontend& f, std::uint32_t held, float x = 0, float y = 0) {
  InputFrame in;
  for (auto& pad : in.pads) pad.connected = true;
  in.pads[0].held = held; in.pads[0].x = x; in.pads[0].y = y;
  f.update(1.0 / 60, in);
}
```

(`#include <algorithm>` e `#include <utility>` também.) E em `main()`, antes do `return 0;` final:

```cpp
  // Online: the title gets "Jogar online" right after "Jogar"; the match runs on the server's view.
  {
    auto f = std::make_unique<Frontend>();
    auto owned = std::make_unique<FakeOnline>();
    FakeOnline& online = *owned;
    f->setOnline(std::move(owned));
    Player me = createPlayer("me", "Ana", 1), ally = createPlayer("ally", "Beto", 2);
    online.view->players["me"] = me;
    online.view->players["ally"] = ally;
    tap(*f, 0, ActDown);
    tap(*f, 0, ActConfirm);
    assert(online.count("open") == 1 && !f->inGame());
    online.next = OnlinePort::MenuResult::Play;
    hold(*f, 0);
    assert(f->inGame());
    hold(*f, 0, 1, 0);
    assert(f->state().players.size() == 2 && online.lastInput.x == 1);
    tap(*f, 0, ActSpecial);
    assert(online.count("special") == 1);
    tap(*f, 0, ActSignalHelp);
    assert(online.count("signal:help") == 1);
    hold(*f, ActAlt);                                  // gamepad: hold X/Y, then a direction
    hold(*f, ActAlt | ActUp);
    assert(online.count("signal:here") == 1);
    hold(*f, 0);
    // Power choice goes to the server once per offer.
    online.view->players["me"].pendingPowers = {"arcane", "haste"};
    hold(*f, 0);
    tap(*f, 0, ActConfirm);
    tap(*f, 0, ActConfirm);
    assert(online.count("choose:arcane") == 1);
    online.view->players["me"].pendingPowers.clear();
    // Esc opens the online menu over the running match; "Sair da sala" leaves.
    tap(*f, 0, ActPause);
    assert(f->inGame());
    tap(*f, 0, ActDown);
    tap(*f, 0, ActConfirm);
    assert(!f->inGame() && online.count("leave") == 1);
    // A dropped session ends the match too.
    online.next = OnlinePort::MenuResult::Play;
    hold(*f, 0);
    assert(f->inGame());
    online.isClosed = true;
    hold(*f, 0);
    assert(!f->inGame() && online.count("leave") == 2);
  }
  // Without an online port the title is unchanged (first item still starts a local run).
  {
    auto f = std::make_unique<Frontend>();
    tap(*f, 0, ActConfirm);
    assert(f->inGame());
  }
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL de compilação — `'OnlinePort' was not declared in this scope`.

- [ ] **Step 3: Implementar a interface** — em `platforms/sdl/frontend.hpp`:

Acrescente `#include <memory>` e `#include <optional>` aos includes.

Troque o `enum Action` por:

```cpp
enum Action : std::uint32_t {
  ActSpecial = 1u << 0, ActDash = 1u << 1, ActPause = 1u << 2, ActConfirm = 1u << 3, ActCancel = 1u << 4,
  ActAlt = 1u << 5, ActUp = 1u << 6, ActDown = 1u << 7, ActLeft = 1u << 8, ActRight = 1u << 9, ActDebug = 1u << 10,
  // Online signals to allies (PC keyboard Q/E/X/C; gamepads use Alt + direction instead).
  ActSignalHere = 1u << 11, ActSignalHelp = 1u << 12, ActSignalDanger = 1u << 13, ActSignalLook = 1u << 14,
};
```

Troque `struct InputFrame` por:

```cpp
// Text typed this frame (PC keyboard, only while the frontend asks for text). Consoles leave it empty.
struct TextInput {
  std::string typed;  // UTF-8
  int backspaces{};
  bool submit{};      // Enter
};

struct InputFrame {
  std::array<PadInput, cfg::MAX_PLAYERS> pads{};
  TextInput text;
};

// Online co-op, implemented on PC by platforms/online (IXWebSocket). The frontend only talks to this
// interface, so the console builds compile without any networking code.
class OnlinePort {
public:
  enum class MenuResult { Stay, Title, Play };
  virtual ~OnlinePort() = default;
  // Online pages (room list, create, name/code entry, lobby) before a match.
  virtual void openMenu(const Profile& profile) = 0;
  // `pressed`/`held`: player 1's Action bits (sticks already folded into directions).
  virtual MenuResult updateMenu(std::uint32_t pressed, std::uint32_t held, const TextInput& text, Profile& profile) = 0;
  virtual void renderMenu(BatchRenderer& b, float width, float height, float scale) = 0;
  [[nodiscard]] virtual bool wantsText() const = 0;
  // In a match: pumps the connection and writes the view to draw. False until the first snapshot.
  virtual bool frame(double dt, Vec2 input, GameState& out) = 0;
  [[nodiscard]] virtual const std::string& localId() const = 0;
  [[nodiscard]] virtual bool reconnecting() const = 0;
  // The connection is gone for good (the online pages show why).
  [[nodiscard]] virtual bool closed() const = 0;
  virtual std::optional<std::string> takeNotice() = 0;
  virtual void choosePower(const std::string& id) = 0;
  virtual void reroll() = 0;
  virtual void special() = 0;
  virtual void dash(Vec2 direction) = 0;
  virtual void signal(const char* kind, std::optional<Vec2> at) = 0;
  // Back to the online pages (after a match, or when the player leaves the room).
  virtual void leave() = 0;
};
```

Em `FrontendOptions`, acrescente:

```cpp
  bool startOnline{};   // open the online pages right away (--online-bot)
```

Em `class Frontend`, na parte pública, depois de `void setProfilePath(std::string path);`:

```cpp
  // PC only: enables "Jogar online" on the title screen.
  void setOnline(std::unique_ptr<OnlinePort> online);
  // The platform should deliver keyboard text (SDL_StartTextInput) while this is true.
  [[nodiscard]] bool wantsTextInput() const;
```

Na parte privada, troque os dois enums por:

```cpp
  enum class Screen { Title, Playing, Paused, Over, Shop, Online };
  enum class TitleItem : std::uint8_t { Play, Online, Campaign, Weapon, Special, Shop, Quit };
```

troque `native::StaticVector<TitleItem, 6> titleItems() const;` por `native::StaticVector<TitleItem, 7> titleItems() const;`, e acrescente às declarações privadas:

```cpp
  void resetRunView();
  void enterOnline();
  void updateOnlineMenu(const InputFrame& input);
  void startOnlineRun();
  void updateOnlinePlaying(double frameSeconds, const InputFrame& input);
  void updateOnlineChooser(const Player& me);
  void sendSignals(const Player& me);
  void leaveOnlineMatch();
  void renderOnlineOverlay(BatchRenderer& batch, float width, float height);
  native::StaticVector<const Player*, cfg::MAX_PLAYERS> hudPlayers() const;
```

e aos membros:

```cpp
  std::unique_ptr<OnlinePort> online_;
  bool onlineMatch_{}, onlineMenuOpen_{};
  std::string sentChoiceKey_;
  // Player id behind each local slot: "p1".."p4" offline; online only slot 0 (the server's id).
  std::array<std::string, cfg::MAX_PLAYERS> localIds_{"p1", "p2", "p3", "p4"};
```

- [ ] **Step 4: Implementar no `frontend.cpp`**

Acrescente `#include <cstring>` e `#include <optional>` aos includes. No namespace anônimo, depois de `formatClock`:

```cpp
const char* signalText(const std::string& kind) {
  // src/feedback.js SIGNAL_TEXT.
  if (kind == "here") return "venham aqui!";
  if (kind == "help") return "preciso de ajuda!";
  if (kind == "danger") return "cuidado!";
  if (kind == "look") return "olhem ali!";
  return "sinal";
}
```

`playerForSlot` passa a usar `localIds_`:

```cpp
Player* Frontend::playerForSlot(int slot) {
  const std::string& id = localIds_[static_cast<std::size_t>(slot)];
  if (id.empty()) return nullptr;
  const auto it = state_.players.find(id);
  return it == state_.players.end() ? nullptr : &it->second;
}
```

Troque `startRun()` inteiro por (corrige também a leitura de `state_` depois do destrutor):

```cpp
void Frontend::startRun() {
  depositRun(); // leaving a run early (restart) still banks its coins
  // Rebuild in place: GameState is ~200 KB and an assignment from a temporary would put a second
  // copy on the (small, on Vita) main-thread stack. Prvalue + placement new is guaranteed elision.
  state_.~GameState();
  new (&state_) GameState(createGameState(kCampaigns[std::clamp(campaign_, 0, 2)].id));
  // Permanent upgrades apply to everyone; the loadout is player 1's, as in the web client.
  const Loadout loadout{weapon_, special_};
  static constexpr const char* names[4] = {"Arcanista 1", "Arcanista 2", "Arcanista 3", "Arcanista 4"};
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    if (!joined_[static_cast<std::size_t>(slot)]) continue;
    Player p = createPlayer(kIds[slot], names[slot], character_[static_cast<std::size_t>(slot)], &profile_.upgrades, slot == 0 ? &loadout : nullptr);
    p.x = (slot % 2 ? 60 : -60) * (slot > 0 ? 1 : 0);
    p.y = (slot >= 2 ? 60 : 0);
    state_.players[p.id] = std::move(p);
  }
  onlineMatch_ = false;
  localIds_ = {kIds[0], kIds[1], kIds[2], kIds[3]};
  resetRunView();
  profile_.prefs.characters = character_;
  profile_.prefs.campaign = kCampaigns[std::clamp(campaign_, 0, 2)].id;
  profile_.prefs.weapon = weapon_;
  profile_.prefs.special = special_;
  saveProfileNow();
  screen_ = Screen::Playing;
}

void Frontend::resetRunView() {
  clock_.reset();
  choiceKey_.clear();
  choiceIndex_ = 0;
  camera_ = {};
  camera_.zoom = 0; // snap on the first rendered frame
  anim_.reset();
  announce_ = {}; toast_ = {};
  feedbackPrimed_ = false;
  hurtFlash_ = 0;
  lastHp_.fill(-1);
  sampled_ = {};
  hazardCount_ = 0;
  overSoundPlayed_ = false;
  deposited_ = false;
  earned_ = 0;
  overTime_ = 0;
}
```

Em `update()`, troque o `switch (screen_)` e o bloco seguinte por:

```cpp
  switch (screen_) {
    case Screen::Title:
      updateTitle();
      if (quitRequested_) return false;
      break;
    case Screen::Shop: updateShop(); announce_.age += frameSeconds; toast_.age += frameSeconds; break;
    case Screen::Online: updateOnlineMenu(input); announce_.age += frameSeconds; toast_.age += frameSeconds; break;
    case Screen::Playing: updatePlaying(frameSeconds, input); break;
    case Screen::Paused: updatePaused(); break;
    case Screen::Over: updateOver(); break;
  }
  updateMusic();
  if (screen_ != Screen::Title && screen_ != Screen::Shop && screen_ != Screen::Online) {
    // Animation time stops while paused or choosing a power, exactly like the web client (online the
    // match keeps running on the server, so it never stops there).
    anim_.update(state_, frameSeconds, screen_ == Screen::Paused || (screen_ == Screen::Playing && chooser() && !onlineMatch_));
```

(o resto do bloco — `hits`, `kills`, `announce_.age`… — fica igual).

`titleItems()`:

```cpp
native::StaticVector<Frontend::TitleItem, 7> Frontend::titleItems() const {
  native::StaticVector<TitleItem, 7> items;
  items.push_back(TitleItem::Play);
  if (online_) items.push_back(TitleItem::Online);
  items.push_back(TitleItem::Campaign);
  if (unlocked("arsenal")) items.push_back(TitleItem::Weapon);
  if (unlocked("secondSpell")) items.push_back(TitleItem::Special);
  items.push_back(TitleItem::Shop);
  items.push_back(TitleItem::Quit);
  return items;
}
```

No `switch` de `updateTitle()`, depois de `case TitleItem::Play: startRun(); break;`:

```cpp
    case TitleItem::Online: enterOnline(); break;
```

No `switch` de `renderTitle()`, depois do `case TitleItem::Play: ...`:

```cpp
      case TitleItem::Online:
        b.text(mx + 20 * s, y + 8 * s, "Jogar online", 24 * s, sel ? kGold : kMuted);
        if (sel) hint = "Co-op pela internet, junto com quem joga no navegador.";
        break;
```

Em `depositRun()`, troque `for (const auto& [_, p] : state_.players) coins = std::max(coins, p.coins);` por:

```cpp
  if (onlineMatch_) { if (const Player* me = playerForSlot(0)) coins = me->coins; } // online: only your own coins
  else for (const auto& [_, p] : state_.players) coins = std::max(coins, p.coins);
```

No topo de `updatePlaying()`:

```cpp
  if (onlineMatch_) { updateOnlinePlaying(frameSeconds, input); return; }
```

No topo de `updateOver()`:

```cpp
  if (onlineMatch_) {
    if (options_.autoplay || anyPressed(ActConfirm) || anyPressed(ActCancel)) leaveOnlineMatch();
    return;
  }
```

Acrescente as funções novas (depois de `autoplayInput`):

```cpp
void Frontend::setOnline(std::unique_ptr<OnlinePort> online) {
  online_ = std::move(online);
  if (online_ && options_.startOnline) enterOnline();
}

bool Frontend::wantsTextInput() const { return screen_ == Screen::Online && online_ && online_->wantsText(); }

void Frontend::enterOnline() {
  screen_ = Screen::Online;
  online_->openMenu(profile_);
}

void Frontend::updateOnlineMenu(const InputFrame& input) {
  const std::string name = profile_.prefs.name;
  const auto result = online_->updateMenu(edges_[0].pressed, edges_[0].held, input.text, profile_);
  if (profile_.prefs.name != name) saveProfileNow();
  if (result == OnlinePort::MenuResult::Title) { screen_ = Screen::Title; sfx(Sound::Click); }
  else if (result == OnlinePort::MenuResult::Play) startOnlineRun();
}

void Frontend::startOnlineRun() {
  state_.~GameState();
  new (&state_) GameState();
  onlineMatch_ = true;
  onlineMenuOpen_ = false;
  sentChoiceKey_.clear();
  localIds_ = {online_->localId(), "", "", ""};
  resetRunView();
  screen_ = Screen::Playing;
}

void Frontend::leaveOnlineMatch() {
  depositRun();
  online_->leave();
  onlineMatch_ = false;
  onlineMenuOpen_ = false;
  screen_ = Screen::Online;
}

void Frontend::updateOnlinePlaying(double frameSeconds, const InputFrame& input) {
  // No pause online: Esc/Start opens a small menu over the match, which keeps running.
  if (anyPressed(ActPause)) { onlineMenuOpen_ = !onlineMenuOpen_; pauseIndex_ = 0; sfx(Sound::Click); }
  else if (onlineMenuOpen_) {
    if (pressed(0, ActUp) || pressed(0, ActDown)) { pauseIndex_ = 1 - pauseIndex_; sfx(Sound::Click); }
    if (pressed(0, ActCancel)) onlineMenuOpen_ = false;
    else if (pressed(0, ActConfirm)) {
      if (pauseIndex_ == 1) { leaveOnlineMatch(); return; }
      onlineMenuOpen_ = false;
    }
  }
  const auto& pad = input.pads[0];
  // `real` is float on the PSP build: keep brace-init free of double -> float narrowing.
  Vec2 move{onlineMenuOpen_ ? real{0} : static_cast<real>(pad.x), onlineMenuOpen_ ? real{0} : static_cast<real>(pad.y)};
  if (const Player* me = playerForSlot(0); me && me->alive && !onlineMenuOpen_ && !state_.over) {
    if (!me->pendingPowers.empty()) {
      updateOnlineChooser(*me);
      move = {};
    } else {
      sentChoiceKey_.clear();
      if (pressed(0, ActSpecial)) online_->special();
      if (pressed(0, ActDash)) online_->dash(pad.x == 0 && pad.y == 0 ? Vec2{me->moveX, me->moveY} : Vec2{pad.x, pad.y});
      sendSignals(*me);
    }
  }
  const auto t0 = Clock::now();
  online_->frame(frameSeconds, move, state_);
  timings_.updateMs = msSince(t0);
  timings_.steps = 0;
  observeEvents();
  if (auto notice = online_->takeNotice()) announce(*notice, kMuted, true);
  if (online_->closed()) { leaveOnlineMatch(); return; }
  if (state_.over) {
    overTime_ += frameSeconds;
    if (overTime_ > 1.2) { screen_ = Screen::Over; depositRun(); }
  } else {
    overTime_ = 0;
  }
}

void Frontend::updateOnlineChooser(const Player& me) {
  std::string key;
  for (const auto& id : me.pendingPowers) { key += id; key += ','; }
  if (key != choiceKey_) { choiceKey_ = key; choiceIndex_ = 0; }
  if (key == sentChoiceKey_) return; // already answered: waiting for the server to apply it
  const int count = static_cast<int>(me.pendingPowers.size());
  if (pressed(0, ActLeft) || pressed(0, ActUp)) { choiceIndex_ = (choiceIndex_ + count - 1) % count; sfx(Sound::Click); }
  if (pressed(0, ActRight) || pressed(0, ActDown)) { choiceIndex_ = (choiceIndex_ + 1) % count; sfx(Sound::Click); }
  if (pressed(0, ActAlt)) {
    if (me.rerolls > 0) { online_->reroll(); sentChoiceKey_ = key; sfx(Sound::Click); }
    return;
  }
  if (pressed(0, ActConfirm) || options_.autoplay) {
    online_->choosePower(me.pendingPowers[static_cast<std::size_t>(std::clamp(choiceIndex_, 0, count - 1))]);
    sentChoiceKey_ = key;
    sfx(Sound::Power);
  }
}

void Frontend::sendSignals(const Player& me) {
  // Keyboard Q/E/X/C, or hold Alt (X/Y on a gamepad) and press a direction (web: Q/E/X and a click).
  const char* kind = nullptr;
  if (pressed(0, ActSignalHere)) kind = "here";
  else if (pressed(0, ActSignalHelp)) kind = "help";
  else if (pressed(0, ActSignalDanger)) kind = "danger";
  else if (pressed(0, ActSignalLook)) kind = "look";
  else if (edges_[0].held & ActAlt) {
    if (pressed(0, ActUp)) kind = "here";
    else if (pressed(0, ActLeft)) kind = "help";
    else if (pressed(0, ActRight)) kind = "danger";
    else if (pressed(0, ActDown)) kind = "look";
  }
  if (!kind) return;
  std::optional<Vec2> at;
  if (std::strcmp(kind, "look") == 0) {
    // There is no mouse click in the world: "look there" points 300 units ahead of where you face.
    const real len = std::max<real>(real(1e-6), std::hypot(me.moveX, me.moveY));
    at = Vec2{me.x + me.moveX / len * 300, me.y + me.moveY / len * 300};
  }
  online_->signal(kind, at);
  sfx(Sound::Signal);
}

native::StaticVector<const Player*, cfg::MAX_PLAYERS> Frontend::hudPlayers() const {
  native::StaticVector<const Player*, cfg::MAX_PLAYERS> list;
  auto find = [&](const std::string& id) -> const Player* {
    if (id.empty()) return nullptr;
    const auto it = state_.players.find(id);
    return it == state_.players.end() ? nullptr : &it->second;
  };
  if (!onlineMatch_) {
    for (const auto& id : localIds_) list.push_back(find(id)); // fixed corners per local slot
    return list;
  }
  // Online: you first, then allies by character.
  list.push_back(find(localIds_[0]));
  for (int c = 0; c < 4; ++c)
    for (const auto& [id, p] : state_.players)
      if (id != localIds_[0] && p.color == c && !list.full()) list.push_back(&p);
  return list;
}

void Frontend::renderOnlineOverlay(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  if (online_->reconnecting()) {
    const char* text = "Reconectando ao servidor...";
    const float w = b.textWidth(text, 22 * s) + 40 * s;
    b.rect(width * 0.5f - w * 0.5f, height * 0.5f - 24 * s, w, 48 * s, rgba(8, 10, 20, 220));
    b.text(width * 0.5f, height * 0.5f - 12 * s, text, 22 * s, kGold, Align::Center);
  }
  if (!onlineMenuOpen_) return;
  b.rect(0, 0, width, height, rgba(0, 0, 0, 140));
  b.text(width * 0.5f, height * 0.28f, "A partida continua", 40 * s, kText, Align::Center);
  const char* items[2] = {"Continuar", "Sair da sala"};
  for (int i = 0; i < 2; ++i) {
    const float y = height * 0.4f + i * 60 * s;
    const bool sel = i == pauseIndex_;
    b.rect(width * 0.5f - 200 * s, y, 400 * s, 50 * s, sel ? rgba(40, 44, 80, 240) : rgba(18, 20, 36, 220));
    if (sel) b.frame(width * 0.5f - 200 * s, y, 400 * s, 50 * s, 3 * s, kGold);
    b.text(width * 0.5f, y + 11 * s, items[i], 24 * s, sel ? kText : kMuted, Align::Center);
  }
}
```

Em `render()`, troque o `if (screen_ == Screen::Title) ... else if (screen_ == Screen::Shop) ... else { ... }` por:

```cpp
  if (screen_ == Screen::Title) {
    renderTitle(batch, width, height);
  } else if (screen_ == Screen::Shop) {
    renderShop(batch, width, height);
  } else if (screen_ == Screen::Online) {
    online_->renderMenu(batch, width, height, uiScale(height));
    renderFeedback(batch, width, height);
  } else {
    renderWorld(batch, width, height);
    renderHud(batch, width, height);
    renderFeedback(batch, width, height);
    if (screen_ == Screen::Playing && chooser()) renderChooser(batch, width, height);
    if (screen_ == Screen::Playing && onlineMatch_) renderOnlineOverlay(batch, width, height);
    if (screen_ == Screen::Paused) renderPause(batch, width, height);
    if (screen_ == Screen::Over) renderOver(batch, width, height);
  }
```

Em `renderWorld()`, dentro do primeiro `for (const auto& [_, p] : state_.players) {`, na primeira linha do corpo:

```cpp
    if (onlineMatch_ && p.id != localIds_[0]) continue; // online: the camera follows you only
```

Em `observeEvents()`, antes de `playEventSound(e);`:

```cpp
    else if (k == "signal" && onlineMatch_ && e.player != localIds_[0]) {
      std::snprintf(scratch_, sizeof scratch_, "%s: %s", e.name.c_str(), signalText(e.text));
      announce(scratch_, playerColor(e.color), true);
    }
```

Em `playEventSound()`, antes de `else if (!near) return;`:

```cpp
  else if (k == "signal") sfx(Sound::Signal);
```

Em `renderHud()`, troque o início do laço de painéis (de `// One panel per player, in the four corners.` até a linha do `b.text(... scratch_, 20 * s, playerColor(p->color));` do nível) por:

```cpp
  // One panel per player, in the four corners (online: you top-left, allies in the other corners).
  const auto hud = hudPlayers();
  for (std::size_t index = 0; index < hud.size(); ++index) {
    const Player* p = hud[index];
    if (!p) continue;
    const int slot = static_cast<int>(index);
    const float pw = 310 * s, ph = 86 * s;
    const float x = (slot % 2 == 0) ? 10 * s : width - pw - 10 * s;
    const float y = (slot < 2) ? 10 * s : height - ph - 10 * s;
    b.rect(x, y, pw, ph, kPanel);
    b.rect(x, y, 4 * s, ph, playerColor(p->color));
    if (onlineMatch_) std::snprintf(scratch_, sizeof scratch_, "%s  Nv %d", p->name.c_str(), p->level);
    else std::snprintf(scratch_, sizeof scratch_, "P%d  Nv %d", slot + 1, p->level);
    b.text(x + 12 * s, y + 4 * s, scratch_, 20 * s, playerColor(p->color));
```

(o resto do corpo do laço fica igual).

Em `renderChooser()`, troque a linha do `std::snprintf(... "P%d subiu para o nível %d - escolha um poder" ...)` por:

```cpp
  if (onlineMatch_) std::snprintf(scratch_, sizeof scratch_, "Você subiu para o nível %d - escolha um poder", p->level);
  else std::snprintf(scratch_, sizeof scratch_, "P%d subiu para o nível %d - escolha um poder", slot + 1, p->level);
```

Em `renderOver()`, troque o laço `for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) { const Player* p = playerForSlot(slot); ... }` por:

```cpp
  const auto hud = hudPlayers();
  for (std::size_t index = 0; index < hud.size(); ++index) {
    const Player* p = hud[index];
    if (!p) continue;
    if (onlineMatch_) std::snprintf(scratch_, sizeof scratch_, "%s · nível %d · %d abates · %d de dano · %d moedas", p->name.c_str(), p->level,
                                    p->stats.kills, static_cast<int>(p->stats.damage), p->coins);
    else std::snprintf(scratch_, sizeof scratch_, "P%d · nível %d · %d abates · %d de dano · %d moedas", static_cast<int>(index) + 1, p->level,
                       p->stats.kills, static_cast<int>(p->stats.damage), p->coins);
    b.text(width * 0.5f, y, scratch_, 22 * s, playerColor(p->color), Align::Center);
    y += 34 * s;
  }
```

e a última linha por:

```cpp
  b.text(width * 0.5f, height * 0.8f, onlineMatch_ ? "A ou B: voltar ao menu online" : "A jogar de novo · B menu principal", 22 * s, kMuted, Align::Center);
```

- [ ] **Step 5: Efeito e seta de sinal** — em `platforms/sdl/animator.hpp`, acrescente `Signal` ao fim do `enum class FxKind` (`... Ghost, Combo, Signal,`). Em `platforms/sdl/animator.cpp`, no `handleEvent`, antes do `} else if (k == "convergence") {`:

```cpp
  } else if (k == "signal") {
    // An ally's call: a pulse where they pointed, with an off-screen arrow while it lasts.
    static constexpr const char* labels[] = {"VENHAM AQUI", "AJUDA", "CUIDADO", "OLHEM ALI"};
    const int which = e.text == "help" ? 1 : e.text == "danger" ? 2 : e.text == "look" ? 3 : 0;
    if (Effect* fx = push(FxKind::Signal, x, y, 3.0f, true)) { fx->color = native::playerColor(e.color); fx->text = labels[which]; }
```

Em `platforms/sdl/world_renderer.cpp`, no `switch` de `drawEffectsNormal` (onde está `case FxKind::Combo:`), acrescente:

```cpp
      case FxKind::Signal:
        b.textOutlined(fx.x, fx.y - 64, fx.text ? fx.text : "", 16, A(fx.color, std::min(1.0f, fade * 3)), A(rgba(10, 10, 16), std::min(1.0f, fade * 3)), Align::Center);
        break;
```

no `switch` de `drawEffectsAdditive` (onde está `case FxKind::Ring:`), acrescente:

```cpp
      case FxKind::Signal: {
        const float pulse = std::fmod(fx.age, 1.0f);
        b.circle(fx.x, fx.y, 20 + pulse * 70, A(fx.color, (1 - pulse) * fade), 3);
        b.circle(fx.x, fx.y, 10, A(fx.color, fade * 0.8f));
        break;
      }
```

e, no fim de `drawWorld` (depois da seta do `ALTAR`):

```cpp
  for (const auto& fx : anim.effects())
    if (fx.kind == FxKind::Signal && !inView(fx.x, fx.y)) edgeArrow(b, v, fx.x, fx.y, fx.color, fx.text ? fx.text : "SINAL");
```

- [ ] **Step 6: Rodar os testes (e os consoles)**

Run: `tools/docker/build-all.sh host switch vita psp`
Expected: `host ok (testes passaram)` com o `frontend` passando; `switch`, `vita` e `psp` ok (nenhum código de rede entrou em `platforms/sdl`).

- [ ] **Step 7: Commit**

```bash
git add platforms/sdl tests/frontend_tests.cpp
git commit -m "feat(frontend): online match mode behind OnlinePort, ally signals on screen

Also fixes startRun() reading state_ after destroying it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 14: Telas online (`OnlineMenu`)

**Files:**
- Create: `platforms/online/online_menu.hpp`, `platforms/online/online_menu.cpp`
- Create: `tests/online_menu_tests.cpp`
- Modify: `CMakeLists.txt` (alvo `arcana_online_menu` no bloco do desktop; um teste)

**Interfaces:**
- Consumes: `sdl::OnlinePort` (Task 13), `Session` (10), `RoomList` (11), `TextEntry` (12), `checkServerUrl` (3).
- Produces (namespace `arcana::online`):
  - `inline constexpr std::string_view kDefaultServer = "wss://vps65228.publiccloud.com.br/ws";`
  - `struct OnlineMenuConfig { std::string url{kDefaultServer}; bool forceUrl{}; bool allowInsecure{}; TransportFactory transport; std::string bot; int botPlayers{2}; };`
  - `class OnlineMenu final : public sdl::OnlinePort` com `enum class Page { Name, Home, Create, Code, Connecting, Lobby, Playing }`, `page()`, `message()`, `setClockForTests(std::function<double()>)`.

- [ ] **Step 1: Escrever o teste que falha** — `tests/online_menu_tests.cpp`:

```cpp
// Tests rely on assert(); keep it active in Release builds.
#undef NDEBUG
#include "fake_transport.hpp"
#include "online_menu.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdio>
#include <memory>

using namespace arcana;
using namespace arcana::online;
using namespace arcana::online::testing;
using arcana::sdl::OnlinePort;
using arcana::sdl::TextInput;
using nlohmann::json;

namespace {
double now = 0;
OnlinePort::MenuResult step(OnlineMenu& m, Profile& p, std::uint32_t pressed = 0, TextInput text = {}) {
  now += 16;
  return m.updateMenu(pressed, 0, text, p);
}
} // namespace

int main() {
  FakeNet net;
  OnlineMenuConfig config;
  config.transport = net.factory();
  OnlineMenu menu(config);
  menu.setClockForTests([] { return now; });
  Profile profile;

  // First visit asks for a name.
  menu.openMenu(profile);
  assert(menu.page() == OnlineMenu::Page::Name && menu.wantsText());
  step(menu, profile, 0, TextInput{"Ana", 0, true});
  assert(profile.prefs.name == "Ana" && menu.page() == OnlineMenu::Page::Home && !menu.wantsText());

  // The room list is fetched; confirming the first room joins it.
  step(menu, profile);
  FakeWire& list = *net.wires.back();
  assert(list.url == std::string(kDefaultServer));
  open(list);
  step(menu, profile);
  message(list, R"({"type":"rooms","rooms":[{"code":"XYZ789","count":1,"running":true,"host":"Beto","campaign":"quick","curses":[]}],"capacity":{"used":1,"max":3},"v":1})");
  step(menu, profile);
  step(menu, profile, sdl::ActConfirm);
  assert(menu.page() == OnlineMenu::Page::Connecting);
  FakeWire& game = *net.wires.back();
  open(game);
  step(menu, profile);
  const json entry = json::parse(game.sent[0]);
  assert(entry["type"] == "join" && entry["room"] == "XYZ789" && entry["name"] == "Ana");

  // Joined a running room: the first snapshot starts the match.
  message(game, R"({"type":"joined","room":"XYZ789","playerId":"u2","token":"t","color":1})");
  message(game, R"({"type":"start","state":{"t":1,"p":[["u2","Ana",1,0,0,100,100,0,1,true,190,{},null]],"e":[],"s":[],"es":[],"g":[],"h":[],"r":[],"z":[],"ev":[]}})");
  assert(step(menu, profile) == OnlinePort::MenuResult::Play);
  assert(menu.localId() == "u2");
  auto view = std::make_unique<GameState>();
  now += 200;
  assert(menu.frame(1.0 / 60, {0, 0}, *view) && view->players.count("u2") == 1);

  // Leaving goes back to the room list.
  menu.leave();
  assert(menu.page() == OnlineMenu::Page::Home && menu.closed());

  // Create: toggle visibility, then "Criar sala" (row 8).
  const int rooms = 1;
  for (int i = 0; i < rooms; ++i) step(menu, profile, sdl::ActDown); // skip the room row
  step(menu, profile, sdl::ActConfirm);                              // "Criar sala"
  assert(menu.page() == OnlineMenu::Page::Create);
  step(menu, profile, sdl::ActConfirm);                              // visibility: aberta -> fechada
  for (int i = 0; i < 8; ++i) step(menu, profile, sdl::ActDown);
  step(menu, profile, sdl::ActConfirm);
  assert(menu.page() == OnlineMenu::Page::Connecting);
  FakeWire& created = *net.wires.back();
  open(created);
  step(menu, profile);
  assert(json::parse(created.sent[0])["visibility"] == "closed");
  message(created, R"({"type":"joined","room":"NEW234","playerId":"u1","token":"t","color":0})");
  message(created, R"({"type":"lobby","count":1,"visibility":"closed","hostId":"u1","running":false,"campaign":"quick","curses":[],"players":[{"id":"u1","name":"Ana","color":0}]})");
  step(menu, profile);
  assert(menu.page() == OnlineMenu::Page::Lobby);
  step(menu, profile, sdl::ActRight);                               // next free character
  assert(json::parse(created.sent.back())["type"] == "selectCharacter");
  step(menu, profile, sdl::ActDown);                                // "Iniciar"
  step(menu, profile, sdl::ActConfirm);
  assert(json::parse(created.sent.back())["type"] == "start");

  // Cancel while in the lobby leaves the room.
  step(menu, profile, sdl::ActCancel);
  assert(menu.page() == OnlineMenu::Page::Home && created.closed);

  // Plain ws:// to a remote host is refused before connecting.
  {
    FakeNet other;
    OnlineMenuConfig c;
    c.url = "ws://example.com/ws";
    c.forceUrl = true;
    c.transport = other.factory();
    OnlineMenu insecure(c);
    insecure.setClockForTests([] { return now; });
    Profile named;
    named.prefs.name = "Ana";
    insecure.openMenu(named);
    step(insecure, named);
    assert(other.wires.empty() && !insecure.message().empty());
  }
  std::puts("online_menu: ok");
  return 0;
}
```

- [ ] **Step 2: CMake** — no bloco `if(ARCANA_BUILD_DESKTOP)`, depois do `if(MINGW) ... endif()` do `arcana_desktop`:

```cmake
  if(TARGET arcana_online)
    add_library(arcana_online_menu platforms/online/online_menu.cpp)
    target_include_directories(arcana_online_menu PUBLIC platforms/online)
    target_link_libraries(arcana_online_menu PUBLIC arcana_frontend arcana_online)
    target_link_libraries(arcana_desktop PRIVATE arcana_online_menu)
    target_compile_definitions(arcana_desktop PRIVATE ARCANA_HAS_ONLINE=1)
  endif()
```

E, no bloco de testes online (depois de `arcana_online_test(online_text arcana_online)`):

```cmake
    if(TARGET arcana_online_menu)
      arcana_online_test(online_menu arcana_online_menu)
    endif()
```

(Como `arcana_online` é definido antes do bloco do desktop, `TARGET arcana_online` já existe ali.)

- [ ] **Step 3: Rodar e ver falhar**

Run: `tools/docker/build-all.sh host`
Expected: FAIL — `online_menu.cpp: No such file` / `online_menu.hpp: No such file`.

- [ ] **Step 4: Implementar** — `platforms/online/online_menu.hpp`:

```cpp
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
```

`platforms/online/online_menu.cpp`:

```cpp
#include "online_menu.hpp"

#include "batch_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace arcana::online {
namespace {

using native::rgba;
using sdl::Align;
using MenuResult = sdl::OnlinePort::MenuResult;

constexpr std::uint32_t kBg = rgba(10, 10, 22), kPanel = rgba(18, 20, 36, 220), kSel = rgba(40, 44, 80, 240);
constexpr std::uint32_t kText = rgba(236, 240, 255), kMuted = rgba(150, 160, 190), kGold = rgba(255, 214, 110), kDanger = rgba(255, 140, 120);
constexpr const char* kCampaignIds[3] = {"quick", "classic", "endless"};
constexpr const char* kCampaignTitles[3] = {"Ritual rápido", "Ritual clássico", "Ritual infinito"};
struct CurseText { const char* id; const char* title; const char* desc; };
// server/curses.js CURSES.
constexpr CurseText kCurses[6] = {
  {"swarm", "Enxame", "Ondas 35% maiores e mais inimigos na tela"},
  {"frenzy", "Frenesi", "Inimigos 15% mais rápidos"},
  {"brittle", "Fragilidade", "Você recebe 25% mais dano"},
  {"famine", "Fome", "Corações e curas restauram metade"},
  {"tyrant", "Tirania", "Guardiões com 40% mais vida"},
  {"nobility", "Nobreza sombria", "Cada chamado de elite traz duas elites"},
};
constexpr const char* kCharacterNames[4] = {"Azul", "Vermelho", "Verde", "Roxo"};
constexpr int kMaxRoomRows = 6;
constexpr int kCreateRows = 10; // visibility, ritual, 6 curses, create, back

const char* campaignTitle(const std::string& id) {
  for (int i = 0; i < 3; ++i) if (id == kCampaignIds[i]) return kCampaignTitles[i];
  return kCampaignTitles[0];
}
double steadyMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int rankOf(const Profile& p, const char* id) {
  const auto it = p.upgrades.rank.find(id);
  return it == p.upgrades.rank.end() ? 0 : it->second;
}
bool is(std::uint32_t pressed, std::uint32_t action) { return (pressed & action) != 0; }
int wrap(int value, int count) { return count <= 0 ? 0 : (value % count + count) % count; }

void row(sdl::BatchRenderer& b, float x, float y, float w, float s, bool selected, const char* label, const char* value = nullptr,
         std::uint32_t valueColor = 0) {
  b.rect(x, y, w, 42 * s, selected ? kSel : kPanel);
  if (selected) b.frame(x, y, w, 42 * s, 3 * s, kGold);
  b.text(x + 20 * s, y + 8 * s, label, 22 * s, selected ? kText : kMuted);
  if (value) b.text(x + w - 20 * s, y + 10 * s, value, 20 * s, valueColor ? valueColor : selected ? kGold : kMuted, Align::Right);
}

} // namespace

OnlineMenu::OnlineMenu(OnlineMenuConfig config) : config_(std::move(config)), now_(steadyMs) {}

void OnlineMenu::openMenu(const Profile& profile) {
  url_ = !config_.forceUrl && !profile.prefs.server.empty() ? profile.prefs.server : config_.url;
  const UrlCheck check = checkServerUrl(url_, config_.allowInsecure);
  urlOk_ = check == UrlCheck::Ok;
  message_.clear();
  if (check == UrlCheck::Invalid) message_ = "Endereço do servidor inválido: " + url_;
  if (check == UrlCheck::InsecureBlocked) message_ = "Conexões sem TLS (ws://) só são permitidas para localhost. Use --insecure-ws para testes.";
  rooms_ = urlOk_ ? std::make_unique<RoomList>(url_, config_.transport) : nullptr;
  endlessUnlocked_ = rankOf(profile, "endless") > 0;
  name_ = profile.prefs.name;
  campaign_ = 0;
  for (int i = 0; i < 3; ++i) if (profile.prefs.campaign == kCampaignIds[i]) campaign_ = i;
  if (campaign_ == 2 && !endlessUnlocked_) campaign_ = 0;
  index_ = 0;
  page_ = profile.prefs.name.empty() ? Page::Name : Page::Home;
  if (page_ == Page::Name) entry_ = TextEntry(TextKind::Name);
}

EntryRequest OnlineMenu::baseEntry(const Profile& profile) const {
  EntryRequest e;
  e.name = profile.prefs.name.empty() ? "Arcanista" : profile.prefs.name;
  e.color = profile.prefs.characters[0];
  e.campaign = profile.prefs.campaign;
  e.meta = profile.upgrades;
  e.loadout.weapon = rankOf(profile, "arsenal") > 0 ? profile.prefs.weapon : "";
  e.loadout.special = rankOf(profile, "secondSpell") > 0 ? profile.prefs.special : 0;
  return e;
}

void OnlineMenu::connect(EntryRequest entry) {
  if (!urlOk_) return;
  session_ = std::make_unique<Session>(SessionConfig{url_, std::move(entry), config_.transport});
  session_->start(now_());
  message_.clear();
  index_ = 0;
  page_ = Page::Connecting;
}

void OnlineMenu::backHome() {
  session_.reset();
  page_ = Page::Home;
  index_ = 0;
  if (rooms_) rooms_->refresh(now_());
}

int OnlineMenu::visibleRooms() const {
  return rooms_ ? std::min(kMaxRoomRows, static_cast<int>(rooms_->rooms().rooms.size())) : 0;
}

MenuResult OnlineMenu::updateMenu(std::uint32_t pressed, std::uint32_t, const sdl::TextInput& text, Profile& profile) {
  const double now = now_();
  if (rooms_ && page_ == Page::Home) rooms_->update(now);
  if (session_) session_->update(now);
  if (!config_.bot.empty()) updateBot(profile);
  switch (page_) {
    case Page::Name:
    case Page::Code: return updateText(pressed, text, profile);
    case Page::Home: return updateHome(pressed, profile);
    case Page::Create: updateCreate(pressed, profile); return MenuResult::Stay;
    case Page::Connecting:
    case Page::Lobby: return updateSession(pressed);
    case Page::Playing: return MenuResult::Stay;
  }
  return MenuResult::Stay;
}

MenuResult OnlineMenu::updateHome(std::uint32_t pressed, Profile& profile) {
  if (is(pressed, sdl::ActCancel) || is(pressed, sdl::ActPause)) return MenuResult::Title;
  const int rooms = visibleRooms(), rows = rooms + 4;
  if (is(pressed, sdl::ActUp)) index_ = wrap(index_ - 1, rows);
  if (is(pressed, sdl::ActDown)) index_ = wrap(index_ + 1, rows);
  index_ = std::clamp(index_, 0, rows - 1);
  if (!is(pressed, sdl::ActConfirm)) return MenuResult::Stay;
  if (index_ < rooms) {
    EntryRequest e = baseEntry(profile);
    e.action = "join";
    e.room = rooms_->rooms().rooms[static_cast<std::size_t>(index_)].code;
    connect(std::move(e));
  } else if (index_ == rooms) {
    page_ = Page::Create;
    index_ = 0;
  } else if (index_ == rooms + 1) {
    entry_ = TextEntry(TextKind::RoomCode);
    page_ = Page::Code;
  } else if (index_ == rooms + 2) {
    entry_ = TextEntry(TextKind::Name, profile.prefs.name);
    page_ = Page::Name;
  } else {
    return MenuResult::Title;
  }
  return MenuResult::Stay;
}

void OnlineMenu::updateCreate(std::uint32_t pressed, Profile& profile) {
  if (is(pressed, sdl::ActCancel) || is(pressed, sdl::ActPause)) { page_ = Page::Home; index_ = 0; return; }
  if (is(pressed, sdl::ActUp)) index_ = wrap(index_ - 1, kCreateRows);
  if (is(pressed, sdl::ActDown)) index_ = wrap(index_ + 1, kCreateRows);
  const bool change = is(pressed, sdl::ActConfirm) || is(pressed, sdl::ActLeft) || is(pressed, sdl::ActRight);
  if (!change) return;
  if (index_ == 0) open_ = !open_;
  else if (index_ == 1) do campaign_ = (campaign_ + 1) % 3; while (campaign_ == 2 && !endlessUnlocked_);
  else if (index_ < 8) curses_[static_cast<std::size_t>(index_ - 2)] = !curses_[static_cast<std::size_t>(index_ - 2)];
  else if (index_ == 8 && is(pressed, sdl::ActConfirm)) {
    EntryRequest e = baseEntry(profile);
    e.action = "create";
    e.visibility = open_ ? "open" : "closed";
    e.campaign = kCampaignIds[campaign_];
    for (int i = 0; i < 6; ++i) if (curses_[static_cast<std::size_t>(i)]) e.curses.push_back(kCurses[i].id);
    connect(std::move(e));
  } else if (index_ == 9 && is(pressed, sdl::ActConfirm)) {
    page_ = Page::Home;
    index_ = 0;
  }
}

MenuResult OnlineMenu::updateText(std::uint32_t pressed, const sdl::TextInput& text, Profile& profile) {
  entry_.type(text.typed);
  for (int i = 0; i < text.backspaces; ++i) entry_.backspace();
  if (is(pressed, sdl::ActUp)) entry_.move(0, -1);
  if (is(pressed, sdl::ActDown)) entry_.move(0, 1);
  if (is(pressed, sdl::ActLeft)) entry_.move(-1, 0);
  if (is(pressed, sdl::ActRight)) entry_.move(1, 0);
  if (is(pressed, sdl::ActConfirm)) entry_.press();
  const bool submit = entry_.takeSubmit() || (text.submit && entry_.valid());
  const bool back = is(pressed, sdl::ActCancel) || is(pressed, sdl::ActPause);
  if (page_ == Page::Name) {
    if (submit) {
      std::string name = entry_.value();
      name.erase(0, name.find_first_not_of(' '));
      name.erase(name.find_last_not_of(' ') + 1);
      profile.prefs.name = name_ = name;
      page_ = Page::Home;
      index_ = 0;
    } else if (back) {
      if (profile.prefs.name.empty()) return MenuResult::Title;
      page_ = Page::Home;
    }
  } else if (submit) {
    EntryRequest e = baseEntry(profile);
    e.action = "join";
    e.room = entry_.value();
    connect(std::move(e));
  } else if (back) {
    page_ = Page::Home;
  }
  return MenuResult::Stay;
}

MenuResult OnlineMenu::updateSession(std::uint32_t pressed) {
  if (auto notice = session_->takeNotice()) message_ = *notice;
  switch (session_->status()) {
    case SessionStatus::Closed:
      message_ = session_->failure().empty() ? "Você saiu da sala." : session_->failure();
      backHome();
      return MenuResult::Stay;
    case SessionStatus::Playing:
      page_ = Page::Playing;
      return MenuResult::Play;
    case SessionStatus::Lobby:
      if (page_ != Page::Lobby) { page_ = Page::Lobby; index_ = 0; }
      break;
    default: break;
  }
  if (is(pressed, sdl::ActCancel) || is(pressed, sdl::ActPause)) {
    session_->leave();
    message_.clear();
    backHome();
    return MenuResult::Stay;
  }
  if (page_ != Page::Lobby) return MenuResult::Stay;
  constexpr int rows = 3; // character, start, leave
  if (is(pressed, sdl::ActUp)) index_ = wrap(index_ - 1, rows);
  if (is(pressed, sdl::ActDown)) index_ = wrap(index_ + 1, rows);
  if (is(pressed, sdl::ActLeft) || is(pressed, sdl::ActRight)) {
    // Next character nobody else in the room uses.
    const int dir = is(pressed, sdl::ActLeft) ? -1 : 1;
    for (int step = 1; step < 4; ++step) {
      const int c = wrap(session_->color() + dir * step, 4);
      const auto& players = session_->lobby().players;
      const bool taken = std::any_of(players.begin(), players.end(), [&](const LobbyPlayer& p) { return p.color == c && p.id != session_->playerId(); });
      if (!taken) { session_->selectCharacter(c); break; }
    }
  }
  if (is(pressed, sdl::ActConfirm)) {
    if (index_ == 1) session_->startMatch();
    else if (index_ == 2) { session_->leave(); message_.clear(); backHome(); }
  }
  return MenuResult::Stay;
}

void OnlineMenu::updateBot(Profile& profile) {
  const double now = now_();
  if (page_ == Page::Name) { profile.prefs.name = name_ = config_.bot == "create" ? "Bot anfitrião" : "Bot convidado"; page_ = Page::Home; }
  if (page_ == Page::Home && !session_ && now >= botNextTry_) {
    botNextTry_ = now + 2000;
    EntryRequest e = baseEntry(profile);
    if (config_.bot == "create") {
      e.action = "create";
      e.visibility = "open";
      connect(std::move(e));
    } else if (rooms_ && !rooms_->rooms().rooms.empty()) {
      e.action = "join";
      e.room = rooms_->rooms().rooms.front().code;
      connect(std::move(e));
    }
  }
  if (page_ == Page::Lobby && session_->isHost() && static_cast<int>(session_->lobby().players.size()) >= config_.botPlayers) session_->startMatch();
}

bool OnlineMenu::frame(double dt, Vec2 input, GameState& out) {
  if (!session_) return false;
  const double now = now_();
  session_->update(now);
  return session_->frame(now, dt, input, out);
}

const std::string& OnlineMenu::localId() const {
  static const std::string none;
  return session_ ? session_->playerId() : none;
}
bool OnlineMenu::reconnecting() const { return session_ && session_->status() == SessionStatus::Reconnecting; }
bool OnlineMenu::closed() const { return !session_ || session_->status() == SessionStatus::Closed; }
std::optional<std::string> OnlineMenu::takeNotice() { return session_ ? session_->takeNotice() : std::nullopt; }
void OnlineMenu::choosePower(const std::string& id) { if (session_) session_->choosePower(id); }
void OnlineMenu::reroll() { if (session_) session_->reroll(); }
void OnlineMenu::special() { if (session_) session_->special(); }
void OnlineMenu::dash(Vec2 direction) { if (session_) session_->dash(direction); }
void OnlineMenu::signal(const char* kind, std::optional<Vec2> at) { if (session_) session_->signal(kind, at); }

void OnlineMenu::leave() {
  if (session_) {
    message_ = session_->status() == SessionStatus::Closed ? session_->failure() : "";
    session_->leave();
  }
  backHome();
}

// ---- Rendering -------------------------------------------------------------------------------

void OnlineMenu::renderMenu(sdl::BatchRenderer& b, float width, float height, float s) {
  b.rect(0, 0, width, height, kBg);
  b.text(width * 0.5f, height * 0.05f, "Jogar online", 52 * s, kGold, Align::Center);
  switch (page_) {
    case Page::Name:
    case Page::Code: renderText(b, width, height, s); break;
    case Page::Home: renderHome(b, width, height, s); break;
    case Page::Create: renderCreate(b, width, height, s); break;
    case Page::Connecting:
      b.text(width * 0.5f, height * 0.4f, "Conectando ao servidor...", 28 * s, kText, Align::Center);
      b.text(width * 0.5f, height * 0.4f + 40 * s, url_, 18 * s, kMuted, Align::Center);
      b.text(width * 0.5f, height * 0.4f + 80 * s, "B / Esc: cancelar", 18 * s, kMuted, Align::Center);
      break;
    case Page::Lobby: renderLobby(b, width, height, s); break;
    case Page::Playing: break;
  }
  if (!message_.empty()) b.textWrapped(width * 0.5f - 400 * s, height - 90 * s, 800 * s, message_, 18 * s, kDanger, 2, Align::Center);
}

void OnlineMenu::renderHome(sdl::BatchRenderer& b, float width, float height, float s) {
  const float w = 620 * s, x = width * 0.5f - w * 0.5f;
  float y = height * 0.17f;
  b.text(x, y, "Salas abertas", 24 * s, kText);
  y += 36 * s;
  const int rooms = visibleRooms();
  const char* status = !rooms_ ? "Servidor indisponível."
                     : !rooms_->error().empty() ? rooms_->error().c_str()
                     : !rooms_->loaded() ? "Buscando salas..."
                     : rooms == 0 ? "Nenhuma sala aberta agora. Crie uma!" : nullptr;
  if (status) { b.text(x + 20 * s, y + 8 * s, status, 20 * s, kMuted); y += 48 * s; }
  char label[96], value[96];
  for (int i = 0; i < rooms; ++i, y += 48 * s) {
    const auto& r = rooms_->rooms().rooms[static_cast<std::size_t>(i)];
    std::snprintf(label, sizeof label, "%s · %s", r.code.c_str(), r.host.c_str());
    std::snprintf(value, sizeof value, "%d/4 · %s%s", r.count, campaignTitle(r.campaign), r.running ? " · em jogo" : "");
    row(b, x, y, w, s, index_ == i, label, value);
  }
  y += 12 * s;
  const char* actions[4] = {"Criar sala", "Entrar com código", "Nome", "Voltar"};
  for (int i = 0; i < 4; ++i, y += 48 * s) row(b, x, y, w, s, index_ == rooms + i, actions[i], i == 2 ? name_.c_str() : nullptr);
  if (rooms_ && rooms_->loaded() && rooms_->error().empty()) {
    std::snprintf(label, sizeof label, "Servidor: %d/%d salas em uso", rooms_->rooms().used, rooms_->rooms().max);
    b.text(width * 0.5f, y + 8 * s, label, 16 * s, kMuted, Align::Center);
  }
}

void OnlineMenu::renderCreate(sdl::BatchRenderer& b, float width, float height, float s) {
  const float w = 620 * s, x = width * 0.5f - w * 0.5f;
  float y = height * 0.15f;
  b.text(width * 0.5f, y, "Criar sala", 28 * s, kText, Align::Center);
  y += 44 * s;
  row(b, x, y, w, s, index_ == 0, "Visibilidade", open_ ? "Aberta (aparece na lista)" : "Fechada (só com o código)");
  y += 46 * s;
  row(b, x, y, w, s, index_ == 1, "Ritual", kCampaignTitles[campaign_]);
  y += 46 * s;
  for (int i = 0; i < 6; ++i, y += 46 * s)
    row(b, x, y, w, s, index_ == 2 + i, kCurses[i].title, curses_[static_cast<std::size_t>(i)] ? "Ativa" : "—",
        curses_[static_cast<std::size_t>(i)] ? kDanger : 0);
  row(b, x, y, w, s, index_ == 8, "Criar sala");
  y += 46 * s;
  row(b, x, y, w, s, index_ == 9, "Voltar");
  if (index_ >= 2 && index_ < 8) b.text(width * 0.5f, y + 56 * s, kCurses[index_ - 2].desc, 18 * s, kMuted, Align::Center);
}

void OnlineMenu::renderText(sdl::BatchRenderer& b, float width, float height, float s) {
  const bool code = entry_.kind() == TextKind::RoomCode;
  b.text(width * 0.5f, height * 0.16f, code ? "Código da sala" : "Seu nome", 30 * s, kText, Align::Center);
  const float bw = 460 * s, bx = width * 0.5f - bw * 0.5f, by = height * 0.24f;
  b.rect(bx, by, bw, 56 * s, kPanel);
  b.frame(bx, by, bw, 56 * s, 2 * s, entry_.valid() ? kGold : kMuted);
  const bool blink = std::fmod(now_() / 500.0, 2.0) < 1.0;
  const std::string shown = entry_.value() + (blink ? "_" : " ");
  b.text(width * 0.5f, by + 10 * s, shown, 30 * s, kText, Align::Center);
  const int cols = TextEntry::kColumns;
  const float key = 54 * s, gap = 6 * s, gx = width * 0.5f - (cols * key + (cols - 1) * gap) * 0.5f, gy = by + 80 * s;
  for (int i = 0; i < entry_.keyCount(); ++i) {
    const float kx = gx + static_cast<float>(i % cols) * (key + gap), ky = gy + static_cast<float>(i / cols) * (key + gap);
    const bool sel = i == entry_.cursor();
    b.rect(kx, ky, key, key, sel ? kSel : kPanel);
    if (sel) b.frame(kx, ky, key, key, 3 * s, kGold);
    b.text(kx + key * 0.5f, ky + 14 * s, entry_.keyLabel(i), 20 * s, sel ? kText : kMuted, Align::Center);
  }
  b.text(width * 0.5f, height - 130 * s, "Teclado: digite · Enter confirma · Esc volta   |   Controle: A tecla · B volta", 16 * s, kMuted, Align::Center);
}

void OnlineMenu::renderLobby(sdl::BatchRenderer& b, float width, float height, float s) {
  const auto& lobby = session_->lobby();
  const float w = 620 * s, x = width * 0.5f - w * 0.5f;
  float y = height * 0.15f;
  char line[160];
  std::snprintf(line, sizeof line, "Sala %s · %s", session_->room().c_str(), lobby.visibility == "open" ? "aberta" : "fechada");
  b.text(width * 0.5f, y, line, 30 * s, kText, Align::Center);
  y += 40 * s;
  std::snprintf(line, sizeof line, "%s · %d maldição(ões)", campaignTitle(lobby.campaign), static_cast<int>(lobby.curses.size()));
  b.text(width * 0.5f, y, line, 20 * s, kMuted, Align::Center);
  y += 44 * s;
  for (const auto& p : lobby.players) {
    b.rect(x, y, w, 40 * s, kPanel);
    b.rect(x, y, 6 * s, 40 * s, native::playerColor(p.color));
    std::snprintf(line, sizeof line, "%s (%s)%s%s%s", p.name.c_str(), kCharacterNames[std::clamp(p.color, 0, 3)],
                  p.id == lobby.hostId ? " · anfitrião" : "", p.id == session_->playerId() ? " · você" : "", p.connected ? "" : " · desconectado");
    b.text(x + 20 * s, y + 8 * s, line, 20 * s, p.connected ? kText : kMuted);
    y += 46 * s;
  }
  y += 12 * s;
  row(b, x, y, w, s, index_ == 0, "Personagem", kCharacterNames[std::clamp(session_->color(), 0, 3)]);
  y += 48 * s;
  row(b, x, y, w, s, index_ == 1, session_->isHost() ? "Iniciar partida" : "Aguardando o anfitrião iniciar");
  y += 48 * s;
  row(b, x, y, w, s, index_ == 2, "Sair da sala");
}

} // namespace arcana::online
```

- [ ] **Step 5: Rodar os testes**

Run: `tools/docker/build-all.sh host`
Expected: `online_menu: ok`; todos os testes passam.

- [ ] **Step 6: Commit**

```bash
git add platforms/online tests/online_menu_tests.cpp CMakeLists.txt
git commit -m "feat(online): room list, create, code/name entry and lobby pages

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 15: Ligar tudo no `arcana_desktop`

**Files:**
- Modify: `platforms/desktop/main.cpp`

**Interfaces:**
- Consumes: `OnlineMenu`, `WsTransport`, `Frontend::setOnline/wantsTextInput`, `TextInput`, `ActSignal*`.
- Produces: flags `--server URL`, `--insecure-ws`, `--online-bot create|join`, `--bot-players N`; linha final `online_players=<n> online_time=<s>` quando `--online-bot`.

- [ ] **Step 1: Includes e argumentos** — depois de `#include "frontend.hpp"`:

```cpp
#if ARCANA_HAS_ONLINE
#include "online_menu.hpp"
#include "ws_transport.hpp"
#endif
```

Em `struct Args`, acrescente:

```cpp
  std::string server;     // online server URL (default: the public one; overrides pref.server)
  bool insecureWs{};      // allow plain ws:// to a remote host (tests)
  std::string onlineBot;  // headless online bot: "create" or "join"
  int botPlayers{2};      // --online-bot create: start once this many joined
```

Em `parseArgs`, antes do `else if (k == "--size")`:

```cpp
    else if (k == "--server") a.server = next("");
    else if (k == "--insecure-ws") a.insecureWs = true;
    else if (k == "--online-bot") a.onlineBot = next("create");
    else if (k == "--bot-players") a.botPlayers = std::atoi(next("2").c_str());
```

- [ ] **Step 2: Teclado em modo texto e sinais** — troque a assinatura e o começo de `readInput` por:

```cpp
void readInput(InputFrame& frame, const std::vector<SDL_GameController*>& pads, bool textMode) {
  frame = {};
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  auto& p0 = frame.pads[0];
  p0.connected = true;
  if (textMode) {
    // Typing a name or room code: letters are text, so the keyboard only backs out (Enter and
    // Backspace arrive as events, see the main loop). Gamepads keep driving the on-screen grid.
    if (k[SDL_SCANCODE_ESCAPE]) p0.held |= ActCancel;
  } else {
    p0.x = static_cast<float>((k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]));
    p0.y = static_cast<float>((k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) - (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]));
    if (p0.x != 0 && p0.y != 0) { p0.x *= 0.7071f; p0.y *= 0.7071f; }
    if (k[SDL_SCANCODE_SPACE]) p0.held |= ActSpecial;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) p0.held |= ActDash;
    if (k[SDL_SCANCODE_ESCAPE]) p0.held |= ActPause;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE]) p0.held |= ActConfirm;
    if (k[SDL_SCANCODE_BACKSPACE]) p0.held |= ActCancel;
    if (k[SDL_SCANCODE_R]) p0.held |= ActAlt;
    if (k[SDL_SCANCODE_F3]) p0.held |= ActDebug;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) p0.held |= ActUp;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) p0.held |= ActDown;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) p0.held |= ActLeft;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) p0.held |= ActRight;
    // Online signals (src/main.js SIGNAL_KEYS, plus C for "look there").
    if (k[SDL_SCANCODE_Q]) p0.held |= ActSignalHere;
    if (k[SDL_SCANCODE_E]) p0.held |= ActSignalHelp;
    if (k[SDL_SCANCODE_X]) p0.held |= ActSignalDanger;
    if (k[SDL_SCANCODE_C]) p0.held |= ActSignalLook;
  }
```

(remova as linhas antigas equivalentes; o bloco de gamepads continua igual).

- [ ] **Step 3: Criar o `OnlineMenu`** — depois de `if ((!headless || args.forceAudio) && !args.mute && audio.init()) frontend->setAudio(&audio);`:

```cpp
#if ARCANA_HAS_ONLINE
  {
    online::OnlineMenuConfig oc;
    if (!args.server.empty()) { oc.url = args.server; oc.forceUrl = true; }
    oc.allowInsecure = args.insecureWs;
    oc.bot = args.onlineBot;
    oc.botPlayers = args.botPlayers;
    const std::string ca = findAsset("", {"cacert.pem"});
    if (ca.empty()) std::fprintf(stderr, "cacert.pem não encontrado: usando os certificados do sistema\n");
    oc.transport = [ca] { return std::make_unique<online::WsTransport>(ca); };
    frontend->setOnline(std::make_unique<online::OnlineMenu>(std::move(oc)));
  }
#endif
```

E, antes de `auto frontend = std::make_unique<Frontend>(options);`, depois de `if (args.benchSeconds > 0) { ... }`:

```cpp
  if (!args.onlineBot.empty()) { options.autoplay = true; options.startOnline = true; }
```

- [ ] **Step 4: Texto no laço principal** — antes do `while (running) {`:

```cpp
  SDL_StopTextInput(); // SDL starts with text input on; only the name/room-code pages want it
  TextInput typed;
```

No começo do corpo do `while (running) {`, antes do `SDL_Event e;`:

```cpp
    const bool textMode = frontend->wantsTextInput();
    if (textMode != (SDL_IsTextInputActive() == SDL_TRUE)) textMode ? SDL_StartTextInput() : SDL_StopTextInput();
    typed = {};
```

Dentro do `while (SDL_PollEvent(&e)) {`, acrescente:

```cpp
      if (textMode && e.type == SDL_TEXTINPUT) typed.typed += e.text.text;
      if (textMode && e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_BACKSPACE) ++typed.backspaces;
        if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) typed.submit = true;
        if (e.key.keysym.sym == SDLK_v && (e.key.keysym.mod & KMOD_CTRL)) {
          if (char* clip = SDL_GetClipboardText()) { typed.typed += clip; SDL_free(clip); }
        }
      }
```

Troque `readInput(input, pads);` por:

```cpp
    readInput(input, pads, textMode);
    input.text = typed;
```

- [ ] **Step 5: Ritmo do bot e relatório** — troque `if (headless) dt = 1.0 / 60.0;` por:

```cpp
    if (headless && args.onlineBot.empty()) dt = 1.0 / 60.0; // deterministic pacing for screenshots/benchmarks
```

e, logo depois de `SDL_RenderPresent(renderer);` no fim do laço:

```cpp
    if (headless && !args.onlineBot.empty()) SDL_Delay(16); // online bots play in real time
```

Depois do laço (antes do `if (frames > 60) {` do relatório):

```cpp
  if (!args.onlineBot.empty())
    std::printf("online_players=%d online_time=%.1f\n", static_cast<int>(frontend->state().players.size()), frontend->state().time);
```

- [ ] **Step 6: Compilar e testar à mão contra um servidor local**

Run:
```bash
tools/docker/build-all.sh host windows
```
Expected: host (testes) e windows ok.

Teste manual (Windows, dois terminais):
```bash
npm --prefix ../meu-game run server
```
```bash
dist/windows/arcana-survivors.exe --server ws://localhost:8081
```
Expected: "Jogar online" no título → pede o nome → Tela Online com "Nenhuma sala aberta agora"; criar sala leva ao lobby; abrir `http://localhost:5173/?server=ws://localhost:8081` (com `npm --prefix ../meu-game run dev`) mostra a sala na lista da web; entrar pela web e iniciar pelo PC → os dois jogam na mesma partida, Q/E/X/C mostram sinais nos dois.

- [ ] **Step 7: Commit**

```bash
git add platforms/desktop/main.cpp
git commit -m "feat(desktop): online menu, keyboard text entry, signal keys and --online-bot

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```

---

### Task 16: Teste ponta a ponta, release e documentação

**Files:**
- Create: `tools/docker/online-e2e.sh`
- Modify: `tools/docker/build-all.sh` (alvo `online-e2e`)
- Create: `tools/release/third-party-notices.sh`, `THIRD_PARTY_NOTICES.txt`
- Modify: `tools/release/make-release.sh`, `tools/release/make-release.ps1`, `tools/docker/windows-build.sh`
- Modify: `README.md`, `docs/superpowers/specs/2026-09-22-online-coop-design.md`

- [ ] **Step 1: Script do teste ponta a ponta** — `tools/docker/online-e2e.sh`:

```bash
#!/usr/bin/env bash
# Cross-play smoke test: the Node server of ../meu-game in a container and two headless C++ bots
# (arcana_desktop --online-bot) in the same room. Needs a host build first (build-all.sh host).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GAME="$(cd "$ROOT/../meu-game" && pwd)"
cd "$ROOT"
MOUNT="$ROOT"; GAME_MOUNT="$GAME"
if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == MSYS* ]]; then
  export MSYS_NO_PATHCONV=1
  MOUNT="$(pwd -W)"; GAME_MOUNT="$(cd "$GAME" && pwd -W)"
fi
[[ -x build-linux/arcana_desktop ]] || { echo "rode antes: tools/docker/build-all.sh host"; exit 2; }
[[ -d "$GAME/node_modules/ws" ]] || { echo "rode antes: npm --prefix ../meu-game install"; exit 2; }

NET=arcana-e2e
cleanup() { docker rm -f arcana-e2e-server >/dev/null 2>&1 || true; docker network rm "$NET" >/dev/null 2>&1 || true; }
trap cleanup EXIT
cleanup
docker network create "$NET" >/dev/null
docker run -d --name arcana-e2e-server --network "$NET" -v "$GAME_MOUNT":/game -w /game -e PORT=8081 node:22-slim node server/server.js >/dev/null
sleep 2

bot() {
  docker run --rm --network "$NET" -v "$MOUNT":/src -w /src -e SDL_AUDIODRIVER=dummy arcana-host \
    build-linux/arcana_desktop --online-bot "$1" --bot-players 2 --server ws://arcana-e2e-server:8081 --insecure-ws --frames 1800 --mute
}
bot create > build-linux/e2e-host.log 2>&1 &
HOST=$!
sleep 4
bot join > build-linux/e2e-guest.log 2>&1
wait "$HOST"

grep -h online_players build-linux/e2e-host.log build-linux/e2e-guest.log || true
if grep -q "online_players=2" build-linux/e2e-host.log && grep -q "online_players=2" build-linux/e2e-guest.log; then
  echo "online-e2e: ok"
else
  echo "online-e2e: FALHOU (logs em build-linux/e2e-*.log)"
  docker logs arcana-e2e-server | tail -20
  exit 1
fi
```

Em `tools/docker/build-all.sh`, no comentário do topo acrescente `#   tools/docker/build-all.sh online-e2e      # cross-play com o servidor do ../meu-game (precisa do host antes)`, e antes do caso `*)`:

```bash
    online-e2e)
      if bash tools/docker/online-e2e.sh; then RESULT[$target]="ok  dois bots C++ jogaram no servidor do meu-game"
      else RESULT[$target]="FALHOU"; fi
      ;;
```

e troque a mensagem do caso `*)` para listar `online-e2e` também.

- [ ] **Step 2: Rodar o ponta a ponta**

Run: `tools/docker/build-all.sh host online-e2e`
Expected: `online-e2e: ok`. Se falhar com erro de handshake de compressão nos logs, troque em `ws_transport.cpp` `WebSocketPerMessageDeflateOptions(true)` por `WebSocketPerMessageDeflateOptions(false)` e anote no commit; o protocolo continua funcionando sem compressão.

- [ ] **Step 3: Avisos de terceiros** — `tools/release/third-party-notices.sh`:

```bash
#!/usr/bin/env bash
# Regenerates THIRD_PARTY_NOTICES.txt from the license files of the libraries CMake fetched for the
# online client. Run after a host build: tools/release/third-party-notices.sh [build-linux/_deps]
set -euo pipefail
DEPS="${1:-build-linux/_deps}"
{
  echo "Arcana Survivors usa as bibliotecas abaixo no modo online (PC). Os textos das licenças seguem."
  for lib in "IXWebSocket 11.4.6|ixwebsocket-src/LICENSE.txt" "Mbed TLS 3.6.4|mbedtls-src/LICENSE" \
             "zlib 1.3.1|zlib-src/LICENSE" "nlohmann/json 3.12.0|nlohmann_json-src/LICENSE.MIT"; do
    printf '\n==== %s ====\n\n' "${lib%%|*}"
    cat "$DEPS/${lib##*|}"
  done
} > THIRD_PARTY_NOTICES.txt
echo "THIRD_PARTY_NOTICES.txt atualizado"
```

Run: `MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W)":/src -w /src arcana-host bash tools/release/third-party-notices.sh`
Expected: `THIRD_PARTY_NOTICES.txt atualizado`, arquivo com as quatro seções.

Inclua o arquivo nos pacotes: em `tools/docker/windows-build.sh`, depois de `cp assets/cacert.pem "$BUNDLE/assets/"`, `cp THIRD_PARTY_NOTICES.txt "$BUNDLE/"`; em `tools/release/make-release.sh`, depois de `cp assets/fonts/* "$STAGE/$NAME-linux-x86_64/assets/fonts/"`:

```bash
cp assets/cacert.pem "$STAGE/$NAME-linux-x86_64/assets/"
cp THIRD_PARTY_NOTICES.txt "$STAGE/$NAME-linux-x86_64/"
```

e em `tools/release/make-release.ps1`, depois de `Copy-Item assets\fonts\* "$Stage\assets\fonts\"`:

```powershell
Copy-Item assets\cacert.pem "$Stage\assets\"
Copy-Item THIRD_PARTY_NOTICES.txt "$Stage\"
```

- [ ] **Step 4: README** — na seção "O que tem no jogo", depois do item de co-op local:

```markdown
- **Co-op online no PC (Windows e Linux)**, no mesmo servidor da versão web: dá para jogar junto com
  quem está no navegador. Lista de salas abertas, sala fechada com código, lobby com troca de
  personagem, ritual e maldições, entrada com a partida em andamento e reconexão automática.
  Sinais para os aliados: Q (venham aqui), E (ajuda), X (cuidado), C (olhem ali); no controle,
  segure X/Y e aperte uma direção.
```

Na tabela de controles, acrescente a linha `| Sinais (online) | — | — | — | Q / E / X / C |`. Em "Ferramentas de desenvolvimento", acrescente:

```bash
./arcana_desktop --server ws://localhost:8081              # online contra um servidor local do meu-game
./arcana_desktop --online-bot create --server ws://host:8081 --insecure-ws --frames 1800  # bot online headless
```

e explique em uma frase: "O servidor padrão é `wss://vps65228.publiccloud.com.br/ws`; `server=` no `profile.ini` troca o padrão e `--server` tem prioridade. Sem TLS (`ws://`) só para `localhost`, ou com `--insecure-ws`." Em "Testes (`ctest`)", acrescente: `online_*` (protocolo contra fixtures geradas pelo meu-game, mensagens, interpolação/predição, sessão/reconexão, entrada de texto, telas online) e o alvo `online-e2e` do `build-all.sh`. Na tabela de arquitetura, acrescente `platforms/net/` (rede online sem SDL: transporte, protocolo, sessão) e `platforms/online/` (telas online, só PC).

- [ ] **Step 5: Atualizar o spec** — em `docs/superpowers/specs/2026-09-22-online-coop-design.md`:
  - em "Rede e sincronização", item 1: "Thread de rede: recebe texto e enfileira; o parse do JSON e o `decodeSnapshot` rodam no `Session::update()` da thread principal (≈0,3 ms por snapshot no PC)";
  - remova o parágrafo "Campos que o renderer usa e o snapshot não traz ... preservados por id" e escreva "O renderer só usa campos presentes no snapshot (conferido), então nada é preservado entre snapshots";
  - em "Arquitetura", troque "O `arcana_core` não muda." por "O `arcana_core` só ganha acréscimos: `Event::player/name` e `playerMovement()`.";
  - troque `online_screens.{hpp,cpp}` em `platforms/sdl/` por `platforms/online/online_menu.{hpp,cpp}` (o Makefile do Switch compila `platforms/sdl/*.cpp` por wildcard);
  - em "Integração com o Frontend", troque "`name=`" por "`pref.name=`" e acrescente "servidor alternativo em `pref.server=`".

- [ ] **Step 6: Build completo**

Run: `tools/docker/build-all.sh host switch vita windows psp online-e2e`
Expected: todos `ok`.

- [ ] **Step 7: Commit**

```bash
git add tools THIRD_PARTY_NOTICES.txt README.md docs/superpowers/specs/2026-09-22-online-coop-design.md
git commit -m "build(online): cross-play e2e test, third-party notices, docs

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
graphify update .
```
