# LumeBSD 2.0 "Farol"

Sobre o FreeBSD 15.1. O primeiro update grande: uma cara própria, janelas em abas como no BeOS, atributos nos arquivos, o Wi-Fi sem Python e as ferramentas de construção em Rust.

## A cara do LumeBSD

- **O farol em todo lugar**: no carregador de boot, na tela de abertura da sessão, na tela de login, no botão do menu, em Configurações › Sobre este sistema e no menu de ajuda. Nenhum logo do KDE à vista.
- **Decoração de janelas Papel**: a barra de título fina, de papel e tinta, com o fechar à esquerda.
- O papel de parede novo, da 2.0.

## Abas e janelas empilhadas, como no BeOS

- **Ctrl+Meta+E** empilha a janela ativa com a anterior: as duas ficam no mesmo lugar, com uma aba para cada uma na barra de título. Clicar na aba troca de janela.
- **Ctrl+Meta+PgDn** e **Ctrl+Meta+PgUp** passam de aba em aba; **Ctrl+Meta+Shift+E** separa a janela da pilha.
- Ao trocar de aba, a janela aparece no lugar e no tamanho da anterior (maximizada, se ela estava); as outras da pilha ficam guardadas.

## Atributos nos arquivos

- Como no BeOS, um arquivo carrega atributos gravados nele mesmo: **etiquetas**, **nota** (de 0 a 5 estrelas), **comentário**, **origem** e os seus (autor, cliente, projeto...). Vão junto quando o arquivo é copiado, movido ou guardado na máquina do tempo.
- No Dolphin, botão direito › **Atributos…** mostra e edita, também em vários arquivos de uma vez. O Dolphin mostra as mesmas etiquetas, nota e comentário no painel de informações (F11).
- **Procurar por atributos** (no menu, ou botão direito numa pasta): `etiquetas=trabalho nota>=4 cliente~acme`. A procura salva vira um arquivo `.consulta`, como no BeOS: dois cliques nele procuram de novo.
- No terminal: `lume atributo contrato.pdf etiquetas+=trabalho nota=4` e `lume atributo procurar etiquetas=trabalho`.

## Wi-Fi sem Python

- Um ícone de rede nosso, na bandeja, no lugar do networkmgr: o cabo e o Wi-Fi, as redes por perto com o sinal, conectar com a senha (também WPA3) e esquecer uma rede. Não pede a senha do administrador para conectar.

## Por dentro

- **O lume-motor é uma biblioteca** (`liblume-motor`, com os cabeçalhos em `/usr/include/lume`), para outros programas desenharem com ele. O papel do motor e o do filtro e-ink saem das mesmas contas: são o mesmo papel.
- **As ferramentas de construção em Rust**: o pendrive, a .iso, o pacote de atualização e a compilação dos apps saem de um programa só, `lume-montar`, no lugar dos scripts em Python.

# LumeBSD 1.5.1 "Farol"

Sobre o FreeBSD 15.1. O tema escuro, e máquinas virtuais e computadores com BIOS.

## Papel noite: o tema escuro

- O Papel visto à noite: o fundo é a tinta (um preto quente, não o preto puro) e as letras são o papel, um pouco apagado para não ofuscar. Os ícones, o terminal, os apps do LumeBSD, o papel de parede com o farol à noite e o filtro e-ink acompanham.
- `lume tema escuro` troca na hora, só para você e sem senha; `lume tema claro` volta. Também em Configurações do Sistema › Aparência › Tema global: **Papel noite (LumeBSD)**.
- No tema escuro, o filtro e-ink usa uma tinta mais funda e um papel menos claro, sozinho.

## Máquinas virtuais e computadores com BIOS

- **O disco instalado dá boot em BIOS**: computadores antigos e máquinas virtuais com SeaBIOS, o padrão do virt-manager e do GNOME Boxes. Antes, a instalação terminava sem erro e o disco não ligava.
- **A tela é o console principal**: numa máquina virtual com porta serial, o boot e o assistente do primeiro boot iam para a serial, e a tela ficava só com um cursor. Agora vão para a tela, em BIOS e em UEFI, e com acentos: em BIOS, a máquina virtual mostrava "não" como "n?o".

Para quem já instalou: a correção do boot vale para instalações novas (a partição do BIOS é criada pelo instalador).

# LumeBSD 1.5 "Farol"

