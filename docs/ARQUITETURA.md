# Arquitetura do LumeBSD

Este documento explica como o LumeBSD é montado: de onde vem cada parte,
como o build funciona, o que o kernel `LUME` tem, como o vídeo NVIDIA e
Intel é escolhido e como o pendrive, o instalador, a forja e o tema se
encaixam. O README fala do uso; aqui é o "como funciona por dentro".

## Visão geral

```
 FreeBSD releng/15.1  (commit fixado em build-lumebsd.sh)
        │
        │  patches/0001..00NN  (git apply, em ordem)
        ▼
 árvore do LumeBSD ──► buildworld + buildkernel LUME   (LLVM 19)
        │                     │
        │                     ▼
        │       installworld/distribution/installkernel -DNO_ROOT
        │                     │   (diretório "stage" + METALOG, sem root)
        │                     ▼
        │       ferramentas/pacotes.py   pacotes essenciais do FreeBSD
        │                     │   (pkg, drm, nvidia-kmod, firmwares,
        │                     │    microcódigo — hash BLAKE2b conferido)
        │                     ▼
        │       ferramentas/montar-pendrive.py
        │                     │   makefs (UFS) + FAT EFI + mkimg (GPT)
        ▼                     ▼
 fonte/ (cópia legível)   LumeBSD-1.1-Farol-pendrive.img
```

O LumeBSD **não é um fork**: é o FreeBSD 15.1 com uma série de patches. Os
patches são a fonte de verdade; `fonte/` é a cópia legível dos arquivos que
eles criam, gerada por `ferramentas/fonte.py` e conferida no CI.

## Onde está cada coisa

| Caminho (árvore do FreeBSD) | O que é |
|---|---|
| `usr.bin/lume/` | `lume(1)`, em C: painel, emblema, dicas, e o despachante dos subcomandos `lume configurar/hardware/video/wifi/desktop/instalar/tema/atualizar/tempo` |
| `usr.sbin/lume-configurar/` | os scripts `lume-*` (sh, inclusive `lume-atualizar` e `lume-tempo`), o tema Papel (`tema/`, com o menu "Versões anteriores" do Dolphin) e o `cron` da máquina do tempo |
| `usr.sbin/vigia/` | o gerenciador de serviços, em C (`vigiad` e o cliente `vigia`) |
| `usr.sbin/forja/` | a forja (sh), `forja.conf` e `forja(8)` |
| `libexec/rc/rc.d/lume_*`, `vigia` | serviços de boot: assistente, vídeo, notebook, emblema, checagem de saúde das atualizações (`lume_saude`), vigia |
| `stand/lua/gfx-lume*.lua` | marca e emblema do menu do loader |
| `sys/amd64/conf/LUME` | configuração do kernel |
| `sys/conf/newvers.sh` | identidade: `TYPE=LumeBSD`, `REVISION=1.1`, `BRANCH=FAROL`, e `FREEBSD_REVISION=15.1` |

Os arquivos do FreeBSD que os patches *alteram* (Makefiles, `newvers.sh`,
`os-release`, `motd.template`, `drawer.lua`, `pkg/Makefile`...) estão
listados em [`fonte/LEIAME.md`](../fonte/LEIAME.md).

## Build

`build-lumebsd.sh` faz tudo, em Linux (cross-build) ou FreeBSD:

1. **Fonte**: busca o commit fixado do `releng/15.1` (`FREEBSD_COMMIT`) e
   aplica `patches/*.patch`.
2. **world e kernel**: `buildworld` e `buildkernel KERNCONF=LUME`. No Linux,
   pelo `tools/build/make.py` do FreeBSD, com o LLVM 19 do sistema como
   compilador cruzado.
3. **stage**: `installworld distribution installkernel -DNO_ROOT` num
   diretório comum. Sem root, as permissões e donos ficam no `METALOG`.
4. **pacotes**: `ferramentas/pacotes.py` lê o catálogo do repositório
   trimestral do FreeBSD, resolve dependências, baixa e confere o hash
   (BLAKE2b) de cada pacote.
5. **imagem**: `ferramentas/montar-pendrive.py` acrescenta ao stage a lista
   do sistema para o instalador (`/usr/share/lume/sistema.mtree`), os
   pacotes (para instalar) e os mesmos pacotes já extraídos (para testar
   ao vivo), e a configuração do modo ao vivo; depois gera o UFS
   (`makefs`), a partição EFI e a imagem GPT (`mkimg`), que dá boot por
   UEFI e BIOS.

