## O que muda

<!-- Uma ou duas frases: o que e por quê. Se fecha uma issue: "Fecha #123". -->

## Como testou

<!-- Ex.: "lume loja instalar X no QEMU", "fase loja do tests/teste-pendrive.py",
     "build-lumebsd.sh completo". Capturas de tela ajudam quando muda o visual. -->

## Conferi

- [ ] A mudança no sistema está em `src` (lumebsd-15.1) e regerei `patches/` e `fonte/` (veja CONTRIBUTING.md)
- [ ] Scripts sh passam no `shellcheck -s sh -S error`; manuais no `mandoc -T lint`
- [ ] Programas novos no catálogo passam no `ferramentas/montar catalogo`
- [ ] Textos para o usuário em português, sem termos técnicos quando dá para evitar
- [ ] Atualizei o README, a Ajuda ou `docs/ARQUITETURA.md`, se precisou
