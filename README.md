# Arcana Survivors — port nativo em C++

Port em C++20 do **Arcana Survivors** (antes *wizard-coop*, um survivors-like cooperativo feito em
JavaScript) para **Nintendo Switch**, **PS Vita** e **PSP**, com um build de **PC** para
desenvolvimento. Os ports em JavaScript (nx.js / QuickJS) tinham quedas grandes de FPS; aqui não há
JavaScript, HTML nem Canvas no jogo: simulação, renderer e áudio são nativos.

**Baixe a versão mais recente em [Releases](https://github.com/MrPowerUp82/wizard-coop-ports/releases/latest).**

## Plataformas

| Plataforma | Arquivo | Instalação | Save |
|---|---|---|---|
| Nintendo Switch (CFW/Atmosphère) | `arcana-survivors-v<versão>-switch.nro` | Copie para `sd:/switch/` e abra pelo Homebrew Menu, de preferência em modo aplicativo (segure R ao abrir um jogo). | `sdmc:/switch/arcana-survivors/profile.ini` |
| PS Vita (HENkaku/Ensō) | `arcana-survivors-v<versão>-vita.vpk` | Instale pelo VitaShell. Title ID `ARCA00001`. | `ux0:data/arcana-survivors/profile.ini` |
| PSP (CFW) ou Vita com Adrenaline | `arcana-survivors-v<versão>-psp.cso` (ou `.iso`) | Copie para `ms0:/ISO/` (no Vita: `ux0:pspemu/ISO/`). Não roda em firmware original. | `ms0:/data/arcana-survivors/profile.ini` |
| Linux x86_64 (dev) | `arcana-survivors-v<versão>-linux-x86_64.tar.gz` | Precisa de `libsdl2`, `libsdl2-image` e `libsdl2-ttf`. Extraia e rode `./arcana_desktop`. | pasta de dados do usuário (o caminho aparece no terminal) |

O save é um arquivo de texto `chave=valor`, gravado de forma atômica (arquivo temporário + rename):
fechar o jogo ou acabar a bateria no meio não corrompe o progresso, e reinstalar não apaga nada.

## O que tem no jogo

- **Campanha completa:** seis reinos com hordas e chefes, rituais rápido, clássico e infinito,
  poderes, evoluções, combos, encontros (altar, mercador, santuário, ladrão), maldições e reviver.
- **Co-op local para até 4 jogadores** em tela compartilhada (Switch, Vita e PC). No Switch, cada
  jogador pode usar um Joy-Con na horizontal, um par de Joy-Cons, o modo portátil ou um Pro Controller.
- **Quatro personagens** (Azul, Vermelho, Verde e Roxo), cada um com o próprio tiro e especial; no
  co-op ninguém repete personagem.
- **Grimório (meta-progressão):** as moedas de cada partida compram os 15 upgrades permanentes do
  jogo web, incluindo os desbloqueios Arsenal (arma inicial), Segundo feitiço (especial alternativo)
  e Ritual infinito. Dá para redistribuir tudo e receber as moedas de volta.
- **Visual do cliente web portado:** chão por fase, atmosfera, poses animadas, efeito de cada
  especial, raios, familiar, números de dano, tremor e flash de tela, avisos de eventos.
- **Áudio sintetizado** como no navegador: 31 efeitos e trilha generativa que muda com o momento da
  partida (menu, horda, guardião, fúria, vitória, derrota). Nenhum arquivo de áudio.

## Controles

| Ação | Switch | Joy-Con na horizontal | Vita / PSP | PC |
|---|---|---|---|---|
| Mover | analógico / direcional | analógico | analógico / direcional | WASD / setas |
| Especial | A, R, ZR | SL ou botão da direita | ✕, R | Espaço |
| Esquiva | B, L, ZL | SR ou botão de baixo | ○, L | Shift |
| Trocar opções de poder | X, Y | botões de cima/esquerda | □, △ | R |
| Pausa | + / − | + ou − | Start | Esc |
| Overlay de desempenho | clique do analógico | clique do analógico | Select | F3 |

No menu, esquerda/direita troca o personagem; no co-op, cada jogador entra com A e sai com B.

## Build

Só é preciso ter o **Docker**: todos os toolchains (devkitPro, VitaSDK, PSPSDK e GCC) rodam em
containers, nada é instalado no sistema.

**Windows:** dê dois cliques em `build.cmd` ou rode no terminal (sem Git Bash, WSL ou ajuste de
política de execução):

```bat
build.cmd                 :: host, switch, vita
build.cmd psp             :: só alguns: host | switch | vita | psp | psp-probe
release.cmd               :: todos os alvos + dist\release\v<versão>\ (arquivos, SHA256SUMS, notas)
```

**Linux / macOS:**

```bash
tools/docker/build-all.sh                  # host, switch, vita
tools/docker/build-all.sh psp              # só alguns alvos
tools/release/make-release.sh              # release
```

| Alvo | Imagem | Saída em `dist/` |
|---|---|---|
| `host` | `arcana-host` (de `tools/docker/host.Dockerfile`) | testes + `linux/arcana_desktop` |
| `switch` | `devkitpro/devkita64` | `switch/arcana-survivors.nro` |
| `vita` | `vitasdk/vitasdk` | `vita/arcana-survivors-native.vpk` |
| `psp` | `pspdev/pspdev` + `arcana-host` | `psp/arcana-survivors.iso`, `.cso` e a pasta `ArcanaSurvivors/` (EBOOT) |
| `psp-probe` | `pspdev/pspdev` | `psp-probe/EBOOT.PBP` (mede a CPU do PSP, sem gráficos) |

O script de release se recusa a rodar com mudanças não commitadas, para que os hashes sempre
correspondam a um commit. As notas de destaque de cada versão ficam em `tools/release/NOTES.md`, e a
versão em `platforms/switch/sdl/Makefile` (`APP_VERSION`).

### Build manual (sem Docker)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARCANA_BUILD_SERVER=OFF -DARCANA_BUILD_NET=OFF -DARCANA_BUILD_CLIENT=OFF \
  -DARCANA_BUILD_TESTS=ON -DARCANA_BUILD_DESKTOP=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`ARCANA_BUILD_DESKTOP` precisa de SDL2, SDL2_image e SDL2_ttf. `-DARCANA_REAL_FLOAT=ON` compila a
simulação em `float`, como no PSP, para testá-la no PC.

## Ferramentas de desenvolvimento

O `arcana_desktop` tem opções para testar sem controle e sem tela:

```bash
./arcana_desktop --autoplay 4 --perf                       # 4 bots jogando, overlay de desempenho
./arcana_desktop --autoplay 2 --charged --frames 1800 \
                 --shots-every 20 --screenshot shots/s.png # capturas em série (headless)
./arcana_desktop --psp                                     # prévia do PSP: 480x272, UI compacta
./arcana_desktop --open-shop --profile teste.ini           # abre direto no Grimório com outro save
./arcana_desktop --audio-demo demo.wav                     # grava todos os sons e músicas num WAV
./arcana_desktop --bench 60                                # benchmark headless
```

Nos modos com bot, benchmark ou screenshot o save real nunca é tocado. No PSP, um `autoplay.txt` em
`ms0:/data/arcana-survivors/` liga o bot com o overlay (escreva `charged` dentro para especiais
contínuos).

Testes (`ctest`): `core` (regras da simulação), `native_hotpath` (zero alocações por frame depois do
aquecimento), `frontend` (seleção de personagem, trigonometria visual) e `meta` (save, loja, bônus,
depósito de moedas e redistribuição).

## Arquitetura

```
include/arcana/        API do core: estado, entidades, dados; real.hpp (double, ou float no PSP)
include/arcana/native/ StaticVector, fixed step, spatial grid, render queue (sem heap por frame)
src/                   simulação (game.cpp), dados das fases/poderes (data.cpp), save (profile.cpp)
platforms/sdl/         frontend compartilhado: renderer em lote, animações, mundo, HUD, menus, áudio
platforms/desktop/     main do PC (SDL2)
platforms/switch/sdl/  main do Switch (libnx + SDL2)
platforms/vita/sdl/    main do Vita (VitaSDK + SDL2/GXM)
platforms/psp/sdl/     main do PSP (PSPSDK + SDL2/GU); platforms/psp/probe: medidor de CPU
tools/docker/          builds em container; tools/release/: empacotamento da release
tools/assets/          geração do atlas, do chão por fase e da arte do XMB do PSP
tools/psp/make_cso.py  compressão ISO -> CSO com verificação
tests/                 testes automatizados
```

Decisões que sustentam o desempenho:

- **Memória fixa no caminho quente.** Entidades vivem em `StaticVector` pré-alocados; a simulação e
  a montagem do frame não alocam depois do aquecimento (há um teste que garante isso).
- **Renderer em lote.** Sprites, formas, texto e chão saem de uma única textura; um frame são ~6–8
  chamadas `SDL_RenderGeometry`, mesmo com 180 inimigos. No PSP, a textura é uma página de 512×512.
- **Simulação a 60 Hz fixos**, independente da taxa de quadros.
- **Um frontend para todas as plataformas.** Cada console só tem seu `main.cpp` (inicialização,
  controles e caminhos de arquivo); o modo compacto adapta a interface à tela de 480×272 do PSP.
- **PSP em `float`.** O processador do PSP não tem `double` em hardware (~100× mais lento no probe).
  O core usa `arcana::real`, que é `double` nas outras plataformas; 200 partidas de bot em cada modo
  deram o mesmo resultado dentro do ruído estatístico.

Detalhes, medições e histórico de cada etapa em [`NATIVE_PORT_STATUS.md`](NATIVE_PORT_STATUS.md);
o mapa de arquivos JS → C++ em [`MIGRATION.md`](MIGRATION.md).

## Estado e limitações

- O PSP roda a 60 fps no PPSSPP, mas ainda não foi medido em aparelho real.
- No PSP não há co-op (o aparelho tem um controle só), e o flash branco de dano virou uma tinta
  vermelha (falta espaço na textura de 512×512).
- Ainda faltam o Códex, o desafio diário, as maldições no menu e o multiplayer online. O servidor
  WebSocket (`server/`) e o cliente de rede (`client/`) da primeira etapa continuam no repositório,
  mas não fazem parte dos builds de console.
- `platforms/vita/native` (vita2d) e `platforms/switch/native_probe` são os protótipos da primeira
  etapa, mantidos como referência.

## Regra do port

Uma mudança só conta como otimização de console quando reduz custo medido, alocações, largura de
banda de memória ou draw calls. Estar em C++ não garante FPS por si só.
