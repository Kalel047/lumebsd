# Programas do Mac no LumeBSD: o estudo

Este documento é o começo da compatibilidade com o macOS: o que existe, o
que o LumeBSD já faz e o caminho técnico para rodar programas do Mac no
FreeBSD. Estudo feito em novembro de 2026, sobre o Darling no commit
`8ca2c84f` (4 de novembro de 2026).

## Resumo

- **Hoje não existe nada que rode programas do Mac no FreeBSD.** O único
  projeto que faz isso fora do Mac é o [Darling](https://github.com/darlinghq/darling),
  e ele só funciona no Linux.
- O Darling roda muitos programas de **linha de comando** do Mac. Programas
  **gráficos** ainda são experimentais: o AppKit dele vem do Cocotron, e o
  Metal está começando, traduzido para Vulkan. A versão mais recente é uma
  prévia, a 0.1.20260608 (junho de 2026).
- **Portar o Darling para o FreeBSD é possível, mas é um trabalho grande**:
  cerca de 18 mil linhas da camada que traduz as chamadas de sistema do macOS
  para as do Linux, e mais o servidor (`darlingserver`) e o carregador
  (`mldr`), que usam recursos que só o Linux tem. Estimativa: meses de
  trabalho de uma pessoa experiente, para chegar aos programas de linha de
  comando. Programas gráficos dependem do próprio Darling avançar.
- **O que o LumeBSD faz desde a 1.4**: dois cliques num `.dmg` explicam que o
  programa é do Mac e abrem a loja procurando por ele. O catálogo conhece os
  programas da Apple (Pages, Numbers, Keynote, iMovie, Final Cut, GarageBand,
  Logic, Fotos, Pré-Visualização, Xcode...) e mostra o substituto. A Ajuda
  explica isso (`lume ajuda mac`).

## O que o LumeBSD já faz

| Situação | O que acontece |
|---|---|
| Dois cliques num `.dmg` | `lume-compat abrir`: tira o nome do programa do arquivo (`Spotify-1.2.3.dmg` → "Spotify", `Microsoft_Excel_16.89_Installer.pkg` → "Microsoft Excel"), explica, e oferece **Procurar** (abre `lume-programas --procurar NOME`) ou **Ler a Ajuda** |
| Um `.pkg` do Mac | o mesmo (o LumeBSD separa o `.pkg` do Mac, um arquivo `xar`, do `.pkg` do FreeBSD) |
| Procurar "keynote", "imovie", "logic" na loja | o substituto, pelo campo "parecido com" do catálogo |
| Programas do Mac que têm versão para Windows | abrem com o Wine (64 e 32 bits) |

## Como o Darling funciona

O Darling não é um emulador nem uma máquina virtual: ele roda o código do
Mac direto no processador (x86-64), como o Wine faz com o Windows. As peças:

1. **`mldr`**, o carregador de binários Mach-O (o formato do macOS). Ele mapeia
   o programa e o `dyld` da Apple (o carregador de bibliotecas do macOS, de
   código aberto) na memória e começa a execução.
2. **`libsystem_kernel`** com uma camada de **emulação**: no macOS, os
   programas falam com o kernel XNU por chamadas de sistema próprias. O
   Darling troca essas chamadas por funções que fazem o mesmo com as
   chamadas do Linux. São cerca de 290 arquivos e 18 mil linhas, em
   `src/external/xnu/darling/src/libsystem_kernel/emulation`, organizados
   pelas áreas do XNU: `unistd`, `stat`, `network`, `bsdthread`, `psynch`,
   `signal`, `kqueue`, `xattr`...
3. **`darlingserver`**, um processo que faz o papel da parte Mach do kernel
   do macOS (portas Mach, a comunicação entre processos que o macOS usa para
   tudo). Antes era um módulo do kernel do Linux; hoje é um programa comum
   (cerca de 7 mil linhas de C++), o que facilita portar.
4. Um **contêiner** com o sistema de arquivos do macOS (`/System`,
   `/Library`...): no Linux, com *namespaces* e *overlayfs*.
5. As **bibliotecas e frameworks** do macOS: as de baixo nível vêm do código
   aberto da Apple; as de alto nível (Foundation, AppKit, CoreAudio) são
   reimplementações.

## O que muda no FreeBSD

A boa notícia: o FreeBSD está mais perto do macOS do que o Linux. A parte BSD
do XNU veio do FreeBSD, e o FreeBSD já tem de forma nativa coisas que o
Darling precisa imitar no Linux, como o `kqueue`.

| No Linux (Darling hoje) | No FreeBSD | Esforço |
|---|---|---|
| chamadas de sistema do Linux na camada de emulação (~18 mil linhas) | uma camada `emulation/freebsd`: muitas chamadas do XNU têm equivalente direto no FreeBSD (mesmos nomes, mesmos argumentos, às vezes outros números e estruturas) | o grosso do trabalho, mas boa parte é tradução mecânica |
| `kqueue` emulado sobre `epoll` | `kqueue` nativo | menor que no Linux |
| `darlingserver`: `epoll`, `eventfd`, `timerfd` | `kqueue`; o FreeBSD 14+ tem `eventfd` e `timerfd` | pequeno |
| `darlingserver`: `pidfd_open`, `process_vm_readv/writev` | descritores de processo (`pdfork`, `procdesc`) e `ptrace(PT_IO)` | médio |
| `darlingserver`: `SCM_CREDENTIALS` | `SCM_CREDS` | pequeno |
| `mldr`: `MAP_FIXED_NOREPLACE`, `prctl(PR_SET_MM)` | `MAP_FIXED \| MAP_EXCL`; o ajuste do `brk` não existe e precisa de outro caminho | médio |
| contêiner com *namespaces* e *overlayfs* | *jails*, `nullfs` e `unionfs` | médio |
| sinais de tempo real e `ucontext` do Linux | os do FreeBSD (estruturas diferentes) | médio, delicado |
| o build (CMake, scripts que assumem Linux) | ajustes no CMake | pequeno |

Os programas gráficos têm um obstáculo a mais: o AppKit e o Metal do Darling
ainda estão começando no próprio Linux. Portar agora levaria para o FreeBSD
o mesmo estágio experimental.

## Caminho proposto

1. **Feito (1.4)**: o atalho para `.dmg` e `.pkg`, o catálogo com os
   programas da Apple e a Ajuda.
2. **Prova de conceito**: compilar o `darlingserver` e o `mldr` no FreeBSD,
   com as trocas da tabela, e carregar um binário Mach-O mínimo (um
   "olá, mundo" de linha de comando) até o `dyld`.
3. **Camada de emulação**: escrever `emulation/freebsd` área por área,
   começando pelas que um programa de linha de comando usa (`unistd`,
   `stat`, `fcntl`, `mman`, `signal`, `bsdthread`, `psynch`), com os testes do
   próprio Darling.
4. **Contêiner** com jail e `unionfs`, e um comando `lume compat mac` que
   instala o "macOS" do Darling na pasta do usuário, como o Wine faz.
5. **Programas gráficos**: acompanhar o Darling no Linux; o que funcionar lá
   tende a funcionar aqui depois dos passos 2 a 4.

Cada passo é útil sozinho e pode ser devolvido ao projeto Darling, que é
licenciado sob a GPL 3.0. O LumeBSD manteria o port como um pacote à parte,
não dentro do sistema base.

## Fontes

- [Darling, no GitHub](https://github.com/darlinghq/darling) (README e
  código: `src/startup/mldr`, `src/external/darlingserver`,
  `src/external/xnu/darling/src/libsystem_kernel/emulation`)
- [Darling (software), na Wikipedia](https://en.wikipedia.org/wiki/Darling_(software))
- [Darling: Mach-O linking and loading tricks](https://blog.darlinghq.org/2018/07/mach-o-linking-and-loading-tricks.html)
- [Darling: containerization](https://docs.darlinghq.org/internals/basics/containerization.html)
