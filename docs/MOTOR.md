# O motor de renderização do LumeBSD

A especificação ("Motor de Renderização Vetorial para LumeBSD", de Kalel)
pede um motor que desenhe direto na placa de vídeo, com geometria vetorial
analítica, a simulação de uma tela de e-ink (papel, tinta que assenta) e
quadros a 180 Hz. Este documento diz o que já existe, como funciona, o que
foi medido e o caminho até o motor desenhar o desktop.

Começou na 1.5 por duas frentes:

1. **No desktop de hoje**: o filtro e-ink do KWin (`app/lume-tinta`)
   ganhou o grão de papel e a viscosidade da tinta. Isso o usuário já vê.
2. **O protótipo `lume-motor`** (`app/lume-motor`): um programa separado,
   que desenha sem X11 nem Wayland, pelo DRM/KMS, com o pipeline da
   especificação inteiro.

## O pipeline do lume-motor

```
Primitiva declarativa da aplicação (caminhos de curvas, texto)
  │  cena.c: tudo vira curva quadrática monótona em y
  ▼
Anel de curvas na GPU (SSBO; escrita sem sincronizar, sem bitmaps)
  ▼
[GPU] Geometria analítica: um retângulo por caminho; o fragment shader
      resolve a equação de cada curva na linha do pixel
  ▼
[GPU] Compute shader: as fibras do papel (uma vez por resolução)
  ▼
[GPU] Fragment shader: papel + tinta (bordas pelas fibras)
  ▼
[GPU] Fragment shader: viscosidade (o quadro novo assenta sobre o anterior)
  ▼
DRM/KMS: page flip no vblank, no modo de maior taxa do painel
```

### 1. Vetorial analítico, sem atlas de fontes

- A aplicação descreve caminhos (`motor_cena_mover`, `motor_cena_linha`,
  `motor_cena_quad`, `motor_cena_cubica`) e texto (`motor_cena_texto`). As letras saem do contorno da
  fonte, pelo FreeType, como curvas: **não há bitmap de letra nem atlas**.
- Tudo vira curva quadrática de Bézier **monótona em y** (linhas com o
  controle no meio; quadráticas divididas no extremo; cúbicas aproximadas
  por 2 a 16 quadráticas). Assim cada curva cruza uma linha de pixels no
  máximo uma vez.
- Na GPU, cada caminho é um retângulo (desenho instanciado). Para cada
  pixel, o fragment shader resolve `y(t) = y` em cada curva do caminho e
  soma os cruzamentos à direita (regra do número de voltas, não-nula), com a
  **cobertura horizontal exata**: a fração do pixel à esquerda do cruzamento.
  Na vertical, quatro sublinhas. É o método das curvas resolvidas por pixel
  (da família do Loop-Blinn e do Slug), sem tesselação.
- Resultado: as letras e os desenhos ficam nítidos em qualquer zoom e em
  qualquer densidade de tela; o mesmo desenho de um documento impresso.
- Memória: a página da demonstração tem 8.035 curvas em 266 caminhos, 251 KB
  na GPU. Um bitmap da tela em 1280×720 teria 3,6 MB; em 4K, 32 MB.

### 2. Papel

Um **compute shader** gera, uma vez por resolução, as fibras de celulose:
ruído de valor alongado nas duas direções e um grão fino, presos ao pixel
da tela. O papel varia de tom com elas, e as bordas da tinta (os meios-tons)
avançam ou recuam por elas: bordas orgânicas, sem a rigidez do pixel.

### 3. Viscosidade da tinta

Dois históricos (pingue-pongue). A cada quadro:
`novo = antes + (agora − antes) × (1 − e^(−Δt/τ))`, com τ = 35 ms por padrão.
Uma mudança assenta em cerca de 100 ms, sem piscar (o e-ink de verdade
pisca preto e branco para limpar). Depois de 6τ sem mudança, um quadro
entra inteiro e o motor para de redesenhar. O histórico é lido pelo pixel
(`texelFetch` em `gl_FragCoord`), sem depender da orientação das texturas.

### 4. DRM/KMS

`saida.c` abre `/dev/dri/cardN`, escolhe o monitor ligado e, na resolução
preferida dele, **o modo de maior taxa de atualização** (180 Hz num painel
de 180 Hz), cria a superfície no GBM, o contexto EGL com OpenGL ES 3.1, e
troca os quadros com `drmModePageFlip` no vblank. Sem X11, sem Wayland, sem
compositor no meio. Ao sair, devolve o modo que estava na tela.

