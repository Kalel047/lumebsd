# Roteiro: LumeBSD 2.0 e 2.x

A lista de Kalel para o primeiro update grande, e as decisões tomadas
(novembro de 2026).

## 2.0

| Item | Como |
|---|---|
| **Identidade visual própria** | o farol do LumeBSD no carregador de boot, no splash, no SDDM, na tela de bloqueio, no botão do menu e em "Sobre o sistema"; nenhum logo do KDE à vista |
| **Wi-Fi sem Python** | um app nosso em C++/Qt na bandeja, no lugar do networkmgr (a única peça visível em Python ao nosso alcance) |
| **Abas e empilhamento de janelas** | como no BeOS e no Haiku (lá ficam no servidor de janelas, o app_server, não no kernel); aqui, fase 1, como plugin do KWin |
| **Atributos de arquivo na interface** | como no BFS: atributos estendidos no ZFS e no UFS (`extattr`), com os nomes do KDE (o Dolphin mostra as etiquetas, a nota e o comentário); `lume atributo` em C para ler, gravar e procurar; a janela de atributos no botão direito do Dolphin; consultas salvas em arquivos `.consulta`, que procuram de novo com dois cliques |
| **lume-motor e lume-tinta juntos** | o motor vira biblioteca (`liblume-motor`, `/usr/include/lume`); as contas do papel (`papel.glsl`) e as cores (`papel.h`) são uma fonte só para o motor e o filtro do KWin |
| **Ferramentas de build sem Python** | `ferramentas/*.py` reescritas em Rust: um programa, `lume-montar`, com subcomandos (`ferramentas/montar`) |

Situação (novembro de 2026): os seis itens feitos no ramo `versao-2.0`. A
procura por atributos percorre a pasta direto no disco; um índice vivo (com
o kqueue, como as consultas ao vivo do BeOS) fica para o 2.x, junto com o
compositor próprio.

Sobre o Python: o sistema do LumeBSD (os comandos `lume`, o rc, os apps) não
usa Python. Ele chega à máquina como dependência de pacotes de terceiros (o
KWin, o plasma-workspace, o glib, o LibreOffice) e do networkmgr; tirá-lo de
vez pediria recompilar o KDE, e ele voltaria com os kits de ciências.

## 2.x

- **Office com a estética do iWork**, começando pelo editor de texto (estilo
  Pages, salvando em `.odt` e `.docx`); depois a planilha e as apresentações.
- **MediaKit de baixa latência**: um servidor de mídia nosso sobre o OSS, com
  threads de tempo real e um grafo de nós como o do BeOS (alvo: uns 5 ms).
- **Replicants**: pedaços vivos de um app na área de trabalho ou em outro app.
- **Compositor próprio** sobre o lume-motor (fase 2 das janelas): abas,
  Replicants e a tinta de forma nativa.
- **lume-macho**: um módulo de kernel para o Mach-O (como o Linuxulator) e o
  dyld e a libSystem do Darling, para rodar programas do Mac de verdade.
