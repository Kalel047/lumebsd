# Roteiro: LumeBSD 2.0 e 2.x

A lista de Kalel para o primeiro update grande, e as decisões tomadas
(novembro de 2026).

## 2.0

| Item | Como |
|---|---|
| **Identidade visual própria** | o farol do LumeBSD no carregador de boot, no splash, no SDDM, na tela de bloqueio, no botão do menu e em "Sobre o sistema"; nenhum logo do KDE à vista |
| **Wi-Fi sem Python** | um app nosso em C++/Qt na bandeja, no lugar do networkmgr (a única peça visível em Python ao nosso alcance) |
| **Abas e empilhamento de janelas** | como no BeOS e no Haiku (lá ficam no servidor de janelas, o app_server, não no kernel); aqui, fase 1, como plugin do KWin |
| **Atributos de arquivo na interface** | como no BFS: atributos estendidos no ZFS (`extattr`), um indexador nosso em C para as buscas ao vivo, colunas e edição no Dolphin, pastas de busca salvas |
| **lume-motor e lume-tinta juntos** | o motor vira biblioteca (papel, fibras, tinta) e o filtro do KWin a usa |
| **Ferramentas de build sem Python** | `ferramentas/*.py` reescritas em C ou Rust |

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
