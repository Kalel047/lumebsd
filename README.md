# LumeBSD 1.0 "Farol"

> Luz firme sobre fundação BSD.

LumeBSD é uma distribuição BSD independente construída a partir do código-fonte
do **FreeBSD 15.0** (`releng/15.0`). Ela mantém a base sólida do FreeBSD
(kernel, userland, rc.d, ports/pkg) e dá ao sistema identidade, ferramentas e
padrões próprios.

A ideia: **um BSD que já chega pronto para quem fala português**. Você liga
a máquina, um assistente em português pergunta o essencial e o sistema
fica configurado, em pt-BR, com teclado ABNT2 e horário de Brasília.

```
     \    |    /
   ---  (( ))  ---
     /   _|_   \
        /___\
        |[_]|        LumeBSD 1.0
        |===|        "Farol"
        |   |
        |===|
       /|   |\
        |===|
       _|___|_
  ~~~~/_______\~~~~
   ~~   ~~~~   ~~
```

## O que o LumeBSD muda

### Primeiro boot guiado

No primeiro boot, antes do login, aparece o assistente `lume-configurar`:

```
  [1/6] Senha do administrador (root)
  [2/6] Seu usuário              -> criado no grupo wheel (pode usar su -)
  [3/6] Nome desta máquina
  [4/6] Fuso horário             -> padrão America/Sao_Paulo
  [5/6] Teclado                  -> ABNT2, US internacional, US ou Portugal
  [6/6] Idioma do sistema        -> Português (Brasil) ou English (US)
```

Ele mostra um resumo, pede confirmação e aplica tudo. Valida o que você
digita (senha com pelo menos 6 caracteres, login em minúsculas, fuso
existente) e nunca deixa o sistema com o root sem senha por descuido: se
ninguém responder em 120 s, o boot segue, o assistente volta no próximo
boot e o painel `lume` avisa que falta configurar. Dá para refazer a
qualquer momento com `lume configurar`.

No FreeBSD essas perguntas só existem no instalador; as imagens de disco
para VM e nuvem sobem com root sem senha e tudo em inglês.

### vigia: serviços que se reerguem sozinhos

O LumeBSD tem um gerenciador de serviços próprio, o **vigia**. Cada serviço é
um arquivo de poucas linhas em `/etc/vigia/servicos`:

```ini
descricao = Página "Olá" na porta 8080
comando   = sh -c 'while true; do printf "HTTP/1.0 200 OK\r\n\r\nOlá\n" | nc -N -l 8080; done'
usuario   = nobody
depende   = relogio
reiniciar = sempre
```

O `vigiad` é o pai de cada serviço e fica sabendo na hora quando um deles
cai (um único laço `kqueue`). Ele:

- sobe os serviços na ordem das dependências;
- reinicia o que cair, esperando 1, 2, 4, 8 s… até 30 s;
- desiste depois de 5 quedas seguidas e põe o serviço em **manutenção**,
  com o motivo à vista, em vez de ficar em loop para sempre;
- guarda a saída de cada serviço em `/var/log/vigia/<serviço>.log`;
- para tudo com SIGTERM (e SIGKILL após 10 s) no desligamento.

```
root@lumebox:~ # vigia

  SERVIÇO       ESTADO         PID     HÁ         REINÍCIOS  DESCRIÇÃO
  instavel      ● manutenção   -       11 s       4          Serviço instável (demonstração)
    └ saiu com código 3; caiu 5 vezes seguidas, veja 'vigia log instavel' e use 'vigia reiniciar instavel'
  ntpd          ● ativo        1114    53 s       0          Relógio sincronizado pela internet (NTP)
  relogio       ● ativo        1208    46 s       0          Relógio de exemplo (escreve a hora no log)
  web           ● ativo        1224    39 s       1          Página "Olá" na porta 8080

  3 ativo(s) de 4.
```

| Comando | O que faz |
|---|---|
| `vigia` | tabela de serviços |
| `vigia iniciar / parar / reiniciar X` | age agora |
| `vigia ativar / desativar X` | age e guarda a escolha para os próximos boots |
| `vigia info X` | todos os detalhes |
| `vigia log X [-n N] [-f]` | saída do serviço |
| `vigia recarregar` | relê os arquivos: novos sobem, apagados param |

#### Limites por serviço

Cada serviço roda numa classe de login própria (`v-<nome>`), herdada por
tudo o que ele criar. Isso dá ao vigia uma visão do serviço inteiro, não
só do processo principal:

- a tabela mostra a **memória** do serviço todo, e `vigia info` mostra
  também CPU e número de processos;
