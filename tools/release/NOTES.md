Versão nativa em C++ do **Arcana Survivors** (antes *wizard-coop*) para **Nintendo Switch**, **PS Vita** e **PSP**, com builds para Windows e Linux. Ela substitui os ports em JavaScript (nx.js / QuickJS), que tinham quedas grandes de FPS.

## Novidades da v0.8.0

- **Dois novos personagens jogáveis (O Desenvolvedor e o Guardião da Aurora):**
  - **O Desenvolvedor (Personagem Secreto):** Na tela de título, aperte sete vezes o botão de trocar opções do Jogador 1 (R no teclado, X/Y no controle, □/△ no Vita e no PSP). Ele possui 5× de vida, 4× de dano, velocidade de ataque dobrada, 3 projéteis iniciais, +35% de velocidade e 12 de armadura. Dispara *Código-fonte* (atravessa até 6 inimigos, desacelera e explode em área). Seu especial *Reescrever realidade* apaga projéteis e atinge inimigos num raio de 600 com 24× o dano, curando 50% e recarregando passivamente em 10 s. Com *Segundo feitiço*, *Restauração do sistema* elimina instantaneamente todos os inimigos no mapa (incluindo elites e chefes), cura aliados em 100% e protege por 5 s.
  - **Guardião da Aurora (Recompensa do Ritual Clássico):** Desbloqueado ao vencer os seis reinos no modo Clássico (solo, co-op local ou online). Mago dourado com auréola solar. Possui 150 de vida (+50%), +35% de dano, +10% de velocidade, 3 de armadura e intervalo de ataque 15% menor. Dispara a *Lança da aurora* (atravessa 2 alvos). Seu especial *Alvorada* causa uma explosão solar em 300 de raio com 6× de dano e apaga projéteis. Com *Segundo feitiço*, *Coroa da aurora* dispara 12 lanças solares radiais com 3× de dano.
- **Atlas nativo expandido para 7 colunas:** novos sprites de magos e projéteis gerados com recolorização HSL precisa.
- **Otimização de empacotamento no PSP (512×512):** refino do conjunto de caracteres Latin-1 essenciais na interface em português, permitindo que o novo atlas e todas as fontes caibam na página compacta sem perda visual.
- **Compatibilidade total no co-op local e no multiplayer online para PC com cross-play.**

## Da v0.7.0

- **Co-op online no PC (Windows e Linux), junto com quem joga no navegador.** O jogo se conecta ao mesmo servidor da versão web, então as salas são as mesmas: dá para jogar com amigos no PC e no navegador na mesma partida.
  - **Salas abertas:** a lista mostra as salas com vaga, quem é o anfitrião, quantos jogadores tem e qual ritual.
  - **Sala fechada:** criada com um código de 6 letras, que só entra quem receber.
  - **Lobby:** cada um escolhe seu personagem (sem repetir) e o anfitrião começa a partida. Quem chegar depois entra com a partida em andamento.
  - **Caiu a internet?** O jogo tenta voltar sozinho por cerca de 30 segundos e você volta para a mesma partida, no mesmo personagem.
  - **Sinais para os aliados:** Q (venham aqui), E (preciso de ajuda), X (cuidado) e C (olhem ali). No controle, segure X/Y e aperte uma direção. Quem está longe vê uma seta na borda da tela.
  - **Digitação com controle:** um teclado na tela para o nome e o código da sala, pensado para o Steam Deck. No teclado do PC dá para digitar e colar normalmente.
- **As moedas da partida online vão para o seu Grimório**, como no jogo offline, e as melhorias que você já comprou valem online.
- **Conexão protegida:** a ligação com o servidor usa TLS com verificação de certificado (o pacote de certificados vai junto, em `assets/cacert.pem`). O arquivo `THIRD_PARTY_NOTICES.txt` traz as licenças das bibliotecas usadas nessa parte.
- **Servidor:** por padrão o jogo usa o servidor oficial. Para apontar para outro, use `--server wss://endereco/ws` ou a linha `pref.server=` no `profile.ini`.
- **Switch, Vita e PSP seguem só com o co-op local**, sem nenhuma mudança de desempenho: nada de rede entra nesses builds.

