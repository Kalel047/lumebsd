//! Confere o catálogo da loja (lume loja) contra os pacotes do FreeBSD 15.
//!
//! Sem o packagesite.yaml, baixa o catálogo quarterly de pkg.freebsd.org.
//! Confere: 9 campos por linha, ids únicos, categorias e kits que existem,
//! URLs dos apps da web e, principalmente, que cada pacote existe no
//! repositório. Mostra o tamanho de cada kit (instalado, com as
//! dependências que ainda não vêm com o desktop).

use std::collections::HashSet;
use std::path::Path;

use indexmap::IndexMap;
use serde_json::Value;

use crate::pacotes::{baixar_catalogo, deps, ler_indice, SITE};
use crate::util::{ler, sair};

pub const USO: &str = "uso: lume-montar catalogo CATALOGO [packagesite.yaml]";

/// Os pacotes e todas as dependências.
fn fecho(idx: &IndexMap<String, Value>, nomes: &[String]) -> HashSet<String> {
    let mut vistos = HashSet::new();
    let mut fila: Vec<String> = nomes.to_vec();
    while let Some(n) = fila.pop() {
        if vistos.contains(&n) || !idx.contains_key(&n) {
            continue;
        }
        fila.extend(deps(&idx[&n]));
        vistos.insert(n);
    }
    vistos
}

fn num(d: &Value, campo: &str) -> f64 {
    d.get(campo).and_then(|x| x.as_f64()).unwrap_or(0.0)
}

pub fn main(args: &[String]) {
    if args.is_empty() || args.len() > 2 {
        sair(USO);
    }
    let cat = Path::new(&args[0]);
    let mut idx = IndexMap::new();
    let tmp = tempfile::tempdir().unwrap_or_else(|e| sair(format!("tmp: {e}")));
    let yaml = if args.len() == 2 {
        Path::new(&args[1]).to_path_buf()
    } else {
        baixar_catalogo(tmp.path(), &format!("{SITE}/quarterly/packagesite.pkg"))
    };
    ler_indice(&yaml, "quarterly", &mut idx);
    drop(tmp);

    let mut erros: Vec<String> = vec![];
    let mut cats: HashSet<String> = HashSet::new();
    let mut kits: IndexMap<String, Vec<String>> = IndexMap::new();
    let mut ids: HashSet<String> = HashSet::new();
    let mut itens: Vec<(String, String, Vec<String>, Vec<String>)> = vec![];
    for (n, linha) in ler(cat).lines().enumerate() {
        let n = n + 1;
        if linha.trim().is_empty() || linha.starts_with('#') {
            continue;
        }
        if let Some(r) = linha.strip_prefix("@categoria ") {
            let c = r.split_whitespace().next().unwrap_or("");
            cats.insert(c.split('|').next().unwrap_or("").to_string());
            continue;
        }
        if let Some(r) = linha.strip_prefix("@kit ") {
            let c: Vec<&str> = r.split('|').collect();
            let inclui = c.get(3).map(|x| x.split_whitespace().map(String::from).collect()).unwrap_or_default();
            kits.insert(c[0].to_string(), inclui);
            continue;
        }
        let c: Vec<&str> = linha.split('|').collect();
        if c.len() != 9 {
            erros.push(format!("linha {n}: {} campos (esperava 9)", c.len()));
            continue;
        }
        let (iid, categoria, nome, pacotes, ks) = (c[0], c[1], c[2], c[3], c[4]);
        if ids.contains(iid) {
            erros.push(format!("linha {n}: id repetido: {iid}"));
        }
        ids.insert(iid.to_string());
        itens.push((
            iid.into(),
            categoria.into(),
            pacotes.split_whitespace().map(String::from).collect(),
            ks.split_whitespace().map(String::from).collect(),
        ));
        if nome.is_empty() || c[6].is_empty() {
            erros.push(format!("linha {n}: {iid} sem nome ou descrição"));
        }
    }

    // O desktop já instalado (lume desktop): o que vier com ele não conta.
    let desktop = cat.parent().unwrap_or(Path::new(".")).join("../desktop.txt");
    let mut base = HashSet::new();
    if desktop.exists() {
        let nomes: Vec<String> = ler(&desktop)
            .lines()
            .map(|l| l.split('#').next().unwrap_or("").trim().to_string())
            .filter(|x| !x.is_empty())
            .collect();
        base = fecho(&idx, &nomes);
    }

    for (iid, categoria, pacotes, ks) in &itens {
        if !cats.contains(categoria) {
            erros.push(format!("{iid}: categoria desconhecida: {categoria}"));
        }
        for k in ks {
            if !kits.contains_key(k) {
                erros.push(format!("{iid}: kit desconhecido: {k}"));
            }
        }
        let web: Vec<&String> = pacotes.iter().filter(|p| p.starts_with("web:")).collect();
        if !web.is_empty() && (pacotes.len() != 1 || !web[0].starts_with("web:https://")) {
            erros.push(format!("{iid}: app da web precisa ser só web:https://..."));
        }
        for p in pacotes {
            if p.starts_with("web:") || p == "+linux" {
                continue;
            }
            if !idx.contains_key(p) {
                erros.push(format!("{iid}: pacote não existe no FreeBSD 15: {p}"));
            }
        }
    }
    for (k, inc) in &kits {
        for i in inc {
            if !kits.contains_key(i) {
                erros.push(format!("kit {k}: inclui kit desconhecido: {i}"));
            }
        }
    }

    for (k, inc) in &kits {
        let mut alvo: HashSet<&String> = inc.iter().collect();
        alvo.insert(k);
        let pk: Vec<String> = itens
            .iter()
            .filter(|(_, _, _, ks)| ks.iter().any(|x| alvo.contains(x)))
            .flat_map(|(_, _, ps, _)| ps.iter())
            .filter(|p| !p.starts_with("web:") && *p != "+linux")
            .cloned()
            .collect();
        let novos: Vec<String> = fecho(&idx, &pk).into_iter().filter(|p| !base.contains(p)).collect();
        let mb: f64 = novos.iter().map(|p| num(&idx[p], "flatsize")).sum::<f64>() / 1e6;
        let down: f64 = novos.iter().map(|p| num(&idx[p], "pkgsize")).sum::<f64>() / 1e6;
        println!(
            "kit {k}: {} pacotes, {} com dependências; baixa {down:.0} MB, ocupa {mb:.0} MB",
            pk.len(),
            novos.len()
        );
    }
    println!("{} programas em {} categorias", itens.len(), cats.len());
    if !erros.is_empty() {
        eprintln!("{}", erros.join("\n"));
        std::process::exit(1);
    }
}