### Detalhes que importam

- **LLVM 19, não 18.** O clang 18 acessa `__stack_chk_guard` pela GOT nos
  módulos do kernel (`R_X86_64_REX_GOTPCRELX`), e o linker de módulos do
  kernel não aceita essa relocação: `zfs.ko`, `geom_eli.ko` e outros 396
  módulos não carregariam. O LLVM 19 é o mesmo do FreeBSD 15.1.
- **Triplo do compilador.** O FreeBSD monta o alvo do compilador com a
  versão do `newvers.sh`; com `REVISION=1.1` ele virava
  `x86_64-unknown-freebsd1.1`. Por isso existe `FREEBSD_REVISION=15.1`, que
  é o que `share/mk/src.sys.env.mk` usa.
- **src.conf** (`conf/src.conf`): tira testes, lib32, LLDB, arquivos de
  depuração, jogos, OFED e sendmail; mantém clang, ZFS e DTrace.

## Kernel LUME

`sys/amd64/conf/LUME` é o `GENERIC` inteiro (todos os drivers e módulos,
ZFS, DTrace) com uma mudança: `nooptions RACCT_DEFAULT_TO_DISABLED`. A
contabilidade de recursos fica ligada desde o boot, para o vigia medir e
limitar memória, CPU e processos de cada serviço pelo RCTL. Os símbolos de
depuração são gerados (o CTF do DTrace precisa deles), mas não instalados.

## Vídeo: NVIDIA e Intel

Nada de vídeo é compilado no kernel; tudo vem de pacotes oficiais do
FreeBSD:

| Peça | De onde vem |
|---|---|
| Intel (i915/xe) | `drm-612-kmod` + `gpu-firmware-intel-kmod-*` |
| NVIDIA (Turing em diante) | `nvidia-kmod` no pendrive; `nvidia-driver` no sistema |
| NVIDIA mais antigas | `nvidia-driver-580` (Maxwell, Pascal, Volta), `-470` (Kepler), `-390` (Fermi) |
| Sem aceleração | `xf86-video-scfb` (framebuffer UEFI) |

`lume video detectar` lê `pciconf -lv`: fornecedor `0x10de` é NVIDIA,
`0x8086` é Intel, e a geração da NVIDIA sai do prefixo do chip (`TU`, `GA`,
`AD`... → atual; `GP`/`GM`/`GV` → 580; `GK` → 470; `GF` → 390). A escolha:

- NVIDIA sem Intel, ou num desktop (sem bateria) → NVIDIA;
- Intel, inclusive notebook híbrido (a tela interna liga na Intel) → Intel;
- o resto → scfb.

O `rc.d/lume_video` roda antes do SDDM a cada boot: grava
`/var/db/lume/video-tentando`, carrega o driver e apaga a marca. Se o
computador travar, a marca sobra; no boot seguinte o modo cai para scfb e
o estado fica "travou". O X recebe um `10-lume-video.conf` com o driver
escolhido. No pendrive, `lume video testar` faz o mesmo na hora.

`lume desktop` instala o pacote do driver certo para a placa detectada.

## Pendrive e instalador

O pendrive é um sistema completo em UFS (rótulo `gpt/lumeroot`), que sobe
como root sem senha e não monta nada do disco do computador.

`lume instalar` (`lume-instalar.sh`):

1. confere que `zfs.ko` e `geom_eli.ko` carregam, **antes** de mexer no disco;
2. GPT: EFI (260 MB, FAT32), swap (8 GB, criptografada com chave
   descartável a cada boot) e o resto para o ZFS;
3. GELI AES-256-XTS na partição do ZFS, com `-b` (o loader pede a senha e
   repassa a chave ao kernel — uma pergunta só);
4. pool `zroot` com o esquema de ambientes de boot (`zroot/ROOT/default`,
   `home`, `tmp`, `usr/ports`, `var/*`...);
5. copia o sistema pela lista `sistema.mtree` (os arquivos do build, sem o
   que é só do modo ao vivo);
6. loader no caminho padrão `EFI/BOOT/BOOTX64.EFI` e uma entrada "LumeBSD"
   no `efibootmgr`;
7. `loader.conf`, `rc.conf`, `sysctl.conf` e `/etc/make.conf` do sistema
   instalado; pacotes do pendrive; com internet, o desktop.

