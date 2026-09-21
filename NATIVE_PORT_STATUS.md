# Native Port Status

## Stage 3 — efeitos e animações do cliente web (2026-09-21)

- `platforms/sdl/animator.*`: port do `src/animation.js` com pools fixos (192 efeitos, 64 números,
  256 atores). Poses por ator (passada, respiração, squash ao levar dano, recuo ao conjurar, queda
  ao cair), faíscas, anéis, partículas (neve, brasas, folhas, estrelas, cura, fumaça), efeito de cada
  especial (nova, meteoro, espinhos, lua; e as variantes do "Segundo feitiço"), raios irregulares,
  golpes do familiar, sigilos de conjuração, "NÍVEL +", combos, convergência, afterimages da
  esquiva, fantasmas de morte, números de dano, screen shake, flash de tela e hit-stop.
- `platforms/sdl/world_renderer.*`: port do `renderWorld()` do `src/render.js`. Inclui chão em tiles
  por fase (`assets/terrain_tiles.png`, gerado por `tools/assets/bake_terrain.py` com o mesmo
  algoritmo do `terrain.js`), atmosfera, altar, mercador/santuário, zonas (granizo, escudo de
  chamas, vórtice, raízes, meteoro), runas, alertas, baú/ímã, gemas raras/épicas, rastros dos tiros,
  aviso de investida, marcadores de elite/ladrão/congelado, aura, órbitas, familiar animado, anel
  de especial pronto, reviver e setas para alvos fora da tela.
- HUD: anúncios e toasts dos eventos (port do `feedback.js`), vinheta vermelha de dano e de vida baixa.
- `BatchRenderer`: modo aditivo (o "lighter" do canvas), glow radial, silhuetas brancas para o
  flash de dano, elipses, arcos, polígonos, gradientes e texto com contorno. Tudo continua saindo de
  uma única textura; só a troca normal↔aditivo gera outra chamada de desenho (~6–8 por frame).
- Core: eventos `combo` levam reação e se é em equipe, `special` da lua leva a origem do salto,
  explosões `boom` levam o raio.
- Vita: o link usa uma cópia do script padrão com 8 KiB de folga antes de `.data`. Sem isso, o
  `vita-elf-create` falha com "segment 1 overlaps" quando o código termina perto de 64 KiB.

Captura headless para inspecionar efeitos: `./arcana_desktop --autoplay 4 --charged --frames 1800 --shots-every 20 --screenshot shots/s.png`.

## Stage 2 — jogo jogável em PC, Switch e Vita (2026-09-21)

Um único frontend nativo (`platforms/sdl/`) roda nos três alvos. Cada plataforma só tem um
`main.cpp` com init, caminhos de assets e leitura de input.

- **Renderer em lote (`BatchRenderer`)**: sprites, formas e texto saem de **uma textura**
  (atlas 128 px + texel branco + glifos pré-renderizados). Um frame inteiro vira normalmente
  **1 chamada `SDL_RenderGeometry`**, e os buffers são reservados uma vez só.
- **Frontend (`Frontend`)**: menu com escolha de ritual (rápido/clássico/infinito), entrada de
  até 4 jogadores locais, HUD por jogador, barra de chefe, escolha de poderes com reroll, pausa,
  fim de partida e overlay de desempenho (F3 / Select / clique dos analógicos).
- **Render queue** agora desenha também orbes, aura, runas, familiar, zonas, alertas, altar,
  encontros, raios em cadeia, explosões, jogadores caídos/reviver e barras de vida.
- **Co-op em tela compartilhada**: câmera centraliza e afasta o zoom; jogadores ficam "presos"
  à área visível (`leashPlayers`).
- **Switch**: `platforms/switch/sdl` → `arcana-survivors.nro` (libnx + SDL2/mesa). Input direto
  da HID (portátil, par, Joy-Con único na horizontal, Pro), mesmo mapeamento do port nx.js.
  Fonte do sistema (`pl`), atlas no romfs.
