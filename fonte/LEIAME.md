# Código do LumeBSD

Cópia legível dos arquivos que o LumeBSD acrescenta ao FreeBSD 15.1, nos mesmos caminhos da árvore do FreeBSD.
A fonte de verdade são os `patches/`; esta pasta é gerada por `ferramentas/fonte.py gerar` e conferida no CI.

## Arquivos do FreeBSD que os patches alteram

Estes só aparecem nos patches (são mudanças pequenas em arquivos do FreeBSD):

- `libexec/rc/rc.d/Makefile`
- `libexec/rc/rc.d/os-release`
- `share/mk/src.sys.env.mk`
- `stand/images/Makefile`
- `stand/lua/Makefile`
- `stand/lua/core.lua`
- `stand/lua/drawer.lua`
- `sys/conf/newvers.sh`
- `sys/sys/copyright.h`
- `usr.bin/Makefile`
- `usr.bin/login/motd.template`
- `usr.sbin/Makefile`
- `usr.sbin/pkg/Makefile`
