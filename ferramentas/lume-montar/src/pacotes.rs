//! Baixa pacotes oficiais do FreeBSD 15 (com dependências) e confere cada um.
//!
//! Lê os catálogos assinados de pkg.freebsd.org (quarterly e kmods do 15.1),
//! resolve as dependências e confere o checksum de cada arquivo contra o
//! catálogo: BLAKE2b-512 em z-base32, o formato "2$" do pkg.
//! Precisa de curl e bsdtar (o catálogo vem comprimido com zstd).

use std::collections::HashSet;
use std::path::Path;

use blake2::{Blake2b512, Digest};
use indexmap::IndexMap;
use serde_json::Value;

use crate::util::{casa, criar_pastas, rodar, s, sair};
use crate::v;

pub const USO: &str = "uso: lume-montar pacotes [--sem-dependencias] DESTINO pacote [pacote ...]
Um nome com * é um padrão: 'gpu-firmware-intel-kmod-*' pega todos.";

pub const SITE: &str = "https://pkg.freebsd.org/FreeBSD:15:amd64";
const REPOS: [&str; 2] = ["kmods_quarterly_1", "quarterly"]; // drivers do kernel primeiro
const ALFA: &[u8] = b"ybndrfg8ejkmcpqxot1uwisza345h769";

/// O checksum do pkg: BLAKE2b-512 em z-base32, os bits do menos
/// significativo para o mais, de cinco em cinco.
pub fn soma_pkg(caminho: &Path) -> String {
    let dados = std::fs::read(caminho).unwrap_or_default();
    let h = Blake2b512::digest(&dados);
    let bits: Vec<u8> = h.iter().flat_map(|b| (0..8).map(move |i| (b >> i) & 1)).collect();
    let mut r = String::from("2$");
    for bloco in bits.chunks(5) {
        let v: usize = bloco.iter().enumerate().map(|(i, b)| (*b as usize) << i).sum();
        r.push(ALFA[v] as char);
    }
    r
}

/// Lê um packagesite.yaml (um objeto JSON por linha).
pub fn ler_indice(caminho: &Path, repo: &str, idx: &mut IndexMap<String, Value>) {
    for linha in crate::util::ler(caminho).lines() {
        if linha.trim().is_empty() {
            continue;
        }
        let mut d: Value = serde_json::from_str(linha)
            .unwrap_or_else(|e| sair(format!("{}: {e}", caminho.display())));
        d["repo"] = Value::String(repo.into());
        let nome = d["name"].as_str().unwrap_or_default().to_string();
        idx.entry(nome).or_insert(d);
    }
}

/// Baixa e abre o catálogo de um repositório; devolve o packagesite.yaml.
pub fn baixar_catalogo(pasta: &Path, url: &str) -> std::path::PathBuf {
    criar_pastas(pasta);
    rodar(&v!["curl", "-sSf", "-o", s(&pasta.join("packagesite.pkg")), url], None);
    rodar(&v!["bsdtar", "-xf", "packagesite.pkg", "packagesite.yaml"], Some(pasta));
    pasta.join("packagesite.yaml")
}

fn catalogo(tmp: &Path) -> IndexMap<String, Value> {
    let mut idx = IndexMap::new();
    for repo in REPOS {
        let yaml = baixar_catalogo(&tmp.join(repo), &format!("{SITE}/{repo}/packagesite.pkg"));
        ler_indice(&yaml, repo, &mut idx);
    }
    idx
}

pub fn deps(d: &Value) -> Vec<String> {
    d.get("deps")
        .and_then(|x| x.as_object())
        .map(|m| m.keys().cloned().collect())
        .unwrap_or_default()
}

fn txt<'a>(d: &'a Value, campo: &str) -> &'a str {
    d.get(campo).and_then(|x| x.as_str()).unwrap_or("")
}

pub fn main(args: &[String]) {
    // --sem-dependencias: só os pedidos (os cabeçalhos do Qt para compilar
    // os apps não precisam do resto).
    let sem_deps = args.iter().any(|a| a == "--sem-dependencias");
    let args: Vec<&String> = args.iter().filter(|a| *a != "--sem-dependencias").collect();
    if args.is_empty() {
        sair(USO);
    }
    let destino = Path::new(args[0].as_str());
    let pedidos = &args[1..];
    criar_pastas(destino);
    let tmp = tempfile::tempdir().unwrap_or_else(|e| sair(format!("tmp: {e}")));
    let idx = catalogo(tmp.path());
    drop(tmp);

    let mut fila: Vec<String> = vec![];
    for p in pedidos {
        let achados: Vec<String> = if p.contains('*') {
            idx.keys().filter(|n| casa(p, n)).cloned().collect()
        } else {
            vec![p.to_string()]
        };
        if achados.is_empty() {
            sair(format!("nenhum pacote para o padrão: {p}"));
        }
        fila.extend(achados);
    }
    let mut escolhidos: Vec<&Value> = vec![];
    let mut vistos: HashSet<String> = HashSet::new();
    while let Some(nome) = fila.pop() {
        if vistos.contains(&nome) {
            continue;
        }
        let d = idx.get(&nome).unwrap_or_else(|| sair(format!("pacote não encontrado: {nome}")));
        vistos.insert(nome);
        escolhidos.push(d);
        if !sem_deps {
            fila.extend(deps(d));
        }
    }
    for d in &escolhidos {
        let (nome, versao, soma) = (txt(d, "name"), txt(d, "version"), txt(d, "sum"));
        let arq = destino.join(format!("{nome}-{versao}.pkg"));
        if arq.exists() && soma_pkg(&arq) == soma {
            continue;
        }
        let mut rel = txt(d, "path");
        if rel.is_empty() {
            rel = txt(d, "repopath");
        }
        let url = format!("{SITE}/{}/{rel}", txt(d, "repo"));
        rodar(&v!["curl", "-sSf", "-o", s(&arq), url], None);
        if soma_pkg(&arq) != soma {
            std::fs::remove_file(&arq).ok();
            sair(format!("checksum não confere: {nome}"));
        }
        println!("  {nome}-{versao}");
    }
    println!("{} pacotes conferidos em {}", escolhidos.len(), destino.display());
}
