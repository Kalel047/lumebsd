//! Mantém fonte/ igual ao que os patches criam.
//!
//! Os patches em patches/ são a fonte de verdade (é deles que o build
//! parte), mas código dentro de um .patch é ruim de ler. fonte/ traz uma
//! cópia, com os mesmos caminhos da árvore do FreeBSD, de cada arquivo que o
//! LumeBSD acrescenta (forja, lume, vigia, lume-configurar, rc.d, tema...).

use std::fs;
use std::path::Path;

use regex::Regex;

use crate::util::{absoluto, criar_pastas, gravar, sair, topo};

pub const USO: &str = "uso: lume-montar fonte gerar   SRC    copia de SRC (FreeBSD com os patches) para fonte/
     lume-montar fonte conferir SRC   falha se fonte/ estiver diferente de SRC

SRC é uma árvore releng/15.1 com patches/*.patch aplicados em ordem.";

/// Arquivos criados e alterados pela série de patches, em ordem.
fn novos_e_alterados(patches: &Path) -> (Vec<String>, Vec<String>) {
    let diff = Regex::new(r"^diff --git a/(\S+) b/(\S+)").unwrap();
    let (mut novos, mut alterados): (Vec<String>, Vec<String>) = (vec![], vec![]);
    let mut nomes: Vec<String> = fs::read_dir(patches)
        .unwrap_or_else(|e| sair(format!("{}: {e}", patches.display())))
        .filter_map(|e| e.ok())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.ends_with(".patch"))
        .collect();
    nomes.sort();
    for nome in nomes {
        let dados = fs::read(patches.join(&nome)).unwrap_or_default();
        let texto = String::from_utf8_lossy(&dados);
        let mut atual: Option<String> = None;
        for linha in texto.split_inclusive('\n') {
            if let Some(m) = diff.captures(linha) {
                let a = m[2].to_string();
                if !novos.contains(&a) && !alterados.contains(&a) {
                    alterados.push(a.clone());
                }
                atual = Some(a);
            } else if linha.starts_with("new file mode") {
                if let Some(a) = &atual {
                    alterados.retain(|x| x != a);
                    if !novos.contains(a) {
                        novos.push(a.clone());
                    }
                }
            } else if linha.starts_with("deleted file mode") {
                if let Some(a) = &atual {
                    novos.retain(|x| x != a);
                    alterados.retain(|x| x != a);
                }
            }
        }
    }
    novos.sort();
    alterados.sort();
    (novos, alterados)
}

fn todos(pasta: &Path, base: &Path, r: &mut Vec<String>) {
    if let Ok(it) = fs::read_dir(pasta) {
        for e in it.filter_map(|e| e.ok()) {
            let p = e.path();
            if p.is_dir() {
                todos(&p, base, r);
            } else {
                r.push(p.strip_prefix(base).unwrap().to_string_lossy().into_owned());
            }
        }
    }
}

pub fn main(args: &[String]) {
    if args.len() != 2 || !["gerar", "conferir"].contains(&args[0].as_str()) {
        sair(USO);
    }
    let (modo, src) = (args[0].as_str(), absoluto(&args[1]));
    let fonte = topo().join("fonte");
    let (novos, alterados) = novos_e_alterados(&topo().join("patches"));
    let mut erros = vec![];
    if modo == "gerar" {
        fs::remove_dir_all(&fonte).ok();
    }
    for rel in &novos {
        let origem = src.join(rel);
        let destino = fonte.join(rel);
        if !origem.is_file() {
            erros.push(format!("falta em SRC: {rel}"));
        } else if modo == "gerar" {
            criar_pastas(destino.parent().unwrap());
            fs::copy(&origem, &destino).unwrap_or_else(|e| sair(format!("{rel}: {e}")));
        } else if !destino.is_file() {
            erros.push(format!("falta em fonte/: {rel}"));
        } else if fs::read(&origem).ok() != fs::read(&destino).ok() {
            erros.push(format!("diferente: {rel}"));
        }
    }
    if modo == "conferir" {
        let mut achados = vec![];
        todos(&fonte, &fonte, &mut achados);
        for rel in achados {
            if rel != "LEIAME.md" && !novos.contains(&rel) {
                erros.push(format!("sobrando em fonte/: {rel}"));
            }
        }
    }
    if modo == "gerar" {
        let mut t = String::from(
            "# Código do LumeBSD\n\n\
             Cópia legível dos arquivos que o LumeBSD acrescenta ao \
             FreeBSD 15.1, nos mesmos caminhos da árvore do FreeBSD.\n\
             A fonte de verdade são os `patches/`; esta pasta é gerada \
             por `ferramentas/montar fonte gerar` e conferida no CI.\n\n\
             ## Arquivos do FreeBSD que os patches alteram\n\n\
             Estes só aparecem nos patches (são mudanças pequenas \
             em arquivos do FreeBSD):\n\n",
        );
        for rel in &alterados {
            t += &format!("- `{rel}`\n");
        }
        gravar(fonte.join("LEIAME.md"), t);
    }
    for e in &erros {
        println!("{e}");
    }
    if !erros.is_empty() {
        std::process::exit(1);
    }
    println!(
        "fonte/: {} arquivos {}",
        novos.len(),
        if modo == "gerar" { "gerados" } else { "conferidos" }
    );
}