## Da v0.6.2

- **Dicas de controle corretas em cada plataforma:** antes as dicas da tela mostravam sempre A/B/X/Y. No PC (Windows/Linux) elas agora acompanham o dispositivo usado por último: no teclado aparecem Enter, Backspace e R, e ao usar um controle voltam para A/B/X/Y. No PS Vita e no PSP aparecem X, O e Quadrado. Vale para o menu, a escolha de poderes, o Grimório, a tela de fim de partida e os Créditos.

## Da v0.6.1

- **Abertura com o selo MrPowerUp82:** um selo arcano (círculo de runas, triângulo, crescente e estrela) se desenha na tela antes do menu, com "MrPowerUp82 apresenta". Dura cerca de 2 segundos e qualquer botão pula. O selo é desenhado pelo próprio renderer, sem textura nova, então fica nítido do PSP (480×272) ao 4K.
- **Tela de Créditos:** novo item no menu principal, com autoria, plataformas, fonte e licença.
- **Assinatura no menu:** "Desenvolvido por MrPowerUp82 · 2026", discreta no canto da tela.
- O menu principal se ajusta quando todos os desbloqueios aparecem (até 7 itens), sem invadir os cards dos jogadores nem a borda da tela do PSP.

## Da v0.6.0

- **PSP:** port completo em ISO/CSO (um arquivo só) ou pasta com EBOOT. A simulação roda em `float`, porque o PSP não tem `double` em hardware; 200 partidas de bot comparadas com a versão `double` dão o mesmo resultado dentro do ruído. Interface refeita para 480×272 (single-player), texturas numa página de 512×512 e áudio sintetizado a 22 kHz. No PPSSPP roda a 60 fps com 90+ inimigos. Ainda não foi medido em hardware real.
- **Grimório (meta-progressão):** as moedas de cada partida ficam guardadas e compram os 15 upgrades permanentes do jogo web, entre eles Vigor, Potência, Canalização, Fênix, Pacto familiar e os desbloqueios Arsenal, Segundo feitiço e Ritual infinito. Também dá para redistribuir tudo e receber as moedas de volta.
- **Save por console:** Grimório, personagens escolhidos, ritual, arma inicial, especial e som ficam salvos. Os arquivos ficam em `sdmc:/switch/arcana-survivors/profile.ini` no Switch e em `ux0:data/arcana-survivors/profile.ini` no Vita. A gravação é atômica, então um crash ou uma queda de energia não corrompe o save.
- **Windows:** build para PC (Windows 10/11 x86_64) em `.zip`, com o mesmo frontend do Linux: extraia e rode `arcana-survivors.exe`. Teclado e controles compatíveis com SDL2, co-op local para até 4 jogadores.
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
| Windows 10/11 x86_64 | `arcana-survivors-v{{VERSION}}-windows-x86_64.zip` | Extraia a pasta inteira e rode `arcana-survivors.exe` (não precisa instalar nada). O executável não é assinado: se o SmartScreen avisar, clique em "Mais informações" → "Executar assim mesmo". O save fica em `%APPDATA%\MrPowerUp82\ArcanaSurvivors\`. |
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

- Ainda faltam o Códex, o desafio diário, as maldições e o multiplayer online nos consoles (Switch, Vita e PSP seguem com co-op local; o online está disponível no PC).
- A orientação do analógico de um Joy-Con sozinho na horizontal ainda não foi validada em todos os firmwares. Se ele girar errado, avise na issue.
- No PSP, o flash branco de dano virou uma tinta vermelha (falta espaço na textura de 512×512) e o co-op local não existe (o aparelho tem um controle só).
- O build do Windows foi testado só sob Wine (não em Windows real); se algo falhar (janela, controle, som), avise na issue.
- O volume segue o do navegador, que é baixo por projeto.

Se o FPS cair, abra o overlay de desempenho numa fase cheia ou num chefe e mande os números (`fps`, `frame`, `sim`, `mundo`, `draw`) numa issue.
