
## O que tem

- **Pendrive ao vivo**: testa o hardware sem tocar no disco (`lume hardware`, `lume video testar`, `lume wifi`).
- **Instalação** num disco inteiro: ZFS sobre GELI (AES-256-XTS) com senha no boot, swap criptografada, ambientes de boot, UEFI.
- **Atualizações que não quebram**, pelo app **Atualizações do LumeBSD** ou por `lume atualizar`: a versão nova entra num ambiente de boot à parte e só é confirmada depois de um boot saudável; se falhar, o computador volta sozinho.
- **Máquina do tempo** (`lume tempo`, e "Versões anteriores" no Dolphin): uma foto do disco por hora, guardada por dias e semanas.
- **Programas do LumeBSD**: a loja em português, com kits por área (Uso geral, Exatas, Sociais aplicadas) e busca pelo programa que você usava ("matlab" acha o Octave).
- **Programas do Windows e da web**: dois cliques num .exe abrem com o Wine; Microsoft 365, Teams, Overleaf e outros viram apps no menu.
- **Intel + NVIDIA**: driver NVIDIA escolhido pela geração da placa; Intel em notebooks e híbridos; volta sozinho ao modo sem aceleração se um driver travar.
- **KDE Plasma 6** com o **tema Papel** (estética e-ink), em português, teclado ABNT2.
- **forja**: pacotes binários ou compilados com as suas opções, no estilo Portage.
- **vigia**: serviços supervisionados, com limites e sockets sob demanda.

## Como usar

1. Baixe `LumeBSD-1.3-Farol-pendrive.img.xz` e confira o SHA256 (`SHA256SUMS`).
2. Extraia (7-Zip no Windows; `xz -d` no Linux/FreeBSD) e grave num pendrive de 8 GB ou mais: **Rufus em modo DD** no Windows, ou `dd` no Linux/FreeBSD.
3. Desligue o Secure Boot e dê boot pelo pendrive.
4. Rode `lume hardware` e `lume video testar`; se estiver tudo certo, `lume instalar` (apaga o disco escolhido). Ele pergunta o kit de programas da sua área.

Já tem o LumeBSD instalado? Abra **Atualizações do LumeBSD** no menu (ou `lume atualizar`). Os arquivos `atualizacao.txt`, `LumeBSD-<versão>-sistema.tar.zst` e `LumeBSD-<versão>-notas.md` desta Release são o que ele baixa. Quem tem uma versão anterior à 1.2 atualiza uma vez com: `fetch -o /tmp/lume-atualizar https://raw.githubusercontent.com/Kalel047/lumebsd/main/fonte/usr.sbin/lume-configurar/lume-atualizar.sh && sh /tmp/lume-atualizar aplicar`

## Limitações conhecidas

- Testado no QEMU (UEFI); em hardware real, só pelo `lume hardware` de cada um. O driver NVIDIA não foi testado com placa real.
- Notebooks híbridos Intel + NVIDIA usam só a Intel.
- O Secure Boot precisa estar desligado.
- Programas do Windows com proteção anticópia, drivers de equipamentos e programas grandes como o MATLAB e o SolidWorks costumam não rodar no Wine; a Ajuda explica as alternativas.

Documentação: [README](https://github.com/Kalel047/lumebsd#readme) e [arquitetura](https://github.com/Kalel047/lumebsd/blob/main/docs/ARQUITETURA.md).
