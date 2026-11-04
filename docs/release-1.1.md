LumeBSD 1.1.2 "Farol" — pendrive ao vivo + instalador, sobre o FreeBSD 15.1.

## Novo na 1.1.2: a máquina do tempo

- **`lume tempo`**: uma foto do disco por hora (snapshots do ZFS), das pastas pessoais e do sistema, só quando algo mudou. Ficam todas das últimas 24 horas, uma por dia por 30 dias e uma por semana por 6 meses.
- Recupere um arquivo apagado ou a versão de ontem de um documento, sem ser root: `lume tempo versoes ARQUIVO` e `lume tempo recuperar ARQUIVO [quando]`. No Dolphin: botão direito, **Versões anteriores**.
- Volte o sistema inteiro para antes de uma instalação: `lume tempo voltar N` (as pastas pessoais não mudam; o sistema de agora fica guardado).
- `forja instalar/remover/atualizar` e `lume atualizar` tiram uma foto antes de mexer.
- O painel do `lume` mostra a última foto.

Quem tem a 1.1.1: `lume atualizar` (a máquina do tempo liga sozinha, pelo `cron`, em até 15 minutos). Para a janela do Dolphin, `pkg install kdialog`.

## Novo na 1.1.1

- **`lume atualizar`**: atualizações num ambiente de boot à parte, testadas no boot seguinte e confirmadas por uma checagem de saúde. Se o boot falhar, travar ou a checagem reprovar, o computador volta sozinho para o sistema anterior.
- Instalador: ambientes de boot funcionando de fato (`canmount=noauto`, sem raiz fixa no `loader.conf`).
- Quem tem a 1.1.0 instalada atualiza com: `fetch -o /tmp/lume-atualizar https://raw.githubusercontent.com/Kalel047/lumebsd/main/fonte/usr.sbin/lume-configurar/lume-atualizar.sh && sh /tmp/lume-atualizar aplicar`

Os arquivos `atualizacao.txt` e `LumeBSD-1.1.2-sistema.tar.zst` desta Release são o que o `lume atualizar` baixa.

## O que tem

- **Pendrive ao vivo**: testa o hardware sem tocar no disco (`lume hardware`, `lume video testar`, `lume wifi`).
- **Instalação** num disco inteiro: ZFS sobre GELI (AES-256-XTS) com senha no boot, swap criptografada, ambientes de boot, UEFI.
- **Intel + NVIDIA**: driver NVIDIA escolhido pela geração da placa; Intel em notebooks e híbridos; volta sozinho ao modo sem aceleração se um driver travar.
- **KDE Plasma 6** com o **tema Papel** (estética e-ink), em português, teclado ABNT2.
- **forja**: pacotes binários ou compilados com as suas opções, no estilo Portage.
- **vigia**: serviços supervisionados, com limites e sockets sob demanda.

## Como usar

1. Baixe `LumeBSD-1.1-Farol-pendrive.img.xz` e confira o SHA256 (`SHA256SUMS`).
2. Extraia (7-Zip no Windows; `xz -d` no Linux/FreeBSD) e grave num pendrive de 8 GB ou mais: **Rufus em modo DD** no Windows, ou `dd` no Linux/FreeBSD.
3. Desligue o Secure Boot e dê boot pelo pendrive.
4. Rode `lume hardware` e `lume video testar`; se estiver tudo certo, `lume instalar` (apaga o disco escolhido).

## Limitações conhecidas

- Testado no QEMU (UEFI); em hardware real, só pelo `lume hardware` de cada um. O driver NVIDIA não foi testado com placa real.
- Notebooks híbridos Intel + NVIDIA usam só a Intel.
- O Secure Boot precisa estar desligado.

Documentação: [README](https://github.com/Kalel047/lumebsd#readme) e [arquitetura](https://github.com/Kalel047/lumebsd/blob/main/docs/ARQUITETURA.md).
