//! Partes comuns ao pendrive e ao pacote de atualização: a lista do sistema.
//!
//! O METALOG do "installworld -DNO_ROOT" diz cada arquivo do sistema base,
//! com dono, permissões, flags e a tag "config" nos arquivos de
//! configuração. O LumeBSD guarda essa lista (sem tamanhos e hashes) em
//! /usr/share/lume/sistema.mtree e a versão completa em
//! /usr/share/lume/versao.

use std::collections::HashSet;
use std::path::Path;
use std::sync::OnceLock;

use regex::Regex;
use sha2::{Digest, Sha256};

use crate::util::{caminho, copiar, criar_pastas, gravar, ler, sair, topo};

pub const RAIZ: &str = "uname=root gname=wheel";
pub const LISTA: &str = "usr/share/lume/sistema.mtree";
pub const VERSAO: &str = "usr/share/lume/versao";

fn re(cel: &'static OnceLock<Regex>, padrao: &str) -> &'static Regex {
    cel.get_or_init(|| Regex::new(padrao).unwrap())
}

/// Tira os campos que mudam a cada build (tamanhos, hashes, datas).
pub fn sem_campos(linha: &str, campos: &str) -> String {
    Regex::new(&format!(r" ({campos})=\S+"))
        .unwrap()
        .replace_all(linha, "")
        .into_owned()
}

/// Cabeçalho e linhas do METALOG original do build.
///
/// O montar-pendrive acrescenta ao METALOG o que é só do pendrive; a cópia
/// METALOG.build guarda o original na primeira vez.
pub fn linhas_do_build(stage: &Path) -> (String, Vec<String>) {
    let metalog = stage.join("METALOG");
    let original = stage.join("METALOG.build");
    if !original.exists() {
        copiar(&metalog, &original);
    }
    let texto = ler(&original);
    let mut it = texto.lines();
    let cab = it.next().unwrap_or("").to_string();
    (cab, it.filter(|l| !l.trim().is_empty()).map(String::from).collect())
}

/// Grava as linhas novas no METALOG.build, no lugar das do mesmo caminho
/// (e das que começam com `fora`, se dado), sem repetir se rodar de novo.
pub fn atualizar_build(stage: &Path, novas: &[String], fora: Option<&str>) {
    linhas_do_build(stage);
    let build = stage.join("METALOG.build");
    let caminhos: HashSet<&str> = novas.iter().map(|l| caminho(l)).collect();
    let texto = ler(&build);
    let mut linhas: Vec<String> = texto
        .lines()
        .filter(|l| !l.trim().is_empty())
        .filter(|l| !caminhos.contains(caminho(l)))
        .filter(|l| fora.is_none_or(|f| !caminho(l).starts_with(f)))
        .map(String::from)
        .collect();
    linhas.extend(novas.iter().cloned());
    gravar(&build, linhas.join("\n") + "\n");
}

/// Grava versao e sistema.mtree no stage e devolve as linhas deles.
///
/// A lista inclui as duas entradas, para o sistema instalado (e o
/// "lume atualizar") saberem que esses arquivos são do sistema.
pub fn extras_lume(stage: &Path, cab: &str, linhas: &[String], versao: &str) -> Vec<String> {
    criar_pastas(stage.join("usr/share/lume"));
    gravar(stage.join(VERSAO), format!("{versao}\n"));
    let novas = vec![
        format!("./{VERSAO} type=file {RAIZ} mode=0444 tags=package=lume"),
        format!("./{LISTA} type=file {RAIZ} mode=0444 tags=package=lume"),
    ];
    let caminhos: HashSet<&str> = novas.iter().map(|l| caminho(l)).collect();
    let mut sistema = vec![cab.to_string()];
    for l in linhas.iter().filter(|l| !caminhos.contains(caminho(l))).chain(novas.iter()) {
        sistema.push(com_digest(stage, &sem_campos(l, "size|sha256digest|md5digest|time|contents")));
    }
    gravar(stage.join(LISTA), sistema.join("\n") + "\n");
    novas
}

/// A mesma regra do lume-atualizar: tag config, fora rc.d/periodic/defaults.
pub fn e_config(linha: &str) -> bool {
    static TAG: OnceLock<Regex> = OnceLock::new();
    static FORA: OnceLock<Regex> = OnceLock::new();
    re(&TAG, r" tags=([^ ]*,)?config(,| |$)").is_match(linha)
        && linha.contains(" type=file")
        && !re(&FORA, r"^\./etc/(rc\.d|defaults|periodic)/").is_match(linha)
}

/// Configurações levam o SHA256 da versão do sistema: na próxima
/// atualização, uma que o usuário não mexeu é trocada pela nova (em vez de
/// ficar a velha e a nova ao lado, .lume-novo).
pub fn com_digest(stage: &Path, linha: &str) -> String {
    if !e_config(linha) {
        return linha.to_string();
    }
    let rel = &caminho(linha)[2..];
    let mut arq = stage.join(rel);
    let build = stage.join(format!("{rel}.build"));
    if rel == "etc/motd.template" && build.exists() {
        arq = build;
    }
    if !arq.is_file() {
        return linha.to_string();
    }
    let dados = std::fs::read(&arq).unwrap_or_else(|e| sair(format!("{}: {e}", arq.display())));
    format!("{linha} sha256digest={:x}", Sha256::digest(&dados))
}

/// Linhas do METALOG apontando, onde preciso, para o arquivo original.
///
/// O pendrive troca a mensagem de boas-vindas no stage; a original fica em
/// etc/motd.template.build e é ela que vai no pacote de atualização.
pub fn com_originais(stage: &Path, linhas: &[String]) -> Vec<String> {
    let motd = stage.join("etc/motd.template.build");
    linhas
        .iter()
        .map(|l| {
            if caminho(l) == "./etc/motd.template" && motd.exists() {
                format!("{} contents={}", sem_campos(l, "size|sha256digest|md5digest"), motd.display())
            } else {
                l.clone()
            }
        })
        .collect()
}

/// Escreve pasta/papel_glsl.inc: o app/lume-motor/papel.glsl (as contas do
/// papel, comuns ao lume-motor e ao lume-tinta) numa macro de texto,
/// LUME_PAPEL_GLSL, que serve ao C e ao C++.
pub fn embutir_papel(pasta: &Path) {
    let fonte = ler(topo().join("app/lume-motor/papel.glsl"));
    let mut linhas = vec![
        "/* Gerado de app/lume-motor/papel.glsl; não edite. */".to_string(),
        "#define LUME_PAPEL_GLSL \\".to_string(),
    ];
    for l in fonte.lines() {
        if l.trim_start().starts_with("//") || l.trim().is_empty() {
            continue;
        }
        let l = l.replace('\\', "\\\\").replace('"', "\\\"");
        linhas.push(format!("    \"{l}\\n\" \\"));
    }
    linhas.push("    \"\"".to_string());
    criar_pastas(pasta);
    gravar(pasta.join("papel_glsl.inc"), linhas.join("\n") + "\n");
}
