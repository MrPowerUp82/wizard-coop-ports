# Arcana Survivors — C++ Native Port

Esta é a migração do `wizard-coop` para C++20 com foco em **performance no PS Vita e Nintendo Switch**. O objetivo não é apenas traduzir JavaScript: o runtime Web deixa de participar do gameplay nos targets nativos.

O desktop SDL2 existe para desenvolvimento/testes. Vita e Switch usam/usarão backends próprios de plataforma sobre o mesmo `arcana_core`.

## Arquitetura

- `include/arcana/` — API pública, tipos e estruturas do core.
- `include/arcana/native/` — `StaticVector`, fixed-step, spatial grid e render queue sem alocações por frame.
- `src/game.cpp` — simulação: campanha, hordas, bosses, combate, XP, drops, revive, poderes, especiais, encontros e maldições.
- `src/data.cpp` — seis fases, inimigos, campanhas e árvore de poderes.
- `src/native/render_queue.cpp` — converte o estado do jogo em comandos de render independentes da plataforma.
- `assets/native_atlas.png` — atlas pré-processado para runtimes nativos.
- `platforms/vita/native/` — VitaSDK + libvita2d/GXM; sem QuickJS/Canvas/WebView.
- `platforms/switch/native_probe/` — probe libnx/AArch64 para medir o custo do core diretamente no Switch.
- `client/` — cliente SDL2 para desenvolvimento no PC.
- `server/` — servidor WebSocket C++/Boost.Beast.
- `tests/native_hotpath_tests.cpp` — valida containers/grid e ausência de heap no hot path após aquecimento.
- `tools/native_bench.cpp` — benchmarks sintéticos reproduzíveis.

- `platforms/sdl/` — frontend nativo compartilhado (renderer em lote, menu, HUD, co-op) usado por PC, Switch e Vita.
- `platforms/switch/sdl/`, `platforms/vita/sdl/`, `platforms/desktop/` — `main.cpp` de cada plataforma.
- `tools/docker/` — builds reproduzíveis (host, devkitPro, VitaSDK) sem instalar toolchains.

Leia `NATIVE_PORT_STATUS.md` para o estado exato do port, comandos de build (Docker) e controles.

## Build de tudo com Docker

Só é preciso ter o Docker. Um comando compila e testa todos os alvos e junta os resultados em `dist/`:

```powershell
.\tools\docker\build-all.ps1              # Windows (PowerShell)
```

```bash
tools/docker/build-all.sh                  # Linux, macOS ou Git Bash
tools/docker/build-all.sh switch vita      # só alguns alvos: host | switch | vita
```

| Alvo | Imagem | Saída |
|---|---|---|
| `host` | `arcana-host` (gerada de `tools/docker/host.Dockerfile`) | testes + `dist/linux/arcana_desktop` |
| `switch` | `devkitpro/devkita64` | `dist/switch/arcana-survivors.nro` |
| `vita` | `vitasdk/vitasdk` | `dist/vita/arcana-survivors-native.vpk` |

## Build do core no PC

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

Para Windows, o mesmo projeto pode ser aberto pelo Visual Studio/CMake. Boost e SDL2 só são necessários se os respectivos targets de rede/desktop forem habilitados.

## PS Vita nativo

O target em `platforms/vita/native` liga o gameplay C++ diretamente ao VitaSDK/libvita2d. Ele usa input nativo, fixed-step a 60 Hz e o atlas pré-baked.

```bash
vdpm install libvita2d
cmake -S platforms/vita/native -B build-vita \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j
```

Saída esperada: `build-vita/arcana-survivors.vpk`.

## Nintendo Switch nativo — probe de CPU

O primeiro target do Switch mede a simulação **sem renderer**, para descobrir no hardware quanto do frame é realmente gasto no core:

```bash
cd platforms/switch/native_probe
make -j
```

Saída esperada: `arcana_switch_probe.nro`. Ele mostra na tela o tempo médio de `updateGame()` e as contagens de entidades.

O backend gráfico do Switch será deko3d, sem nx.js/Canvas/JavaScript.

## Atlas nativo

O atlas pode ser regenerado a partir dos assets originais:

```bash
python3 tools/assets/bake_native_atlas.py
```

Isso evita decodificação/recolorização dos sprites no meio da partida.

## Servidor e cliente desktop

Eles continuam disponíveis para desenvolvimento, multiplayer e testes, mas não fazem parte do runtime mínimo dos consoles.

```bash
cmake -S . -B build-full -DARCANA_BUILD_SERVER=ON -DARCANA_BUILD_NET=ON -DARCANA_BUILD_CLIENT=ON
cmake --build build-full -j
```

## Regra do port

Uma mudança só é considerada otimização de console quando reduz custo medido, alocações, largura de banda de memória ou draw calls. Não vamos assumir que “estar em C++” por si só garante FPS.
