
## O que tem

- **Pendrive ao vivo ou .iso**: testa o hardware sem tocar no disco (`lume hardware`, `lume video testar`, `lume wifi`). A `.iso` serve para DVD, máquina virtual ou Ventoy.
- **Instalação** num disco inteiro: ZFS sobre GELI (AES-256-XTS) com senha no boot, swap criptografada, ambientes de boot; dá boot em UEFI e em BIOS.
- **Atualizações que não quebram**, pelo app **Atualizações do LumeBSD** ou por `lume atualizar`: a versão nova entra num ambiente de boot à parte e só é confirmada depois de um boot saudável; se falhar, o computador volta sozinho.
- **Máquina do tempo** (`lume tempo`, e "Versões anteriores" no Dolphin): uma foto do disco por hora, guardada por dias e semanas.
- **Programas do LumeBSD**: a loja em português, com kits por área (Uso geral, Exatas, Sociais aplicadas) e busca pelo programa que você usava ("matlab" acha o Octave).
- **Janelas em abas** (Ctrl+Meta+E), como no BeOS, e **atributos nos arquivos** (etiquetas, nota, comentário e os seus), editados no botão direito do Dolphin e procurados com consultas salvas.
- **Programas do Windows e da web**: dois cliques num .exe (de 64 ou 32 bits) abrem com o Wine; Microsoft 365, Teams, Overleaf e outros viram apps no menu. Um .dmg do Mac mostra o que usar no lugar; o `lume-macho` (experimental) já roda programas do macOS que falam direto com o kernel.
- **Intel, AMD e NVIDIA**: driver NVIDIA escolhido pela geração da placa; amdgpu ou radeonkms pela geração da AMD; o vídeo integrado (Intel ou Ryzen) em notebooks híbridos; volta sozinho ao modo sem aceleração se um driver travar.
- **KDE Plasma 6** com o **tema Papel** (e o **Papel noite**, escuro: `lume tema escuro`) e o **filtro e-ink**: as janelas em papel e tinta, com o grão do papel e a tinta que assenta sem piscar (fotos, vídeos e tela cheia ficam com cor), em português, teclado ABNT2.
- **lume-motor**: o protótipo do motor de renderização vetorial, direto na placa de vídeo (docs/MOTOR.md).
- **forja**: pacotes binários ou compilados com as suas opções, no estilo Portage.
- **vigia**: serviços supervisionados, com limites e sockets sob demanda.

## Como usar

1. Baixe `LumeBSD-2.0-Farol-pendrive.img.xz` (ou a `LumeBSD-2.0-Farol.iso`, para DVD ou máquina virtual) e confira o SHA256 (`SHA256SUMS`).
2. Extraia (7-Zip no Windows; `xz -d` no Linux/FreeBSD) e grave num pendrive de 8 GB ou mais: **Rufus em modo DD** no Windows, ou `dd` no Linux/FreeBSD.
3. Desligue o Secure Boot e dê boot pelo pendrive. Numa máquina virtual (virt-manager, GNOME Boxes, VirtualBox, QEMU): 4 GB de memória, disco de 40 GB, UEFI ou BIOS.
4. Rode `lume hardware` e `lume video testar`; se estiver tudo certo, `lume instalar` (apaga o disco escolhido). Ele pergunta o kit de programas da sua área.

Já tem o LumeBSD instalado? Abra **Atualizações do LumeBSD** no menu (ou `lume atualizar`). Os arquivos `atualizacao.txt`, `LumeBSD-<versão>-sistema.tar.zst` e `LumeBSD-<versão>-notas.md` desta Release são o que ele baixa. Quem tem uma versão anterior à 1.2 atualiza uma vez com: `fetch -o /tmp/lume-atualizar https://raw.githubusercontent.com/Kalel047/lumebsd/main/fonte/usr.sbin/lume-configurar/lume-atualizar.sh && sh /tmp/lume-atualizar aplicar`

## Limitações conhecidas

- Testado no QEMU (UEFI e BIOS, como o virt-manager cria) e em hardware real com NVIDIA: Core i5-10400F com GeForce GTX 1650 e com GTX 1080 Ti. Placas AMD, Intel integrada e o `lume-motor` na tela ainda não foram testados em hardware real; rode `lume hardware` no pendrive antes de instalar.
- Notebooks híbridos Intel + NVIDIA e Ryzen + NVIDIA usam só o vídeo integrado.
- A viscosidade da tinta precisa de placa de vídeo (sem ela, o KDE desliga as animações e a viscosidade).
- O Secure Boot precisa estar desligado.
- Programas do Windows com proteção anticópia, drivers de equipamentos e programas grandes como o MATLAB e o SolidWorks costumam não rodar no Wine; a Ajuda explica as alternativas.
- Programas do Mac comuns ainda não rodam: o `lume-macho` não tem as bibliotecas do macOS (o caminho está em `docs/MACOS.md`).
- O filtro e-ink e as animações usam a placa de vídeo; sem aceleração, desligue o filtro com Ctrl+Meta+T se a tela ficar lenta. O filtro acompanha a versão do KWin do LumeBSD: se o KWin for atualizado à parte pelo `pkg`, ele fica desligado até a próxima versão do LumeBSD.

Documentação: [README](https://github.com/Kalel047/lumebsd#readme) e [arquitetura](https://github.com/Kalel047/lumebsd/blob/main/docs/ARQUITETURA.md).
