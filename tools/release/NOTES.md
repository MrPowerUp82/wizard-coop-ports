Versão nativa em C++ do **Arcana Survivors** (antes *wizard-coop*) para **Nintendo Switch**, **PS Vita** e **PSP**. Ela substitui os ports em JavaScript (nx.js / QuickJS), que tinham quedas grandes de FPS.

## Novidades da v0.6.0

- **PSP:** port completo em ISO/CSO (um arquivo só) ou pasta com EBOOT. A simulação roda em `float`, porque o PSP não tem `double` em hardware; 200 partidas de bot comparadas com a versão `double` dão o mesmo resultado dentro do ruído. Interface refeita para 480×272 (single-player), texturas numa página de 512×512 e áudio sintetizado a 22 kHz. No PPSSPP roda a 60 fps com 90+ inimigos. Ainda não foi medido em hardware real.
- **Grimório (meta-progressão):** as moedas de cada partida ficam guardadas e compram os 15 upgrades permanentes do jogo web, entre eles Vigor, Potência, Canalização, Fênix, Pacto familiar e os desbloqueios Arsenal, Segundo feitiço e Ritual infinito. Também dá para redistribuir tudo e receber as moedas de volta.
- **Save por console:** Grimório, personagens escolhidos, ritual, arma inicial, especial e som ficam salvos. Os arquivos ficam em `sdmc:/switch/arcana-survivors/profile.ini` no Switch e em `ux0:data/arcana-survivors/profile.ini` no Vita. A gravação é atômica, então um crash ou uma queda de energia não corrompe o save.
- **Seleção de personagem:** Azul, Vermelho, Verde ou Roxo para cada jogador, sem repetir no co-op.
- **Loadout:** com Arsenal, o jogador 1 escolhe a arma inicial; com Segundo feitiço, o especial alternativo.

## Destaques

- **Sem JavaScript no gameplay.** A simulação (hordas, 6 fases, chefes, poderes, evoluções, combos, encontros, maldições, co-op com reviver) roda em C++20. Os containers têm capacidade fixa e o hot path não aloca memória depois do aquecimento.
- **Renderer em lote:** sprites, formas, texto e chão saem de uma única textura, em ~6–8 chamadas de desenho por frame, mesmo com 180 inimigos na tela.
- **Visual do cliente web portado:** chão em tiles por fase, atmosfera, animação dos personagens, efeito de cada especial (nova, meteoro, espinhos, lua e as variantes), raios, familiar, combos, convergência, números de dano, tremor e flash de tela.
- **Áudio sintetizado:** os 31 efeitos e a trilha generativa (menu, horda, guardião, fúria, vitória, derrota), iguais aos do navegador, sem nenhum arquivo de áudio.
- **Co-op local para até 4 jogadores** em tela compartilhada. No Switch, cada jogador pode usar um Joy-Con na horizontal, um par de Joy-Cons, o modo portátil ou um Pro Controller.
- HUD por jogador, escolha de poderes com troca de opções, pausa (com liga/desliga do som), avisos de eventos e overlay de desempenho.

## Instalação

| Plataforma | Arquivo | Como instalar |
|---|---|---|
| Nintendo Switch (CFW/Atmosphère) | `arcana-survivors-v{{VERSION}}-switch.nro` | Copie para `sd:/switch/` e abra pelo Homebrew Menu. Use o modo aplicativo (segure R ao abrir um jogo) para ter memória total. |
| PSP (CFW) ou Vita com Adrenaline | `arcana-survivors-v{{VERSION}}-psp.cso` (ou `.iso`) | Copie para `ms0:/ISO/` (no Vita: `ux0:pspemu/ISO/`). O save fica em `ms0:/data/arcana-survivors/`. Não roda em firmware original. |
| PS Vita (HENkaku/Ensō) | `arcana-survivors-v{{VERSION}}-vita.vpk` | Instale pelo VitaShell. O Title ID é `ARCA00001`, então ele convive com o port JS antigo (`ARCS00001`). |
| Linux x86_64 (dev) | `arcana-survivors-v{{VERSION}}-linux-x86_64.tar.gz` | Precisa de `libsdl2`, `libsdl2-image` e `libsdl2-ttf`. Extraia e rode `./arcana_desktop`. |

## Controles

| Ação | Switch | Joy-Con na horizontal | Vita | PC |
|---|---|---|---|---|
| Mover | analógico / direcional | analógico | analógico / direcional (PSP igual) | WASD / setas |
| Especial | A, R, ZR | SL ou botão da direita | ✕, R | Espaço |
| Esquiva | B, L, ZL | SR ou botão de baixo | ○, L | Shift |
| Trocar opções de poder | X, Y | botões de cima/esquerda | □, △ | R |
| Pausa | + / − | + ou − | Start | Esc |
| Overlay de desempenho | clique do analógico | clique do analógico | Select | F3 |

## Limitações conhecidas

- Ainda faltam o Códex, o desafio diário, as maldições e o multiplayer online.
- A orientação do analógico de um Joy-Con sozinho na horizontal ainda não foi validada em todos os firmwares. Se ele girar errado, avise na issue.
- No PSP, o flash branco de dano virou uma tinta vermelha (falta espaço na textura de 512×512) e o co-op local não existe (o aparelho tem um controle só).
- O volume segue o do navegador, que é baixo por projeto.

Se o FPS cair, abra o overlay de desempenho numa fase cheia ou num chefe e mande os números (`fps`, `frame`, `sim`, `mundo`, `draw`) numa issue.
