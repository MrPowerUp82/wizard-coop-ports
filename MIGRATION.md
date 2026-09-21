# Mapa da migração JS/Web → C++ nativo

| Origem | Destino | Situação |
|---|---|---|
| `server/game.js` | `src/game.cpp` | Migrado: estado, tick, fases, progressão, drops, revive e transições |
| `server/balance.js` | `include/arcana/constants.hpp` + core | Migrado, incluindo caps de entidades |
| `server/phases.js` | `src/data.cpp` | Migrado: 6 fases, inimigos e bosses |
| `server/campaign.js` | `src/data.cpp` + core | Migrado: quick/classic/endless |
| `server/curses.js` | core | Migrado |
| `server/meta.js` | core + `src/profile.cpp` | Migrado |
| `server/powers.js` | data + core | Migrado: oferta, reroll e evoluções |
| `server/movement.js` | core | Migrado: movimento/dash |
| `server/combat.js` | core | Migrado: dano, loot, combos, Fênix, splitter |
| `server/enemies.js` | core + `native/spatial_grid.hpp` | Migrado; separação usa grid fixo sem heap |
| `server/spatial.js` | `include/arcana/native/spatial_grid.hpp` | Reintroduzido em C++ com armazenamento fixo; colisão de projéteis e separação |
| `server/bosses.js` | core | Migrado |
| `server/weapons.js` | core | Migrado; consultas mais caras de projéteis já usam spatial grid |
| `server/objectives.js` | core | Migrado |
| `server/encounters.js` | core | Migrado |
| arrays dinâmicos de entidades | `native::StaticVector` | Migrados nos containers quentes para evitar realloc/erase de heap |
| loop variável do browser | `native::FixedStep` | 60 Hz determinístico nos runtimes nativos |
| `src/render.js`/Canvas | `native::RenderQueue` + backend da plataforma | Separado do core; Vita backend já implementado, Switch deko3d é próxima etapa |
| sprites WebP/recolorização | `assets/native_atlas.png` | Pré-baked; sem WebP/recolor em runtime de console |
| `platforms/vita` JS/QuickJS | `platforms/vita/native` | Novo runtime C++/VitaSDK/libvita2d; fonte pronta para cross-build/hardware test |
| `platforms/switch` nx.js | `platforms/switch/native_probe` | Core libnx nativo implementado; renderer deko3d ainda pendente |
| `server/server.js` | `server/main.cpp` | Servidor Boost.Beast existente para multiplayer |
| `src/net.js` | `client/network.cpp` | Cliente desktop C++ existente |
| `src/wallet.js`/`localStorage` | `src/profile.cpp` | Persistência nativa em arquivo |
| PWA/Vite/HTML/CSS | — | Removidos do runtime dos consoles |

## Mudanças intencionais para performance

1. **Capacidade fixa no hot path.** Entidades usam armazenamento contíguo pré-alocado; exceder o cap falha sem realocar.
2. **Spatial grid voltou para o C++.** O primeiro port C++ tinha simplificado essa parte para O(N²); isso foi revertido porque os testes sintéticos mostraram custo significativo com 180 inimigos/320 tiros.
3. **Render é desacoplado.** `arcana_core` não conhece SDL, Vita2D nem deko3d. Ele produz estado; a camada nativa produz comandos de render.
4. **Assets são preparados antes da execução.** O atlas elimina WebP decode, recolor e Canvas baking durante a partida.
5. **Vita/Switch não dependem do cliente SDL.** SDL2 é ferramenta de desktop, não requisito arquitetural do port.
6. **Rede não fica no caminho crítico local.** O protocolo JSON existente continua útil para depuração/multiplayer; uma serialização binária compacta pode ser adicionada depois, sem contaminar o core local.

## O que ainda falta para um port de console completo

O foco desta etapa foi CPU/memória e a fundação nativa. Ainda faltam: medir em hardware real; batching de sprites/efeitos no Vita; backend deko3d do Switch; HUD/menu/Códex/Grimório nativos; áudio; persistência específica de cada console; multiplayer otimizado; e empacotamento/teste final `.vpk`/`.nro` no toolchain oficial/homebrew correspondente.

Veja `NATIVE_PORT_STATUS.md` para números, comandos de teste e próximas prioridades.