Sobre o FreeBSD 15.1. Papel e tinta de verdade, um motor de renderização próprio, AMD, a .iso e o começo dos programas do Mac.

## Papel e tinta de verdade

- **Grão do papel**: o filtro e-ink desenha as fibras do papel. O tom varia de leve e as bordas das letras seguem as fibras, como tinta em papel. As fibras ficam paradas na tela, como num leitor de e-ink.
- **A tinta assenta**: quando a tela muda, o conteúdo novo aparece sobre o anterior numa fração de segundo, sem o piscar de uma tela de e-ink de verdade. Fotos, vídeos e tela cheia mudam na hora. Precisa de placa de vídeo.
- `lume tema tinta grao 0-100` e `lume tema tinta viscosidade MS` ajustam os dois.

## O motor de renderização

- **lume-motor**: o protótipo do motor de renderização vetorial do LumeBSD. Desenha direto na placa de vídeo, sem X11 nem Wayland, no modo de maior taxa do monitor (180 Hz, se o painel tiver). As letras e os desenhos são calculados pelas equações das curvas em cada pixel: nítidos em qualquer zoom, sem bitmaps de fonte. Tem o papel e a tinta que assenta. Para ver, num console de texto: `lume-motor`.

## Hardware e instalação

- **Placas AMD**: Radeon e o vídeo dos processadores Ryzen, com o driver certo para cada geração. Notebooks Ryzen + NVIDIA usam o vídeo do Ryzen.
- **Processadores AMD**: o microcódigo e o sensor de temperatura certos.
- **.iso**: além do pendrive (`.img`), uma `.iso` para DVD, máquina virtual ou pendrive.

## Programas do Mac, o começo

- O LumeBSD já carrega programas do macOS e traduz as chamadas que eles fazem ao sistema: por enquanto, só os mais simples, que não usam as bibliotecas da Apple. Para ver um rodando: `lume compat mac testar`. Os programas comuns ainda não rodam; o caminho está na Ajuda.

## Da 1.4

### A tela de papel

- **Filtro e-ink**: todas as janelas em tons de papel e tinta. Numa tela comum, conteúdo claro e sem cores fortes se lê melhor sob luz forte ou ao sol, sem aumentar o brilho.
- **Fotos e vídeos ficam com cor**: o filtro sai sozinho de visualizadores de fotos, players de vídeo, editores de imagem e de qualquer janela em tela cheia.
- **Ctrl+Meta+T** liga e desliga o filtro; **Ctrl+Meta+Y** tira ou põe só na janela ativa; `lume tema tinta 60` deixa mais leve.
- **Atalhos de teclado do KDE funcionando**: Alt+Tab, Alt+F4, Meta+W (visão geral) e os outros atalhos das janelas ficavam sem tecla, porque o KWin começava antes do serviço de atalhos. Agora o serviço sobe quando o KWin pede.
- **Animações das janelas** sem movimento, como no papel: o **refresh de e-ink** (a janela aparece escura por um instante e o conteúdo surge de uma vez) ou o **esmaecer** em tons de cinza (`lume tema animacao esmaecer`).

### Programas do LumeBSD, de cara nova

- A loja tem uma aparência própria, de papel e tinta: títulos com serifa, um índice como o sumário de um livro e os kits como capítulos. Não depende mais do tema do KDE.
- A busca acha por todas as palavras, em qualquer ordem ("microsoft excel").

### Mais programas de outros sistemas

- **Programas do Windows de 32 bits**: muitos instaladores e programas antigos. Na primeira vez, o LumeBSD baixa a parte de 32 bits do Wine para a sua pasta (sem senha); depois, dois cliques bastam.
- **Programas do Mac**: ainda não rodam no FreeBSD, mas dois cliques num `.dmg` mostram o que usar no lugar, com a loja procurando pelo nome. A loja conhece os programas da Apple: Pages, Numbers, Keynote, iMovie, Final Cut, GarageBand, Logic, Fotos, Xcode e outros.
- Novos no catálogo: Okular, digiKam, darktable, Ardour, MuseScore e KDevelop.

### Também desde a 1.3

- **Programas do LumeBSD**: mais de 100 programas em português, com kits por área (Uso geral, Exatas e engenharia, Sociais aplicadas) e a busca pelo programa que você usava ("matlab" mostra o Octave).
- **Ajuda do LumeBSD** no menu (`lume ajuda`), agora com a tela de papel e os programas do Mac.