O mesmo motor roda sem tela (EGL surfaceless), para testes e para gerar
imagens: `lume-motor --png PASTA`.

## O que foi medido

| | |
|---|---|
| Página, zoom de 8×, troca de parágrafo | funcionam (quadros em `docs/motor/`) |
| Tinta assentando | some em ~110 ms, sem piscar |
| Memória da cena | 251 KB de curvas (contra 3,6 MB de um bitmap em 720p) |
| Tempo de quadro | **não medido em placa de vídeo**: os testes rodam no llvmpipe (a GPU emulada pela CPU), 42 ms por quadro em 1280×720, o que não diz nada sobre uma placa de verdade |

A meta da especificação (quadro em 5,5 ms, 180 Hz) depende da placa e do
painel; o `lume-motor` mostra a taxa do modo escolhido e o tempo médio e
máximo de cada quadro, para medir num computador de verdade:

```
lume video testar        # o driver de vídeo carregado
# num console de texto (Ctrl+Alt+F2), sem o X naquele terminal:
lume-motor
```

## O que ainda não é

- **O desktop não usa o motor.** Os programas de hoje (Firefox, LibreOffice,
  a loja) desenham pelo X11 ou pelo Wayland; um motor que não passe por eles
  precisa, no fim, fazer o papel de servidor gráfico. O caminho realista:
  1. o motor como **compositor Wayland** (com a wlroots ou a Smithay): as
     janelas dos programas viram texturas, e o motor desenha a moldura, o
     painel, o texto do sistema e o papel e a tinta por cima;
  2. os programas do próprio LumeBSD (a loja, as atualizações, a ajuda)
     desenhando pelo motor, pela primitiva declarativa;
  3. só então tirar o X11 da sessão.
- **Latência de entrada**: o motor ainda não lê teclado nem mouse (sai com
  Enter). Medir a latência de ponta a ponta precisa da entrada pelo evdev e
  de um painel de verdade.
- **Viscosidade por região**: hoje o quadro inteiro assenta junto; o certo é
  só a área que mudou (um vídeo dentro de uma página, por exemplo).
- **Desempenho**: cada pixel percorre todas as curvas do caminho. Para
  caminhos grandes, dividir o caminho em faixas (como o Slug faz) deixa isso
  proporcional às curvas daquela faixa.
- **Wayland no filtro do KDE**: a viscosidade do filtro existe só no KWin do
  X11 (a sessão do LumeBSD); o KWin do Wayland 6.7 mudou a API de desenhar
  a tela.

## A biblioteca

Desde a 2.0 o motor é uma biblioteca do sistema, para outros programas
desenharem com ele:

- `/usr/lib/liblume-motor.so.1` (e o `liblume-motor.so` para ligar);
- os cabeçalhos em `/usr/include/lume`: `motor.h` (a cena, a fonte, o
  motor), `saida.h` (a tela pelo DRM/KMS, ou PNG sem tela) e `papel.h` (as
  cores do papel e da tinta, de dia e à noite);
- todas as funções públicas começam com `motor_`.

```sh
cc -I/usr/include/lume -o meu meu.c -llume-motor -lGLESv2 -lm
```

As contas do papel (as fibras, o grão da tinta) ficam num arquivo só,
`app/lume-motor/papel.glsl`, embutido na construção nos shaders do motor e no
filtro e-ink do KWin (lume-tinta): o papel da tela e o do filtro são o mesmo.

## Arquivos

| Arquivo | O quê |
|---|---|
| `app/lume-motor/motor.h` | a API: cena, fonte, motor |
| `app/lume-motor/saida.h` | a saída: DRM/KMS ou PNG |
| `app/lume-motor/papel.h` | as cores do papel e da tinta |
| `app/lume-motor/papel.glsl` | as contas do papel, comuns com o lume-tinta |
| `app/lume-motor/cena.c` | caminhos, curvas monótonas, texto pelo FreeType |
| `app/lume-motor/motor.c` | os shaders e o pipeline (GLES 3.1) |
| `app/lume-motor/saida.c` | DRM/KMS (GBM, EGL, page flip) e sem tela (PNG) |
| `app/lume-motor/demo.c` | a demonstração (`lume-motor`) |
| `ferramentas/montar-motor.py` | compila para o FreeBSD e põe no sistema |
| `app/lume-tinta/tinta.cpp` | o filtro do KWin, com grão e viscosidade |