Variáveis para instalação sem perguntas (usadas nos testes):
`LUME_DISCO`, `LUME_CONFIRMA=APAGAR`, `LUME_SENHA_DISCO`, `LUME_DESKTOP`,
`LUME_SWAP`, `LUME_CONSOLE_SERIAL`, `LUME_REPO_URL`.

### Primeiro boot

`rc.d/lume_configurar` roda o assistente antes do login enquanto não
existir `/var/db/lume/configurado`. Com o desktop instalado ele espera o
tempo que for (sem usuário não há como entrar pela tela de login); sem
desktop, pula em 120 s. Além de senha, usuário, máquina, fuso e idioma, ele
leva o teclado para o console, para o Xorg (`00-lume-teclado.conf`), para a
tela de login (`sddm_xkb_layout`) e para o KDE (`kxkbrc`).

## Atualizações (`lume atualizar`)

### O pacote

`ferramentas/montar-atualizacao.py` gera, a partir do mesmo stage do
pendrive, o `LumeBSD-<versão>-sistema.tar.zst`: o sistema base inteiro,
lido do `METALOG` original (donos, permissões e flags `schg` vão no tar), mais
`/usr/share/lume/versao` e `/usr/share/lume/sistema.mtree`. Ao lado vai o
índice `atualizacao.txt` (`versao=`, `arquivo=`, `sha256=`, `tamanho=`).
O CI publica os dois na Release; o padrão do sistema é
`releases/latest/download/atualizacao.txt` (`lume_atualizar_url` no
`rc.conf` muda isso). O índice é lido como dados (`chave=valor`), nunca
executado, e cada campo é validado.

### Aplicar

1. confere o SHA256 do pacote (baixado por HTTPS) e se a versão é mais
   nova e da mesma série (`1.1.x`: o mesmo FreeBSD por baixo);
2. garante `canmount=noauto` em todos os ambientes de `ROOT/` (o
   instalador da 1.1.0 criava o `default` com `on`, e o `zfs mount -a` do
   boot montava o ambiente antigo por cima do novo);
3. `bectl create` a partir do ambiente em uso e `bectl mount`;
4. extrai o pacote no ambiente novo, menos:
   - **configurações**: arquivos com a tag `config` no METALOG, fora
     `etc/rc.d`, `etc/periodic` e `etc/defaults` (que têm a tag mas são
     do sistema). Se já existem, ficam; a versão nova vai para
     `<arquivo>.lume-novo` quando é diferente;
   - **bancos derivados**: `pwd.db`, `spwd.db`, `passwd`, `login.conf.db`,
     `services.db` nunca vêm do pacote (são gerados das configurações que
     ficaram; trocá-los desencontraria usuários e senhas);
5. apaga o que estava na lista antiga e saiu da nova; tira o
   `vfs.root.mountfrom` fixo do `loader.conf`; `kldxref`;
6. grava `/var/db/lume/atualizacao-pendente` **dentro** do ambiente novo e
   o ativa com `bectl activate -t` (só o próximo boot).

Se qualquer passo falha, o ambiente novo é destruído e nada mudou.

### Confirmar ou voltar

O loader usa o `bootonce` do pool uma vez e o apaga. No boot do ambiente
novo, `rc.d/lume_saude` vê a marca e, solto do rc por `daemon(8)`, confere:
raiz vinda do ambiente certo, `/var/db` gravável, `sh`, `init`, `login`,
`sshd` e `lume` presentes, vigia respondendo pelo socket e, com desktop, o
SDDM de pé (espera até 2 min). Passou: `bectl activate` (permanente) e a
marca some. Não passou: avisa no console, no syslog e em
`/var/log/lume-atualizar.log`, e não confirma.

Em todos os casos de falha — checagem reprovada, pânico, travamento, falta
de energia no meio do boot — o padrão do pool ainda é o ambiente anterior,
e o próximo boot volta para ele sozinho. `lume atualizar voltar` faz o
mesmo depois de uma atualização já confirmada.

Testado no QEMU, num sistema instalado pela 1.1.0: atualização 1.1.0 →
1.1.1 confirmada no boot; uma 1.1.2 com o `vigiad` quebrado (checagem
reprovada, motivo registrado, reboot de volta à 1.1.1); uma 1.1.2 com o
`init` quebrado (pânico, reboot automático, volta à 1.1.1); e um boot
interrompido no meio (volta ao anterior).

## Máquina do tempo (`lume tempo`)

