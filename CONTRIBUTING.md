# Como contribuir com o LumeBSD

Obrigado pelo interesse. O LumeBSD é um sistema para quem fala português:
estudantes e professores de exatas e de sociais aplicadas, e quem só quer
um computador que funcione. Toda contribuição que deixa o sistema mais
fácil para essas pessoas é bem-vinda, e não precisa ser código.

Ao participar, você concorda com o [Código de Conduta](CODE_OF_CONDUCT.md).

## Sem programar

- **Relato de hardware**: rodou o pendrive no seu computador? Conte como foi
  com o modelo [Relato de hardware](https://github.com/Kalel047/lumebsd/issues/new?template=hardware.yml),
  colando a saída de `lume hardware` e `lume video testar`. É a informação
  mais valiosa que o projeto pode receber, porque os testes daqui são só no
  QEMU.
- **Bugs**: use o modelo [Algo não funciona](https://github.com/Kalel047/lumebsd/issues/new?template=bug.yml).
- **Programas para o catálogo** e **kits**: sugira com o modelo
  [Ideia ou pedido](https://github.com/Kalel047/lumebsd/issues/new?template=ideia.yml).
  Diga o programa do Windows ou do Mac que ele substitui; é assim que a
  busca da loja acha.
- **Textos**: a Ajuda, o README e as mensagens do sistema devem ser claros
  para quem nunca abriu um terminal. Correções de português e de clareza
  contam muito.

Falhas de segurança não vão em issue pública: veja [SECURITY.md](SECURITY.md).

## Como o repositório funciona

O LumeBSD é o FreeBSD 15.1 com uma série de patches, mais o que fica fora
da árvore do FreeBSD:

| Pasta | O que é |
|---|---|
| `patches/` | a fonte de verdade: o build clona o `releng/15.1` (no commit de `FREEBSD_COMMIT`, em `build-lumebsd.sh`) e aplica os patches em ordem |
| `fonte/` | uma cópia legível dos arquivos que os patches criam ou mudam, com os mesmos caminhos da árvore do FreeBSD; gerada, nunca editada à mão |
| `app/` | os apps Qt 6 (Atualizações e Programas do LumeBSD) |
| `ferramentas/` | build, pacotes, imagem do pendrive, catálogo |
| `tests/` | o teste no QEMU, pelo console serial |
| `docs/` | arquitetura, novidades da versão, sessões de teste e capturas |

A arquitetura inteira está em [docs/ARQUITETURA.md](docs/ARQUITETURA.md).

### Mudar algo do sistema

1. Monte a árvore: clone o `releng/15.1` no commit de `FREEBSD_COMMIT` e
   aplique `patches/*.patch` em ordem com `git am` (ou `git apply`).
2. Faça a mudança nessa árvore e um commit.
3. Gere o patch novo e atualize `fonte/`:

   ```sh
   git -C SRC format-patch -N --no-signature --start-number NN COMMIT~1..COMMIT -o patches/
   python3 ferramentas/fonte.py gerar SRC
   ```

4. Confira: `python3 ferramentas/fonte.py conferir SRC` não acusa diferença.

Para mudanças pequenas num script, editar a cópia em `fonte/` ajuda a
mostrar a ideia num pull request; quem mantém o projeto transforma em patch.

### Mudar as cores do tema

O tema Papel fica em `usr.sbin/lume-configurar/tema`. O Papel noite (escuro)
é gerado dele: depois de mudar uma cor do Papel, rode

```sh
python3 ferramentas/tema-noite.py SRC/usr.sbin/lume-configurar/tema
```

e ponha a cor nova e a sua par escura no `MAPA` do script, se ele reclamar.
O CI confere que os arquivos do Papel noite são os que o script gera.

### Adicionar um programa ao catálogo

O catálogo da loja é `usr.sbin/lume-configurar/loja/catalogo`, uma linha por
programa (o formato está no começo do arquivo). Antes de mandar:

```sh
python3 ferramentas/conferir-catalogo.py fonte/usr.sbin/lume-configurar/loja/catalogo
```

Ele confere que cada pacote existe no repositório quarterly do FreeBSD 15 e
mostra quanto cada kit baixa. Um programa entra num kit só se a maioria das
pessoas daquela área usa; o resto fica na loja.

### Testar

- **Rápido** (o mesmo que o CI roda a cada push): `shellcheck -s sh -S error`
  nos scripts, `mandoc -T lint` nos manuais, `conferir-catalogo.py` e
  `fonte.py conferir`.
- **Completo**: `./build-lumebsd.sh` compila tudo e gera a imagem (horas
  numa máquina de 2 núcleos; dependências no README), e
  `tests/teste-pendrive.py` faz o caminho de um usuário no QEMU. A seção
  "Testando" do README lista as fases.

Diga no pull request como você testou.

## Estilo

- **Português** em tudo o que o usuário vê, nos comentários e nos commits.
  Para o usuário, sem jargão: "a versão anterior", não "o boot environment".
- Scripts em `sh` (POSIX, o do FreeBSD), não `bash`. Código C no estilo do
  FreeBSD ([style(9)](https://man.freebsd.org/style/9)).
- Mensagem de commit: uma primeira linha curta dizendo o que muda, e no
  corpo o porquê.
- Uma mudança por pull request.

## Licença

Contribuições entram sob a [licença BSD de 2 cláusulas](LICENSE) do
projeto. O código do FreeBSD segue as licenças dele.
