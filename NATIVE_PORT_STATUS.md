# Native Port Status — Stage 1

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