`usr.sbin/lume-configurar/lume-tempo.sh`, um script `sh` sobre o ZFS e o
`bectl`. Sem daemon: o `cron` roda `lume-tempo automatico` a cada 15
minutos (`/etc/cron.d/lume-tempo`, um arquivo novo, que o `lume atualizar`
instala mesmo em quem tem a 1.1.x).

### O que entra

Os sistemas de arquivos com a propriedade de usuário `lume:tempo=on`. O
instalador a liga em `zroot/home` e em `zroot/ROOT` (herdada por todos os
ambientes de boot; dos ambientes, só o que está em uso entra). Num sistema
sem a propriedade em lugar nenhum — atualizado da 1.1 — o padrão vale
igual e a primeira execução grava a propriedade. `usr/src`, `usr/ports`,
`tmp`, `var/log`, `var/tmp` e `var/crash` ficam de fora.

### As fotos

- Nome `lume-AAAA-MM-DD_HH.MM.SS`, uma foto **atômica** de todos os
  sistemas de arquivos que mudaram (`zfs snapshot a@n b@n` num só
  comando), com as propriedades `lume:tipo` (`auto`, `manual`, `antes`) e
  `lume:descricao`.
- Automática: só se já passou uma hora da última automática **e** o
  `written` do sistema de arquivos (o que mudou desde a foto anterior) não
  é zero. A conferência a cada 15 minutos faz um notebook que estava
  desligado na hora cheia não perder a vez.
- `antes`: o `forja` (instalar, remover, atualizar), o `lume atualizar` e o
  `recuperar --substituir` (como root) tiram uma antes de mexer.

### Limpeza

Por sistema de arquivos, das mais velhas para as mais novas: automáticas
das últimas 24 h ficam todas; até 30 dias, a primeira de cada dia; até 26
semanas, a primeira de cada semana (semana começando na segunda, no fuso
local). `antes` ficam 30 dias; manuais, até alguém apagar. Tudo
configurável no `rc.conf` (`lume_tempo_horas`, `_dias`, `_semanas`,
`_antes_dias`). Com o pool acima de `lume_tempo_limite` (85%), as
automáticas mais velhas saem até baixar. O espaço mostrado é o que
`zfs destroy -nvp ds@a,b,c` diz que voltaria apagando todas as fotos.

### De volta

- **Versões de um arquivo**: o caminho vira (sistema de arquivos,
  montagem, caminho relativo) pela montagem mais longa que o contém; cada
  foto é lida em `<montagem>/.zfs/snapshot/<foto>/<relativo>`, das mais
  novas para as mais velhas, e só entram as versões diferentes da
  seguinte (`cmp`; pastas por data e tamanho). Funciona sem root: o
  usuário só lê nas fotos o que já podia ler.
- **Recuperar** copia (`cp -Rp`) para o lugar se o arquivo sumiu, ou ao
  lado, `nome (de 03-11 14.00).ext`, se ele existe. `--substituir` guarda o
  atual antes (numa foto, como root; ao lado, como usuário).
- **Mudou**: `zfs diff -FH` da foto ao estado atual, sem o que todo boot
  recria (`/var/run`, entropia) e com os nomes `\0NNN` decodificados.
- **Voltar o sistema**: acha a foto do sistema mais nova até o momento
  pedido, em qualquer ambiente de boot (depois de uma atualização, as
  fotos de antes dela estão no ambiente anterior), e faz
  `bectl create -e <ambiente>@<foto> volta-<data>`, com `canmount=noauto`
  e sem raiz fixa no `loader.conf`, como o `lume atualizar`. Ativa de vez
  (`bectl activate`); `/home` não muda. O ambiente de antes continua lá.

### No KDE

`/usr/local/share/kio/servicemenus/lume-tempo.desktop` põe "Versões
anteriores" no botão direito do Dolphin (arquivos e pastas). Ele chama
`lume-tempo gui ARQUIVO`, que monta as janelas com o `kdialog` (no tema
do Plasma, portanto no Papel): a lista das versões, depois abrir (só
leitura, `xdg-open` direto da foto), recuperar ao lado ou substituir; no
fim abre o Dolphin com o arquivo recuperado selecionado. O `lume tema`
instala o menu, e o `automatico` também, para quem atualizou com o KDE já
instalado. O painel do `lume` lê `/var/db/lume/tempo-estado` (gravado pelo
script) e mostra a idade da última foto, em vermelho com mais de 2 dias.

## Identidade e compatibilidade

`uname -s` diz `LumeBSD` e `uname -r` diz `1.1-FAROL`. O ABI dos binários
continua o do FreeBSD 15.1, então os pacotes oficiais funcionam como estão.

