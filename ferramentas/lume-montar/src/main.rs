//! lume-montar: as ferramentas de construção do LumeBSD, num programa só
//! (antes eram scripts em Python). Cada parte é um subcomando:
//!
//!   pendrive      a imagem do pendrive (ao vivo + instalador) e a .iso
//!   atualizacao   o pacote de atualização (lume atualizar)
//!   pacotes       baixa e confere pacotes do FreeBSD 15
//!   app           compila os apps em Qt e os põe no stage
//!   motor         compila o lume-motor (a biblioteca e a demonstração)
//!   efeitos       compila os efeitos e a decoração do KWin
//!   mac           compila o lume-macho e o programa de teste do macOS
//!   fonte         gera e confere a cópia legível fonte/ dos patches
//!   tema-noite    gera o tema Papel noite a partir do Papel
//!   papel-de-parede  desenha o papel de parede Papel
//!   catalogo      confere o catálogo da loja contra os pacotes do FreeBSD
//!   papel-glsl    escreve PASTA/papel_glsl.inc (as contas do papel)

#![allow(clippy::useless_vec)]

mod atualizacao;
mod catalogo;
mod cbor;
mod compilar;
mod fonte;
mod pacotes;
mod papel;
mod pendrive;
mod sistema;
mod tema_noite;
mod util;

const USO: &str = "uso: lume-montar SUBCOMANDO [argumentos]

  pendrive [--iso ISO] STAGE PACOTES FERRAMENTAS IMAGEM [VERSAO]
  atualizacao STAGE VERSAO SAIDA [--notas ARQ] [--minimo V --intermediario URL]
  pacotes [--sem-dependencias] DESTINO pacote...
  app STAGE QT6BASE.pkg          | app --so-compilar RAIZ QT6BASE.pkg PASTA
  motor STAGE PACOTES            | motor --so-compilar RAIZ PACOTES PASTA
  efeitos STAGE PACOTES          | efeitos --so-compilar RAIZ PACOTES PASTA
  mac STAGE                      | mac --so-compilar RAIZ PASTA
  fonte gerar|conferir SRC
  tema-noite TEMA
  papel-de-parede FONTES SAIDA.png [noite]
  catalogo CATALOGO [packagesite.yaml]
  papel-glsl PASTA

\"lume-montar SUBCOMANDO\" sem argumentos mostra os detalhes.
Variáveis: LLVM_BIN (padrão /usr/lib/llvm-19/bin), LUME_TOPO (o repositório).";

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some(sub) = args.first() else { util::sair(USO) };
    let resto = &args[1..];
    match sub.as_str() {
        "pendrive" => pendrive::main(resto),
        "atualizacao" => atualizacao::main(resto),
        "pacotes" => pacotes::main(resto),
        "app" => compilar::app(resto),
        "motor" => compilar::motor(resto),
        "efeitos" => compilar::efeitos(resto),
        "mac" => compilar::mac(resto),
        "fonte" => fonte::main(resto),
        "tema-noite" => tema_noite::main(resto),
        "papel-de-parede" => papel::main(resto),
        "catalogo" => catalogo::main(resto),
        "papel-glsl" => match resto {
            [p] => sistema::embutir_papel(&util::absoluto(p)),
            _ => util::sair("uso: lume-montar papel-glsl PASTA"),
        },
        "-h" | "--help" | "ajuda" => println!("{USO}"),
        _ => util::sair(USO),
    }
}