- **limites** no arquivo valem para a soma (aplicados pelo RCTL do kernel,
  que o LumeBSD já traz ligado):

  ```ini
  limite_memoria   = 64M
  limite_cpu       = 50%
  limite_processos = 20
  ```

- quando o processo principal morre, os filhos que ele deixou para trás
  são encerrados, em vez de ficarem órfãos.

Medido numa VM: com `limite_cpu = 50%`, um laço infinito usou 30,1 s de
CPU em 60 s (50,2%). O kernel freia em passos de 1 segundo, então abaixo
de ~35% um serviço que usa CPU sem parar fica em 33–40% (com 20%, deu
40%). Isso está documentado em `man vigia.conf`.

#### Sob demanda

Com `escutar`, o vigia segura o socket e cria um processo por conexão,
como o `inetd`. Sem visitas, o serviço não ocupa nada:

```ini
descricao = Diz a hora para quem conectar
escutar   = tcp:7777          # ou tcp:127.0.0.1:7777, ou unix:/var/run/x.sock
comando   = date
usuario   = nobody            # portas < 1024 funcionam: quem abre é o vigia
```

A conexão vira a entrada e a saída padrão do comando, então qualquer
script vira um serviço de rede. `VIGIA_CLIENTE` traz o endereço de quem
conectou, e `max_conexoes` controla quantas são atendidas ao mesmo tempo.

#### Dependências em cascata

Iniciar um serviço inicia as dependências; parar ou reiniciar um serviço
faz o mesmo com quem depende dele:

```
root@lumebox:~ # vigia parar relogio
  relogio: ● parado
  parando também: web
```

Qualquer usuário consulta; só o root muda estados. O `ntpd` já vem
gerenciado pelo vigia, e há exemplos em `/usr/share/examples/vigia`.
O painel `lume` mostra o resumo (`serviços   3 de 4 ativos, 1 com problema`).
Manuais: `man vigia` e `man vigia.conf`.

O vigia convive com o `rc.d` do FreeBSD: o sistema base continua subindo
pelo `rc`, e o vigia cuida dos serviços que você quer supervisionados.

### O disco cresce sozinho

No primeiro boot a partição raiz é expandida até o fim do disco
(`growfs`). Deu 20 GB para a VM? O sistema usa os 20 GB, sem comandos.
Também vem com 512 MB de swap já configurados.

### Tudo o mais

| Área | Mudança |
|---|---|
| Identidade | `uname -s` → `LumeBSD`, versão `1.0-FAROL`, `/etc/os-release` com `ID=lumebsd`, `ID_LIKE=freebsd` |
| Boot | Menu do loader "Bem-vindo ao LumeBSD", marca LumeBSD em ASCII e emblema do farol colorido (luz âmbar, torre branca, mar azul) |
| Kernel | Configuração própria `LUME`: parte do `GENERIC`, sem símbolos de debug, CTF e hooks de DTrace, e com a contabilidade de recursos (RACCT) ligada |
| `lume(1)` | Utilitário nativo em C: painel do sistema (kernel, CPU, memória e disco com barras, carga, uptime), `lume farol`, `lume dica`, `lume versao`, `lume configurar` |
| `rc.d/lume_farol` | Serviço que acende o farol e o painel ao final do boot (`lume_farol_enable`) |
| `vigia(8)` | Gerenciador de serviços com supervisão, dependências, logs e proteção contra loop de falhas (`rc.d/vigia`) |
| `lume-configurar(8)` | Assistente de configuração em português, roda no primeiro boot (`rc.d/lume_configurar`) e com `lume configurar` |
| Login | Mensagem do dia em português |
| Userland | `src.conf` enxuto: sem testes, lib32, toolchain, ZFS/CDDL, Kerberos, sendmail, jogos |

## Estrutura

```
patches/0001-...patch   identidade, loader, lume(1), kernel LUME
patches/0002-...patch   assistente de primeiro boot (lume-configurar)
patches/0003-...patch   vigia, o gerenciador de serviços
patches/0004-...patch   vigia: limites, sob demanda, cascata; kernel com RACCT
conf/src.conf          seleção da userland
conf/LUME              configuração do kernel
build-lumebsd.sh       do clone até a imagem bootável
tests/teste-console.py boota no QEMU e conversa com o console serial
docs/                  logs reais de boot
```

## Construindo

Funciona em Linux (cross-build com LLVM) ou FreeBSD:

```sh
git clone https://github.com/Kalel047/lumebsd.git
cd lumebsd
sh build-lumebsd.sh
```

