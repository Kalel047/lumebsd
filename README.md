# LumeBSD 1.0 "Farol"

> Luz firme sobre fundação BSD.

LumeBSD é uma distribuição BSD independente construída a partir do código-fonte
do **FreeBSD 15.0** (`releng/15.0`). Ela mantém a base sólida do FreeBSD
(kernel, userland, rc.d, ports/pkg) e dá ao sistema identidade, ferramentas e
padrões próprios.

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

| Área | Mudança |
|---|---|
| Identidade | `uname -s` → `LumeBSD`, versão `1.0-FAROL`, `/etc/os-release` com `ID=lumebsd`, `ID_LIKE=freebsd` |
| Boot | Menu do loader "Bem-vindo ao LumeBSD", marca LumeBSD em ASCII e emblema do farol colorido (luz âmbar, torre branca, mar azul) |
| Kernel | Configuração própria `LUME`: parte do `GENERIC`, sem símbolos de debug, CTF e hooks de DTrace |
| `lume(1)` | Utilitário nativo em C: painel do sistema (kernel, CPU, memória e disco com barras, carga, uptime), `lume farol`, `lume dica`, `lume versao` |
| `rc.d/lume_farol` | Serviço que acende o farol e o painel ao final do boot (`lume_farol_enable`) |
| Login | Mensagem do dia em português |
| Userland | `src.conf` enxuto: sem testes, lib32, toolchain, ZFS/CDDL, Kerberos, sendmail, jogos |

## Estrutura

```
patches/0001-lumebsd-1.0-farol.patch   todas as mudanças sobre o FreeBSD 15.0
conf/src.conf                          seleção da userland
conf/LUME                              configuração do kernel
build-lumebsd.sh                       do clone até a imagem bootável
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
    bison flex time qemu-system-x86
```

## Testando

```sh
qemu-system-x86_64 -m 1024 -nographic \
    -drive file=LumeBSD-1.0-Farol-amd64.img,format=raw
```

Login: `root`, sem senha. Defina uma com `passwd` antes de qualquer uso real.

## Status

- [x] Identidade, loader, `lume(1)`, serviço rc.d, kernel `LUME`
- [ ] Compilação completa validada
- [ ] Imagem testada no QEMU

## Licença

As mudanças do LumeBSD são distribuídas sob a licença BSD de 2 cláusulas
(veja `LICENSE`). O FreeBSD é distribuído sob suas próprias licenças.
"FreeBSD" é marca registrada da FreeBSD Foundation; o LumeBSD é um projeto
independente, sem afiliação com o FreeBSD Project.