O que olha a versão do sistema precisa saber do FreeBSD por baixo:

- **pkg**: `usr.sbin/pkg` aponta as versões `FAROL` para o repositório
  `quarterly` da release.
- **ports**: o `/etc/make.conf` do sistema instalado e a forja passam
  `OPSYS=FreeBSD`, `_OSRELEASE`/`UNAME_r=15.1-RELEASE` (calculado do
  `kern.osreldate`) e `UNAME_s=FreeBSD`. O `uname(1)` respeita essas
  variáveis, e os ports as repassam a cada compilação, então o `configure`
  dos programas vê `amd64-portbld-freebsd15.1`.
- **compilação à mão** fora dos ports:
  `env UNAME_s=FreeBSD UNAME_r=15.1-RELEASE ./configure`.

## forja

A forja é um script sh sobre `pkg` e a árvore de ports:

- **binário por padrão**: `forja instalar X` usa o pacote oficial;
- **opções por pacote** em `/etc/forja/opcoes` (`editors/nano -NLS`) e
  globais no `forja.conf`, viram `<OPTIONS_NAME>_SET/UNSET` num `make.conf`
  próprio (`/var/db/forja/make.conf`);
- um pacote com opções é **compilado** da árvore; dependências diretas vêm
  em binário (as que também têm opções são compiladas antes); o resultado
  ganha a anotação `forja_opcoes` e um `pkg lock`, para a atualização
  binária não desfazer a escolha;
- **`forja atualizar`**: cria um ambiente de boot (`bectl`), tenta
  atualizar a árvore (git, ou tarball; se falhar, segue com a que tem),
  `pkg upgrade` e recompila o que mudou de versão ou de opções (o
  `--newuse` do Portage);
- **mundo** (`/var/db/forja/mundo`): o que você pediu, como o `@world`.

## vigia

Daemon em C com um laço `kqueue`: é pai de cada serviço, sabe na hora
quando um cai, reinicia com espera crescente e põe em manutenção depois de
5 quedas seguidas. Cada serviço roda numa classe de login `v-<nome>`, que
dá a visão do serviço inteiro (soma de memória/CPU de todos os processos) e
os limites via RCTL. Sockets sob demanda: o vigia abre a porta e cria um
processo por conexão (como o `inetd`). Manuais: `vigia(8)`, `vigia.conf(5)`.

## Tema Papel

`lume tema papel` grava padrões do sistema em `/usr/local/etc/xdg` (vale
para quem não mexeu nas próprias configurações): esquema de cores,
fontes IBM Plex, animações em zero, composição desligada, Look-and-Feel
`org.lumebsd.papel`, tela de login e de bloqueio, perfil do Konsole e GTK.
Os ícones `lume-papel` são gerados na hora a partir do Breeze, trocando
cada cor dos SVG pela sua luminância. `lume tema padrao` desfaz tudo.

## Testes

`tests/teste-pendrive.py` conversa com o sistema pelo console serial do
QEMU (UEFI/OVMF, disco NVMe de destino):

| Fase | O que faz |
|---|---|
| `fumaca` | boota o pendrive sem gravar nada nele e confere os comandos |
| `ajustar` | entra como root no sistema instalado e roda comandos (`AJUSTES`); com `LUME_REINICIAR=1` atravessa reboots e pânicos (usado nos testes de atualização) |
| `aovivo` | `lume hardware`, `lume video`, instalação completa com desktop |
| `instalado` | senha do disco, assistente, ZFS/GELI/bectl, SDDM |
| `desktop` | login gráfico, teclado, fotos da tela (Plasma, Konsole, Dolphin) |
| `forja` | binário, compilação com opções, `forja atualizar` |

No CI ([`.github/workflows`](../.github/workflows)):

- **Verificar**, a cada push, em poucos minutos: os patches aplicam no
  commit fixado e no topo do `releng/15.1`, `fonte/` bate com eles,
  `sh -n` + `shellcheck` nos scripts, Python e manuais (`mandoc`).
- **Build completo**, sob demanda e todo dia 1º: compila tudo, boota a
  imagem no QEMU (fase `fumaca`), guarda a imagem como artefato e, se
  pedido, publica a Release.

As fases longas (`aovivo` a `forja`) precisam de um espelho de pacotes
(`LUME_REPO_URL`) e de horas sem KVM; foram rodadas à mão, e as sessões
estão em `docs/`.