- **Vita**: `platforms/vita/sdl` → `arcana-survivors-native.vpk` (SDL2 com renderer "VITA gxm").
  Clocks 444/222/222/166, SceCtrl direto, fonte DejaVu embutida.
- **Toolchains via Docker** (nada instalado no Windows): ver "Builds" abaixo.

### Correção nos testes

`tests/*.cpp` usavam `assert` e eram compilados em Release (`NDEBUG`), então **nenhuma asserção
rodava**. Agora os testes forçam `#undef NDEBUG`. Com isso o teste de hot path revelou que o
cenário deixava o timeout de escolha de poder (15 s) disparar `applyPower` no meio da medição; o
teste agora mantém a janela de escolha aberta. A simulação + `buildRenderQueue` seguem com
**0 alocações** após o aquecimento.

### Builds (Docker)

```bash
# PC/Linux: core, testes e frontend SDL2 (também gera screenshots/benchmarks headless)
docker build -t arcana-host -f tools/docker/host.Dockerfile tools/docker
docker run --rm -v "$PWD":/src -w /src arcana-host bash tools/docker/host-build.sh
docker run --rm -v "$PWD":/src -w /src/build-linux arcana-host ./arcana_desktop --autoplay 2 --perf --frames 2400 --screenshot shots/play.png

# Switch -> platforms/switch/sdl/arcana-survivors.nro
docker run --rm -v "$PWD":/src -w /src devkitpro/devkita64 bash tools/docker/switch-build.sh

# Vita -> build-vita/arcana-survivors-native.vpk
docker run --rm -v "$PWD":/src -w /src vitasdk/vitasdk bash tools/docker/vita-build.sh
```

No Git Bash do Windows, prefixe com `MSYS_NO_PATHCONV=1` e use `"$(pwd -W)"` no lugar de `"$PWD"`.

### Controles

| Ação | Switch (Pro/portátil/par) | Joy-Con único | Vita | PC |
|---|---|---|---|---|
| Mover | analógico / direcional | analógico | analógico / direcional | WASD / setas |
| Especial | A, R, ZR | SL ou botão da direita | ✕, R | Espaço |
| Esquiva | B, L, ZL | SR ou botão de baixo | ○, L | Shift |
| Trocar opções | X, Y | botões de cima/esquerda | □, △ | R |
| Pausa | + / − | + ou − | Start | Esc |
| Desempenho | clique do analógico | clique do analógico | Select | F3 |

### O que medir no hardware

Ative o overlay de desempenho e anote `fps`, `frame` (CPU do frame), `sim`, `queue` e `draw`
com a tela cheia (fase 3+ ou chefe). Se `fps` < 60 com `frame` baixo, o gargalo é GPU/driver.
Aí vale considerar deko3d (Switch) ou reduzir overdraw.

### Pendências

1. Validar no hardware: FPS, orientação do analógico do Joy-Con único (`kRotateSingleJoyCon`),
   renderer GXM do Vita.
2. Terreno/tiles por fase (hoje: cor da fase + grade), animações e efeitos do cliente web.
3. Áudio (SDL_mixer está disponível nos dois SDKs).
4. Meta-progressão/loja/Códex e persistência por console (`profile.cpp`).
5. Trocar strings por IDs nas entidades quentes (tipo de inimigo, drop, sprite).

---

## Stage 1 — fundação nativa

Data: 2026-09-19

O objetivo desta branch é desempenho real em **PS Vita e Nintendo Switch**, removendo o runtime Web/JavaScript do gameplay. O desktop SDL2 continua apenas como ferramenta de desenvolvimento.

## O que já foi feito

### Core C++ voltado a console

