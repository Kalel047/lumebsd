# Roteiro: LumeBSD 2.0 e 2.x

A lista de Kalel para o primeiro update grande, e as decisões tomadas
(novembro de 2026).

## 2.0

| Item | Como |
|---|---|
| **Identidade visual própria** | o farol do LumeBSD no carregador de boot, no splash, no SDDM, na tela de bloqueio, no botão do menu e em "Sobre o sistema"; nenhum logo do KDE à vista |
| **Wi-Fi sem Python** | um app nosso em C++/Qt na bandeja, no lugar do networkmgr (a única peça visível em Python ao nosso alcance) |
| **Abas e empilhamento de janelas** | como no BeOS e no Haiku (lá ficam no servidor de janelas, o app_server, não no kernel); aqui, fase 1, como plugin do KWin |
| **Atributos de arquivo na interface** | como no BFS: atributos estendidos no ZFS e no UFS (`extattr`), com os nomes do KDE (o painel de informações do Dolphin, F11, mostra e edita as etiquetas, a nota e o comentário; na lista, as colunas); `lume atributo` em C para ler, gravar e procurar; a janela de atributos no botão direito do Dolphin; consultas salvas em arquivos `.consulta`, que procuram de novo com dois cliques |
| **lume-motor e lume-tinta juntos** | o motor vira biblioteca (`liblume-motor`, `/usr/include/lume`); as contas do papel (`papel.glsl`) e as cores (`papel.h`) são uma fonte só para o motor e o filtro do KWin |
| **Ferramentas de build sem Python** | `ferramentas/*.py` reescritas em Rust: um programa, `lume-montar`, com subcomandos (`ferramentas/montar`) |

Situação: os seis itens saíram na 2.0 (13 de novembro de 2026).

## 2.1: o que a 2.0 deixou pela metade

| Pendência da 2.0 | Como ficou |
|---|---|
| Trocar o tema desfazia as pilhas de janelas | no X11 o KWin recria todos os efeitos a cada `reconfigure`; o `lume_abas` que sai guarda os grupos numa propriedade do processo do KWin, e o que entra os retoma |
| As colunas Etiquetas e Comentário do Dolphin ficavam vazias | o Dolphin só lê os atributos de cada arquivo com a indexação do Baloo "ligada": fica ligada, sem pasta nenhuma para indexar |
| A procura não se atualizava | consultas ao vivo, como no BeOS: `lume atributo procurar -v` vigia as pastas com o inotify do kernel (que avisa quando um atributo muda) e diz o que entra e o que sai; a janela de procura usa esse modo |

Sobre o Python: o sistema do LumeBSD (os comandos `lume`, o rc, os apps) não
usa Python. Ele chega à máquina como dependência de pacotes de terceiros (o
KWin, o plasma-workspace, o glib, o LibreOffice) e do networkmgr; tirá-lo de
vez pediria recompilar o KDE, e ele voltaria com os kits de ciências.

## 2.2: o editor de texto

O **Páginas** (`app/lume-paginas`, C++/Qt sem moc): a folha na tela, com a
paginação do próprio `QTextDocument`; o painel Formatar e Folha; os modelos
(ABNT, plano de aula, relatório, carta, currículo); leitores e gravadores
próprios de `.odt` e `.docx` (o zip pelo zlib da base, o XML pelo
`QXmlStreamReader`), testados contra o LibreOffice nos dois sentidos; PDF
com os números de página.

## 2.3: a planilha

Os **Números** (`app/lume-numeros`, C++/Qt sem moc): o analisador e o
cálculo das fórmulas próprios (com os nomes do Excel em português e em
inglês, e o dialeto do ODF), a grade sobre um `QTableView` com os gráficos
por cima, os modelos (orçamento, notas, juros, fluxo de caixa,
indicadores), leitores e gravadores próprios de `.xlsx` e `.ods` (com os
gráficos), testados contra o LibreOffice nos dois sentidos; CSV e PDF.

## 2.4: as apresentações

O **Palco** (`app/lume-palco`, C++/Qt sem moc): o slide num
`QGraphicsView` (o texto se escreve direto nele, com o mesmo
`QTextDocument` que desenha), os temas do LumeBSD, os leiautes, as formas,
as imagens, os gráficos (gravados como gráficos de verdade, com a planilha
embutida no `.pptx` e o objeto de gráfico no `.odp`), as tabelas, as
animações de entrada e saída (por parágrafo, em ordem, um passo por
clique), o vídeo (Qt Multimedia), as transições, o modo de apresentação e a
tela do apresentador com as notas e o próximo clique, os modelos (aula,
seminário, conjuntura econômica, reunião), leitores e gravadores próprios
de `.pptx` e `.odp` (com a herança do mestre e do leiaute do PowerPoint),
testados contra o LibreOffice e o python-pptx; PDF.

## 2.5: o escritório junto

O gráfico copiado nos Números cola no Páginas e no Palco com os dados (um
formato próprio na área de transferência, `application/x-lume-grafico`, e
uma imagem para os outros programas). O desenho e os gráficos de verdade
do Office e do ODF ficam em `app/comum`, comuns aos três apps; o Páginas
grava o gráfico no `.docx` (com a planilha embutida) e no `.odt` (o objeto
de gráfico) e o lê dos dois. E o que arquivos de verdade do Word, do Excel e
do PowerPoint mostraram: listas numeradas que continuam, a linha de
cabeçalho que repete, os formatos de data do Excel, as margens internas das
caixas de texto.

## 2.x


- **Office com a estética do iWork**: o editor de texto saiu na 2.2 (o
  Páginas, `app/lume-paginas`), a planilha na 2.3 (os Números,
  `app/lume-numeros`) e as apresentações na 2.4 (o Palco,
  `app/lume-palco`); na 2.5, o gráfico dos Números colado no Páginas e no
  Palco com os dados.
- **MediaKit de baixa latência**: um servidor de mídia nosso sobre o OSS, com
  threads de tempo real e um grafo de nós como o do BeOS (alvo: uns 5 ms).
- **Replicants**: pedaços vivos de um app na área de trabalho ou em outro app.
- **Compositor próprio** sobre o lume-motor (fase 2 das janelas): abas,
  Replicants e a tinta de forma nativa.
- **lume-macho**: um módulo de kernel para o Mach-O (como o Linuxulator) e o
  dyld e a libSystem do Darling, para rodar programas do Mac de verdade.
