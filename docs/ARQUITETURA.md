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
        │       ferramentas/montar pacotes   pacotes essenciais do FreeBSD
        │                     │   (pkg, drm, nvidia-kmod, firmwares,
        │                     │    microcódigo — hash BLAKE2b conferido)
        │                     ▼
        │       ferramentas/montar pendrive
        │                     │   makefs (UFS) + FAT EFI + mkimg (GPT)
        ▼                     ▼
 fonte/ (cópia legível)   LumeBSD-1.2-Farol-pendrive.img
```

O LumeBSD **não é um fork**: é o FreeBSD 15.1 com uma série de patches. Os
patches são a fonte de verdade; `fonte/` é a cópia legível dos arquivos que
eles criam, gerada por `ferramentas/montar fonte` e conferida no CI.

## Onde está cada coisa

| Caminho (árvore do FreeBSD) | O que é |
|---|---|
| `usr.bin/lume/` | `lume(1)`, em C: painel, emblema, dicas, e o despachante dos subcomandos `lume configurar/hardware/video/wifi/desktop/instalar/tema/atualizar/tempo/loja/compat/ajuda` |
| `usr.sbin/lume-configurar/` | os scripts `lume-*` (sh, inclusive `lume-atualizar`, `lume-tempo`, `lume-loja` e `lume-compat`), o tema Papel (`tema/`, com o menu "Versões anteriores" do Dolphin), o catálogo da loja (`loja/`), a Ajuda em HTML (`ajuda/`) e o `cron` da máquina do tempo |
| `usr.sbin/vigia/` | o gerenciador de serviços, em C (`vigiad` e o cliente `vigia`) |
| `usr.sbin/forja/` | a forja (sh), `forja.conf` e `forja(8)` |
| `libexec/rc/rc.d/lume_*`, `vigia` | serviços de boot: assistente, vídeo, notebook, emblema, checagem de saúde das atualizações (`lume_saude`), pacotes da base nova (`lume_pacotes`), vigia |
| `app/lume-atualizacoes/`, `app/lume-programas/` (fora da árvore do FreeBSD) | os apps Atualizações e Programas do LumeBSD (Qt 6), os ícones, os atalhos e as políticas do polkit |
| `app/lume-tinta/`, `app/lume-animacoes/` | os efeitos do KWin: o filtro e-ink (C++) e as animações de abrir e fechar (JavaScript); `ferramentas/montar efeitos` os compila e empacota |
| `app/lume-rede/` | o ícone de rede (C++/Qt): cabo e Wi-Fi pelo socket de controle do wpa_supplicant |
| `app/lume-janelas/` | as abas de janelas (o efeito `lume_abas`) e a decoração Papel (`org.lumebsd.papel`), para o KWin |
| `app/lume-atributos/`, `usr.bin/lume-atributo/` | os atributos dos arquivos: a janela e a procura (Qt) e o `lume atributo` (C) |
| `app/lume-paginas/` | o Páginas (C++/Qt): o editor de texto, com leitores e gravadores de `.odt` e `.docx` e o PDF; também na linha de comando |
| `app/lume-financas/` | as Finanças do LumeBSD (C++/Qt): juros, financiamento, projetos, inflação (SGS do Banco Central), regressão e o orçamento do mês; também na linha de comando |
| `app/lume-motor/` | o motor de renderização: `liblume-motor`, os cabeçalhos e o `papel.glsl` comum ao filtro do KWin |
| `usr.sbin/lume-configurar/marca/` | a identidade: o farol (ícones, logotipo, splash) |
| `ferramentas/lume-montar/` | as ferramentas de construção, em Rust (`ferramentas/montar SUBCOMANDO`) |
| `stand/lua/gfx-lume*.lua` | marca e emblema do menu do loader |
| `sys/amd64/conf/LUME` | configuração do kernel |
| `sys/conf/newvers.sh` | identidade: `TYPE=LumeBSD`, `REVISION=2.2`, `BRANCH=FAROL`, e `FREEBSD_REVISION=15.1` |

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
4. **pacotes**: `ferramentas/montar pacotes` lê o catálogo do repositório
   trimestral do FreeBSD, resolve dependências, baixa e confere o hash
   (BLAKE2b) de cada pacote.
5. **imagem**: `ferramentas/montar pendrive` acrescenta ao stage a lista
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
2. GPT: EFI (260 MB, FAT32), freebsd-boot (512 KB, o `gptzfsboot` para
   BIOS, com o `pmbr` no setor 0), swap (8 GB, criptografada com chave
   descartável a cada boot) e o resto para o ZFS: o "GPT (BIOS+UEFI)" do
   instalador do FreeBSD, para o disco ligar nos dois tipos de firmware;
3. GELI AES-256-XTS na partição do ZFS, com `-b` (o loader pede a senha e
   repassa a chave ao kernel — uma pergunta só);
4. pool `zroot` com o esquema de ambientes de boot (`zroot/ROOT/default`,
   `home`, `tmp`, `usr/ports`, `var/*`...);
5. copia o sistema pela lista `sistema.mtree` (os arquivos do build, sem o
   que é só do modo ao vivo);
6. loader no caminho padrão `EFI/BOOT/BOOTX64.EFI` e, se o boot foi em
   UEFI, uma entrada "LumeBSD" no `efibootmgr`;
7. `loader.conf`, `rc.conf`, `sysctl.conf` e `/etc/make.conf` do sistema
   instalado; pacotes do pendrive; com internet, o desktop.

**O console é a tela.** Numa máquina virtual com porta serial (o
virt-manager cria uma), o firmware UEFI põe a serial na frente no `ConOut`,
e o `loader.efi` passa isso ao kernel (`boot_serial`): o rc e o assistente
iam para a serial. O `loader.conf` do sistema instalado e o do pendrive
levam `boot_serial="NO"`. Em BIOS, o loader faz da serial o console
principal sempre que `comconsole` está na lista; o pendrive tem um
`/boot/lua/local.lua` que, só em BIOS, deixa `console="vidconsole"` (a
serial continua de segunda, por `boot_multicons`). E o assistente do
primeiro boot vai para o `ttyv0` se mesmo assim cair na serial. Numa máquina
virtual em BIOS, o `vt` escolheria o modo texto do VGA (`vt_vga.c`, quando
`vm_guest` está definido), cuja fonte não tem ã nem õ; `hw.vga.textmode="0"`
nos dois `loader.conf` deixa o modo gráfico. Testado por
`tests/teste-virt-manager.py` (SeaBIOS e UEFI, disco SATA, porta serial).

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

`ferramentas/montar atualizacao` gera, a partir do mesmo stage do
pendrive, o `LumeBSD-<versão>-sistema.tar.zst`: o sistema base inteiro,
lido do `METALOG` original (donos, permissões e flags `schg` vão no tar), mais
`/usr/share/lume/versao` e `/usr/share/lume/sistema.mtree`. Ao lado vai o
índice `atualizacao.txt` (`versao=`, `base=`, `abi=`, `osversion=`,
`arquivo=`, `sha256=`, `tamanho=`, `notas=` e, quando preciso,
`minimo=`/`intermediario=`). A base sai do `__FreeBSD_version` do próprio
stage (`1501000` → `15.1`, `FreeBSD:15:amd64`); as notas são
`docs/novidades.md`, que o app mostra.
O CI publica os dois na Release; o padrão do sistema é
`releases/latest/download/atualizacao.txt` (`lume_atualizar_url` no
`rc.conf` muda isso). O índice é lido como dados (`chave=valor`), nunca
executado, e cada campo é validado.

### Aplicar

1. segue o índice (se a versão instalada é mais velha que `minimo=`, vai
   para o índice `intermediario=` antes, até 5 saltos), baixa o pacote
   (com `fetch -r`, continua de onde parou) e confere o SHA256; a versão
   tem que ser mais nova e a base não pode ser mais velha;
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

### Correção, versão nova, versão principal

O número do LumeBSD diz o tipo (correção `x.y.Z`, versão nova `x.Y.0`,
versão principal `X.0.0`), que o app mostra; o trabalho quem decide é a
base FreeBSD (`uname -U` no sistema em uso contra `base=` do índice):

| Base | O que acontece |
|---|---|
| a mesma | só o sistema troca |
| outra da mesma série (15.1 → 15.2) | `drivers`: os pacotes donos de `/boot/modules/*.ko` (drm, nvidia, firmwares), feitos para um kernel exato |
| outra série (15 → 16) | `programas`: todos os pacotes; os travados pelo forja são destravados e listados em `/var/db/lume/recompilar` |

Os pacotes são **baixados antes**, ainda no sistema em uso e com o `pkg`
dele (nada da base nova roda no kernel velho): `pkg -o ABI=<nova> -o
OSVERSION=<nova> -o PKG_DBDIR=<novo>/var/db/pkg -o
PKG_CACHEDIR=<novo>/var/cache/pkg -o REPOS_DIR=<novo>/etc/pkg/,...` faz o
`update` (o catálogo da base nova fica no ambiente novo) e o `install -F`
ou `upgrade -F -f` (só baixa). A marca `/var/db/lume/pacotes-pendentes`
vai no ambiente novo. No primeiro boot dele, já com o kernel novo e antes
do vídeo e da tela de login, `rc.d/lume_pacotes` instala do cache, sem
internet (`pkg-static install|upgrade -U -f`). Se falhar, fica
`pacotes-falharam`, o `lume_saude` não confirma e o boot seguinte volta.

### O app

`app/lume-atualizacoes/main.cpp`, Qt 6 Widgets **sem moc** (só sinais do
Qt ligados a lambdas), para compilar fora do FreeBSD: o
`ferramentas/montar app` usa o clang 19 com `--target
x86_64-unknown-freebsd15.1`, a libc/libc++ do stage e os cabeçalhos e
bibliotecas do pacote `qt6-base` (baixado só para isso; no sistema o Qt
vem com o desktop). O binário vai para `/usr/bin/lume-atualizacoes`, com
`rpath` para `/usr/local/lib/qt6`, e entra no `METALOG.build`, logo no
pendrive e no pacote de atualização.

Todo o trabalho é do `lume-atualizar --maquina`, que troca as mensagens
por linhas `@chave a=b c=d` (`@estado`, `@nova`, `@em_dia`, `@passo n
total texto`, `@progresso pct` durante o download, `@pronto`, `@falhou`,
`@erro`). O que precisa de root vai pelo `pkexec` com a ação polkit
`org.lumebsd.atualizar` (`auth_admin_keep`; no FreeBSD, administrador é o
grupo `wheel`). Reiniciar é pelo `pkexec lume-atualizar reiniciar`, depois
de uma confirmação (o `org.kde.Shutdown` da sessão não reinicia no
FreeBSD).

`lume-atualizacoes --bandeja` entra com a sessão (autostart): procura 90 s
depois e a cada 6 h e só põe um ícone na bandeja, com um aviso, quando há
versão nova ou quando a última atualização falhou. "Falhou" sai do
`/var/log/lume-atualizar.log` (em `zroot/var/log`, o mesmo para todos os
ambientes): o último `pronto: <ambiente>` sem `confirmado` nem
`descartado` depois, num ambiente que existe e não é o atual; o motivo é
a linha `não confirmado` do `lume_saude`.

O menu, o autostart e a política do polkit ficam em `/usr/share/lume/app`
e vão para `/usr/local` pelo `lume tema integrar`, que o `lume tema
papel`, o `lume_saude` (depois de confirmar uma atualização) e o `lume
tempo automatico` chamam.

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

Testado no QEMU, num sistema instalado pela 1.1.0 (veja o README para os
testes da 1.2): atualização 1.1.0 →
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

## Programas (`lume loja`)

O público do LumeBSD (estudantes e professores de exatas e de sociais
aplicadas, e quem só quer um computador que funcione) chega procurando o
programa que já conhece. Por isso a loja é um **catálogo curado**, não a
lista crua de 30 mil pacotes: `usr.sbin/lume-configurar/loja/catalogo`,
uma linha por programa:

```
id|categoria|nome|pacotes|kits|parecido com|descrição|ícone|abrir
octave|ciencia|GNU Octave|octave octave-forge-signal ...|exatas|MATLAB, Simulink|...|octave|
office365|web|Microsoft 365|web:https://www.office.com/|geral|Word, Excel, PowerPoint|...|x-office-document|
```

- **pacotes**: os oficiais do FreeBSD. `+linux` liga antes a camada Linux;
  `web:URL` é um app da web (um `.desktop` que abre o site no Chromium ou
  no Chrome com `--app=`, em janela própria, ou no Firefox).
- **parecido com**: a busca acha por eles; é o que faz "matlab" achar o
  Octave.
- **kits**: `@kit id|Nome|descrição|kits incluídos`. Exatas e Sociais
  aplicadas incluem o Uso geral. O instalador pergunta o kit e roda
  `lume-loja kit` no chroot depois do desktop.
- **abrir**: um atalho de menu que o LumeBSD põe junto (`loja/atalhos`),
  para pacotes que não trazem um (o JupyterLab).

O CI (`ferramentas/montar catalogo`, toda semana) confere que cada
pacote existe no quarterly do FreeBSD 15 e mostra quanto cada kit baixa,
descontando o que já vem com o desktop.

`lume-loja` faz o trabalho; o app `app/lume-programas` (Qt 6, mesmo
esquema do app de atualizações) lê o catálogo direto e chama `lume-loja
--maquina` para `estado` (`@instalado id`), `tamanho` (o que ocupa a
mais, pelo `pkg rquery` sem root, com as dependências que faltam),
`instalar`, `remover` e `kit` (`@passo`, `@pacote`, `@progresso`,
`@pronto`, `@erro`). O progresso vem das linhas `[i/n] Fetching` e
`[i/n] Installing` do `pkg`: baixar é a primeira metade da barra,
instalar a segunda. Root pelo `pkexec` com a ação `org.lumebsd.programas`;
apps da web não pedem senha (vão para o menu do usuário).

Antes de instalar ou remover, `lume-tempo antes` tira uma foto do sistema.
**Remover** não usa `pkg delete` (que levaria junto o que depende do
pacote): marca os pacotes do programa como automáticos (`pkg set -A 1`) e
roda `pkg autoremove`, que só tira o que nada mais usa; os do desktop e os
de outros programas instalados do catálogo nem são marcados.

### Compatibilidade (`lume compat`)

- **Windows**: o Wine (programa `windows` do catálogo). O
  `loja/lume-windows.desktop` é o app padrão dos tipos `.exe`/`.msi`/`.bat`
  (no `mimeapps.list` do sistema) e chama `lume-compat abrir`: sem o Wine,
  pergunta (kdialog) e abre `lume-programas --instalar windows`, que
  instala com a senha e o progresso e fecha; depois `wine start /unix`.
- **Linux**: `lume compat linux ligar` liga `linux_enable` e roda
  `service linux start` (o módulo e as montagens de `/compat/linux`).

### Ajuda (`lume ajuda`)

Páginas HTML em `/usr/share/lume/ajuda`, com o visual do tema Papel (e modo
escuro), abertas no navegador pelo `lume-ajuda` (com atalhos: `lume ajuda
windows`, `lume ajuda wifi`...). O CI confere os links entre elas.

## A tela de papel (efeitos do KWin)

O LumeBSD desenha as janelas em tons de papel e tinta. A ideia vem dos
leitores de e-ink e de um *shader* de leitura que circula no r/unixporn:
numa tela LCD, conteúdo claro e sem cores fortes se lê melhor sob luz forte,
porque o reflexo some contra o fundo claro, sem precisar aumentar o brilho.

**`lume_tinta`** (`app/lume-tinta/tinta.cpp`) é um efeito do KWin em C++, um
`OffscreenEffect` como o "Inverter" do próprio KWin: cada janela é
desenhada numa textura e passa por um *fragment shader* que tira a
luminância (Rec. 709, na escala de gama 2.2 que a gestão de cor do KWin
entrega), aplica uma curva de contraste (pretos de tinta, brancos de papel)
e mistura entre a cor da tinta (`#1c1b19`) e a do papel (`#f0ece2`), na
intensidade escolhida.

- **Exceções**: janelas cuja classe contém o nome de um visualizador de
  fotos, player de vídeo, editor de imagem ou vídeo, ou jogo (`gwenview`,
  `vlc`, `mpv`, `gimp`, `krita`, `kdenlive`, `steam`...), e qualquer janela
  em **tela cheia** (o sinal `windowFullScreenChanged` tira e devolve), não
  passam pelo filtro. Mais classes em `kwinrc`, `[Effect-lume_tinta]
  Excecoes=`.
- **Atalhos** (KGlobalAccel): Ctrl+Meta+T inverte o filtro na tela;
  Ctrl+Meta+Y, só na janela ativa.
- **Sem moc**: a classe do efeito e a fábrica não declaram sinais nem slots
  (as conexões são lambdas). Os metadados do plugin, que o
  `Q_PLUGIN_METADATA` geraria, saem do `montar-efeitos.py`: o
  `metadata.json` convertido em CBOR no formato do moc do Qt 6 (mapa com
  IID, nome da classe e metadados), exportado com `QT_MOC_EXPORT_PLUGIN_V2`.
  Assim o efeito compila com o clang de fora do FreeBSD, como os apps.
- **Duas versões**: contra o KWin do X11 (`kwin-x11`, a sessão padrão) e o
  do Wayland. O KWin só carrega um plugin da mesma versão (está no IID);
  o `lume tema integrar` instala o plugin em
  `/usr/local/lib/qt6/plugins/kwin*/effects/plugins` só se a versão do
  pacote `plasma6-kwin*` bate com a que o build usou.

**`lume_refresh`** e **`lume_esmaecer`** (`app/lume-animacoes/`) são efeitos
em JavaScript, da categoria exclusiva `toplevel-open-close-animation`. O
refresh anima o brilho de 0 a 1 com curva exponencial (a janela fica
escura, na cor da tinta pelo filtro, e o conteúdo surge no fim, como a
troca de página de um e-ink); o esmaecer anima opacidade e saturação. Nada
desliza nem cresce.

O tema Papel liga o compositor OpenGL no `kwinrc` do sistema, que antes
ficava desligado; `lume tema tinta` e `lume tema animacao` mudam o
`kwinrc` do usuário e pedem `reconfigure` ao KWin.

## A loja por dentro (aparência)

O app Programas do LumeBSD usa o estilo Fusion do Qt, uma paleta de papel e
tinta e uma folha de estilo própria: serifa (IBM Plex Serif) nos títulos,
mono (IBM Plex Mono) no índice e nos rótulos, fios de 1 px no lugar de
sombras, botões retos de tinta cheia (a ação principal) ou contorno. Os
ícones são convertidos para tons de cinza. Assim a loja tem a mesma cara
com qualquer tema do KDE.

## Windows de 32 bits

O FreeBSD 15 não publica mais pacotes i386. O Wine do FreeBSD roda
programas de 32 bits com uma cópia i386 do Wine na pasta do usuário
(`~/.i386-wine-pkg`), instalada pelo `pkg32.sh` do próprio pacote, sobre as
bibliotecas de 32 bits do sistema base. Por isso:

- o build deixou de ter `WITHOUT_LIB32` (o sistema tem `/usr/lib32`);
- `lume-compat` lê o cabeçalho PE do `.exe`; um de 32 bits, sem a parte de
  32 bits, pergunta e roda `pkg32.sh --old install -r FreeBSD-ports wine
  mesa-dri` como o usuário (pacotes do FreeBSD 14, que rodam no 15), com
  uma barra do kdialog; depois abre pelo script `wine` do pacote, que junta
  o Wine de 32 e o de 64 bits. Quando o Wine do sistema muda de versão, o
  `pkg32.sh --old upgrade` acompanha;
- os de 64 bits continuam pelo `wine64.bin` com `WINELOADER` (os scripts
  `wine` e `wine64` exigem a parte de 32 bits).

## Programas do Mac

O `loja/lume-mac.desktop` é o app padrão de `.dmg`: `lume-compat abrir` tira
o nome do programa do arquivo (sem versão, arquitetura e "installer"),
explica e abre `lume-programas --procurar NOME`. O catálogo tem os
programas da Apple no campo "parecido com".

Desde a 1.5, o `/usr/libexec/lume-macho` (em `mac/`) carrega executáveis
Mach-O e traduz as chamadas BSD do XNU para as do FreeBSD pelo `ptrace`;
`lume-compat abrir` reconhece um executável do Mac pelos primeiros bytes e
o abre num terminal, e `lume compat mac testar` roda o programa de teste
(`/usr/share/lume/mac/ola-mac`). O estudo e o caminho (a ABI no kernel,
como o Linuxulator, e o dyld do Darling) estão em [MACOS.md](MACOS.md).

## O motor de renderização

O `lume-motor` (`app/lume-motor`; a biblioteca `/usr/lib/liblume-motor.so.1`,
os cabeçalhos em `/usr/include/lume` e a demonstração `/usr/bin/lume-motor`)
é o protótipo do motor da especificação: DRM/KMS com page flip, curvas resolvidas por pixel
na GPU (OpenGL ES 3.1), papel num compute shader e a viscosidade da tinta.
A arquitetura, o que foi medido e o caminho até ele desenhar o desktop
estão em [MOTOR.md](MOTOR.md). O filtro do KWin (`lume_tinta`) usa o mesmo
papel (as mesmas contas, de `papel.glsl`, e as mesmas cores, de `papel.h`) e
a mesma viscosidade no desktop de hoje: as fibras no shader de cada
janela, e a viscosidade num passo da tela inteira (`paintScreen`), com dois
históricos e as janelas sem filtro entrando na hora (recorte por
`glScissor`).

No X11, cada `reconfigure` do KWin recomeça o compositor e recria o efeito.
O `OffscreenEffect` copia cada janela para uma textura no primeiro quadro,
às vezes antes de a imagem da janela chegar, e só copia de novo quando a
janela muda: a área de trabalho e as janelas paradas ficavam pretas depois
de trocar o tema. O `lume_tinta` refaz as texturas de todas as janelas
algumas vezes nos primeiros segundos de vida (0,4 s, 1,5 s, 4 s e 8 s).

## Abas de janelas

Como no BeOS (onde as abas são do app_server, o servidor de janelas), aqui
elas são do KWin, em duas peças que se falam por uma interface comum
(`Lume::ControleAbas`, publicada numa propriedade do `qApp`):

- o efeito `lume_abas` guarda as pilhas: empilhar, separar, a próxima e a
  anterior (atalhos do kglobalaccel). Só a janela da aba ativa fica
  visível; as outras ficam minimizadas, e a que aparece toma a geometria
  (e a maximização) da anterior. Uma janela fechada sai da pilha.
- a decoração `org.lumebsd.papel` (KDecoration3) desenha a barra de
  título, com uma aba por janela da pilha; clicar numa aba pede ao efeito
  a troca. No X11 o KWin entrega os cliques da barra à decoração, por isso
  as abas são dela e não do efeito.

As duas compilam sem moc pelo `ferramentas/montar efeitos`, para o KWin do
X11 e o do Wayland.

No X11, todo `reconfigure` do KWin (trocar o tema, mudar uma opção) recomeça
o compositor, e com ele destrói e recria todos os efeitos. O `lume_abas` que
sai guarda os grupos na propriedade `lume.abas.estado` do `qApp` (o
`internalId` de cada janela e a que aparece); o que entra os retoma. Se
nenhum retomar em 3 segundos (o efeito foi desligado), as janelas escondidas
voltam à tela. O KWin não descarrega a biblioteca de um efeito, então o
temporizador pode chamar o código dela.

## Atributos dos arquivos

Os atributos são os atributos estendidos do sistema de arquivos (`extattr`,
espaço `user`), no ZFS e no UFS2: vão com o arquivo no `cp -p`, no `mv`,
nos snapshots e no `zfs send`. Os nomes conhecidos são os do KDE
(KFileMetaData), para o Dolphin mostrar os mesmos valores: `xdg.tags`
(as etiquetas, separadas por vírgula), `xdg.comment`, `baloo.rating` (de 0
a 10; o `lume atributo` mostra em estrelas) e `xdg.origin.url`. Os outros
ficam como `lume.<nome>`. O painel de informações do Dolphin (F11) lê e
edita as etiquetas, a nota e o comentário direto do arquivo. Na lista, o
Dolphin só lê os atributos de cada arquivo (as colunas Etiquetas, Nota e
Comentário, pelo `KFileMetaData::UserMetaData`, sem índice) quando a
indexação do Baloo está "ligada" (`Baloo::IndexerConfig::fileIndexingEnabled`).
O `/usr/local/etc/xdg/baloofilerc` do LumeBSD deixa ligada, mas sem pasta
nenhuma para indexar (`folders=`, vazio): o indexador não tem o que fazer (no
FreeBSD ele entra em loop no extrator), e quem quiser o índice inclui as
pastas nas Configurações do Sistema › Pesquisa.

- `lume atributo` (`usr.bin/lume-atributo`, C) lê, grava e procura. A
  procura percorre a pasta com o `fts`, sem entrar nas ocultas, e lê os
  atributos de cada arquivo, sem índice.
- Ao vivo (`procurar -v`, como as consultas do BeOS), ela continua de olho:
  uma vigia do inotify por pasta (o inotify do kernel do FreeBSD 15, não o
  kqueue, que pediria um descritor aberto por arquivo). Gravar ou apagar um
  atributo gera `IN_ATTRIB` para a pasta (`vop_setextattr_post`); criar,
  apagar e mover geram os outros. Cada aviso reavalia só aquele arquivo (ou
  percorre a pasta nova) e escreve `+`, `~` ou `-` com o caminho; `*`
  quando o kernel perdeu avisos (`IN_Q_OVERFLOW`), e aí tudo recomeça.
- `lume-atributos` (Qt) é a janela de atributos (o botão direito do
  Dolphin, por um service menu) e a procura, que roda o `lume atributo
  procurar -v` e atualiza a lista a cada linha. Uma consulta salva é um arquivo
  `.consulta` (tipo `application/x-lume-consulta`, com `pasta=` e
  `condicoes=`), que abre o `lume-atributos` com dois cliques.

## O Páginas (editor de texto)

`app/lume-paginas/main.cpp`, um arquivo só, Qt 6 sem moc, como os outros
apps. As peças:

- **A folha.** O documento é um `QTextDocument` com o tamanho da página em
  pixels de 96 por polegada e as margens na moldura raiz: com a altura da
  página, o próprio Qt pagina (as margens valem em cada página, e
  `PageBreak_AlwaysBefore` é a quebra de página). Na tela, um `QTextEdit`
  com a largura da folha e a altura de todas as páginas, sem rolagem
  própria (quem rola é a mesa em volta); por baixo do texto ele desenha as
  folhas, uma faixa da mesa no meio das margens entre uma e outra, e os
  números. O QTextEdit põe a altura da página em -1 quando a largura muda:
  o `resizeEvent` devolve. O PDF desenha cada página com o mesmo
  documento (`QPdfWriter` a 96 ppp), então sai igual à tela.
- **Os estilos.** O nome do estilo fica numa propriedade do bloco
  (`UserProperty + 1`); os tamanhos são proporção da fonte do corpo. O
  esquema "abnt" muda os estilos para a NBR 14724. No `.odt` e no `.docx`
  os estilos vão com os nomes de lá (Heading 1, Title, Quote...), e na
  leitura voltam pelo nome (do LibreOffice, do Word, ou em português).
- **Os formatos.** `.odt` e `.docx` são zips: o `ZipLeitor` e o
  `ZipEscritor` usam o zlib da base. O XML é lido numa árvore pequena
  (`No`) com os prefixos de costume, qualquer que seja o do arquivo. Os
  gravadores escrevem o XML à mão; o ODF com as listas aninhadas como o
  padrão pede, o OOXML com a numeração (`numbering.xml`), o rodapé com o
  campo PAGE e as imagens em `word/media`. Na leitura do Word: as fontes
  do tema, o espaçamento contextual, a numeração "sem número" dos títulos
  do LibreOffice e as imagens ligadas fora do pacote.
- **Os testes.** `lume-paginas --resumo` imprime a estrutura;
  `tests/paginas.sh` grava cada modelo em `.odt` e `.docx`, converte de um
  para o outro e compara, e roda o `--autoteste`, que digita com eventos
  de teclado de verdade (estilos, listas com Tab, quebra de página, Tab
  na tabela). No CI ele compila para o Linux e roda os testes; a
  interoperabilidade com o LibreOffice foi conferida à mão (o LibreOffice
  abre o `.docx` e devolve o `.odt` com a mesma estrutura, e o contrário).

## Ícone de rede

O `lume-rede` (C++/Qt, na bandeja) fala com o wpa_supplicant pelo socket de
controle, o mesmo protocolo do `wpa_cli`, aberto ao grupo `wheel`
(`ctrl_interface_group`): conectar não pede a senha do administrador. A
senha vira a chave (PBKDF2-HMAC-SHA1, como o `wpa_passphrase`) no próprio
app; redes WPA3 usam o `sae_password`. Preparar a placa (`wlans_*` no
rc.conf) é do `lume wifi preparar`, pelo pkexec, e o serviço `lume_wifi`
cria o `wlan0` no boot.

## Vídeo AMD

`lume video` reconhece as placas pelo fabricante (0x1002) e escolhe o
driver pelo nome do chip: `radeonkms` até a geração Sea Islands (Tahiti,
Hawaii, Kaveri e as TeraScale), `amdgpu` dali em diante. Os firmwares
(`gpu-firmware-amd-kmod-*` e `gpu-firmware-radeon-kmod-*`, uns 36 MB) vão
no pendrive e o instalador só os copia para computadores com AMD. Com
processador AMD, o `loader.conf` carrega o `amd-ucode.bin` e o `rc.conf`, o
`amdtemp`.

## A .iso

`montar pendrive --iso` gera, do mesmo stage, uma `.iso` híbrida: El
Torito para BIOS (`cdboot`) e UEFI (um ESP FAT12 de 2 MB, como o do
FreeBSD), e um GPT na área de sistema (com o `isoboot` e o ESP) para
gravar num pendrive. A raiz é o cd9660, só de leitura (`root_rw_mount=NO`);
o arquivo `/etc/diskless` faz o `rc.initdiskless` do FreeBSD levar `/etc`,
`/var`, `/root`, `/mnt` e `/media` para a memória (`/conf/base/*/remount`
e `md_size`), e o `/tmp` vai para um tmpfs. O instalador deixa de fora o
disco com o rótulo `iso9660/LUMEBSD`.

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
fontes IBM Plex, animações do KDE em zero, o compositor OpenGL com o filtro
e-ink e o refresh de e-ink ligados (veja "A tela de papel"), Look-and-Feel
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