O script clona o FreeBSD `releng/15.0`, aplica o patch, compila userland e
kernel `LUME`, instala num diretório de staging sem precisar de root e gera
`LumeBSD-1.0-Farol-amd64.img` (GPT, boot BIOS e UEFI).

Dependências no Ubuntu/Debian:

```sh
sudo apt install clang-18 lld-18 llvm-18 libarchive-dev libbz2-dev zlib1g-dev \
    bison flex time qemu-system-x86 ovmf
```

## Testando

```sh
qemu-system-x86_64 -m 1024 -nographic \
    -drive file=LumeBSD-1.0-Farol-amd64.img,format=raw
```

No primeiro boot o assistente pede a senha do root e cria seu usuário.

Teste automatizado (responde o assistente sozinho e confere o resultado):

```sh
cp LumeBSD-1.0-Farol-amd64.img teste.img && truncate -s +2G teste.img
python3 tests/teste-console.py teste.img primeiro.log primeiro   # assistente
python3 tests/teste-console.py teste.img segundo.log segundo     # não reaparece
python3 tests/teste-console.py teste.img vigia.log servicos      # gerenciador
python3 tests/teste-console.py teste.img boot3.log persistencia  # após reboot
python3 tests/teste-console.py teste.img rec.log recursos         # limites, socket
ALVO_CPU=50 python3 tests/teste-console.py teste.img cpu.log cpu2 # mede limite de CPU
```

## Status

- [x] Identidade, loader, `lume(1)`, serviço rc.d, kernel `LUME`
- [x] Userland e kernel compilados do zero (cross-build em Linux, LLVM 18)
- [x] Imagem bootando no QEMU por **BIOS** e **UEFI**, até o login em ~1 minuto sem KVM
- [x] Assistente de primeiro boot testado nos três cenários: configuração
  completa (com entradas inválidas recusadas), segundo boot sem assistente e
  boot sem ninguém respondendo
- [x] Raiz expandida de 2,9 GB para 4,8 GB num disco 2 GB maior
- [x] vigia testado: dependências, `kill -9` com reinício automático,
  loop de falhas indo para manutenção, permissões, arquivos inválidos,
  desligamento limpo e escolhas que sobrevivem ao reboot
- [x] Limites e contabilidade por serviço: memória e processos medidos;
  `limite_processos` barrando o 4º `fork`; sobras encerradas; CPU medida
  contra um serviço idêntico sem limite (50% → 50,2%)
- [x] Sob demanda: TCP, socket Unix, porta 79 atendida por `nobody`,
  5 conexões simultâneas; cascata ao parar e reiniciar

Sessões reais: [assistente](docs/primeiro-boot-assistente.log),
[vigia](docs/vigia-sessao.log) e
[limites e sob demanda](docs/vigia-recursos-sessao.log).

Primeiro boot real, no console serial:

```
root@farol:~ # uname -a
LumeBSD farol 1.0-FAROL LumeBSD 1.0-FAROL lumebsd-403d4045b631 LUME amd64
root@farol:~ # freebsd-version -ku
1.0-FAROL
1.0-FAROL
root@farol:~ # cat /var/run/os-release
NAME=LumeBSD
VERSION="1.0-FAROL"
VERSION_ID="1.0"
VERSION_CODENAME=farol
ID=lumebsd
ID_LIKE=freebsd
ANSI_COLOR="0;33"
PRETTY_NAME="LumeBSD 1.0-FAROL (Farol)"
root@farol:~ # lume

  LumeBSD 1.0 "Farol"  @ farol
  ----------------------------------------------
  kernel     LumeBSD 1.0-FAROL (base FreeBSD 15.0)
  cpu        QEMU Virtual CPU version 2.5+ (2 núcleos)
  memória    [##..................] 101.8 MiB / 989.5 MiB
  disco /    [####................] 581.6 MiB / 2.9 GiB (ufs)
  carga      0.64  0.20  0.08
  ligado há  0 h 0 min 59 s

root@farol:~ # lume dica
dica: man hier explica a organização de diretórios do sistema.
root@farol:~ # service -e | tail -3
/etc/rc.d/lume_farol
/etc/rc.d/cron
/etc/rc.d/bgfsck
root@farol:~ # shutdown -p now
```

Logs completos em [`docs/boot-bios.log`](docs/boot-bios.log) e
[`docs/boot-uefi.log`](docs/boot-uefi.log).

## Licença

As mudanças do LumeBSD são distribuídas sob a licença BSD de 2 cláusulas
(veja `LICENSE`). O FreeBSD é distribuído sob suas próprias licenças.
"FreeBSD" é marca registrada da FreeBSD Foundation; o LumeBSD é um projeto
independente, sem afiliação com o FreeBSD Project.
