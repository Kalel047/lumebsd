# Segurança

## Versões que recebem correções

| Versão | Correções de segurança |
|---|---|
| 1.3.x (a mais nova) | sim |
| 1.2.x e anteriores | não: atualize pelo app **Atualizações do LumeBSD** (ou `lume atualizar aplicar`) |

As correções saem como uma versão nova (1.3.1, 1.3.2...), que chega pelo app
de atualizações.

## Como relatar uma falha

**Não abra uma issue pública** para uma falha de segurança.

Use o relato privado do GitHub: na aba **Security** do repositório,
**Report a vulnerability**
(<https://github.com/Kalel047/lumebsd/security/advisories/new>). Se o botão
não aparecer, entre em contato com o mantenedor,
[Kalel047](https://github.com/Kalel047), pelos meios de contato do perfil
dele no GitHub, e combine um canal privado.

Conte:

- o que é a falha e o que alguém consegue fazer com ela;
- a versão (`lume`, primeira linha) e como reproduzir;
- se já está pública em algum lugar.

A resposta inicial vem em até 7 dias. Depois da correção, o relato é
publicado com crédito a quem encontrou, se a pessoa quiser.

## O que é do LumeBSD e o que é de outros projetos

| Onde está a falha | Para quem relatar |
|---|---|
| O que o LumeBSD acrescenta: `lume` e os scripts `lume-*` (instalador, atualizações, máquina do tempo, loja, compatibilidade), `forja`, `vigia`, os apps Atualizações e Programas do LumeBSD, as regras do polkit, a configuração padrão, a imagem do pendrive | aqui, como descrito acima |
| O sistema base do FreeBSD (kernel, libc, OpenSSH, ZFS...) | a equipe de segurança do FreeBSD: <https://www.freebsd.org/security/> |
| Um programa dos pacotes (KDE, Firefox, LibreOffice, Wine...) | o projeto do programa, ou os ports do FreeBSD |

Na dúvida, relate aqui: a gente encaminha.