- Simulação em C++20 compartilhada entre as plataformas.
- `FixedStep` a 60 Hz com limite de catch-up.
- Limites de entidades iguais ao jogo original: 180 inimigos, 320 tiros do jogador, 96 tiros inimigos, 220 drops, 12 hazards, 24 runas e 24 zonas.
- Containers quentes migrados para `StaticVector`, com capacidade fixa e armazenamento contíguo.
- Spatial hash grid fixo de 96 px, sem heap, usado na separação de inimigos e colisões de projéteis.
- Render queue de capacidade fixa, sem criar vetores temporários por frame.
- Estado grande e render queue ficam em armazenamento persistente nos runtimes de console, evitando colocar ~200 KB na stack da thread principal.
- Atlas nativo pré-processado em `assets/native_atlas.png`; o console não precisa decodificar/recolorir os WebP do jogo durante a partida.

### PS Vita

`platforms/vita/native/` contém um runtime VitaSDK/libvita2d que liga o core C++ diretamente ao Vita:

- sem QuickJS;
- sem HTML/CSS;
- sem Canvas/WebView;
- input direto por `SceCtrl`;
- fixed step de 60 Hz;
- sprites/círculos pela GPU através de libvita2d/GXM;
- clocks configurados como no port anterior;
- geração de `.vpk` via CMake/VitaSDK.

O código-fonte do target está pronto para cross-build, porém o ambiente usado para esta etapa não possui o VitaSDK instalado; portanto o `.vpk` ainda precisa ser compilado/testado em um ambiente VitaSDK e medido em hardware.

### Nintendo Switch

`platforms/switch/native_probe/` contém um `.nro` de diagnóstico libnx/AArch64 **sem renderer**. Ele executa o mesmo core C++ e mostra o tempo médio de `updateGame()` no próprio Switch.

O objetivo é separar duas causas de queda de FPS:

1. custo da simulação;
2. custo do renderer.

Depois de medir o probe, o próximo backend é o renderer deko3d com atlas único e batching de sprites. O ambiente usado nesta etapa não possui devkitPro/libnx, então o `.nro` precisa ser compilado no toolchain do Switch antes do teste em hardware.

## Medições desta etapa

As medições abaixo foram feitas no host de desenvolvimento. Elas **não representam FPS do Vita/Switch**; servem para verificar regressões e comparar algoritmos no mesmo ambiente.

- Separação com 180 inimigos: a troca de comparação O(N²) pelo spatial grid reduziu o teste sintético de aproximadamente **51 µs/tick para 30 µs/tick** no mesmo setup de benchmark (~42%).
- Cenário com 180 inimigos + 320 projéteis: o uso do grid na busca de colisões reduziu o teste sintético de aproximadamente **1,43 ms/tick para 0,21 ms/tick** no mesmo setup (~85%).
- Um teste de hot path, depois de 600 frames de aquecimento, executa mais 600 frames de simulação + construção da render queue e exige **0 novas alocações de heap**.

Uma build Release atual também inclui `arcana_native_bench`, para repetir benchmarks sem depender destes números registrados.

## Testes

No host:

```bash
cmake -S . -B build \
  -DARCANA_BUILD_SERVER=OFF \
  -DARCANA_BUILD_CLIENT=OFF \
  -DARCANA_BUILD_NET=OFF \
  -DARCANA_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/arcana_native_bench
```

Os testes relevantes são:

- `core`: regras básicas da simulação;
- `native_hotpath`: `StaticVector`, spatial grid e ausência de novas alocações no trecho quente após aquecimento.

## Próximas etapas de performance

1. Medir `native_probe` no Switch e runtime Vita no hardware real.
2. Implementar sprite batching do Vita para reduzir draw calls.
3. Implementar backend deko3d no Switch: atlas único, vertex/index buffers persistentes e poucos draw calls por camada.
4. Substituir strings usadas em entidades quentes (`Enemy::type`, drop kind, projectile sprite) por IDs/enums compactos.
5. Fazer profiling das armas orbit/aura/runes/zones e levar consultas restantes ao spatial grid apenas onde houver ganho medido.
6. Só depois reintroduzir HUD completo, efeitos, áudio e multiplayer, medindo o frame budget a cada etapa.

A meta desta estrutura é evitar que recursos visuais ou de rede voltem a contaminar o hot path da simulação.
