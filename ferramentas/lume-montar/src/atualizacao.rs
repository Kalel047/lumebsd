//! Gera o pacote de atualização do LumeBSD (para "lume atualizar").
//!
//! O pacote traz /usr/share/lume/versao e /usr/share/lume/sistema.mtree (a
//! lista do sistema, que o "lume atualizar" usa para saber o que mudou, o
//! que saiu e o que é configuração do usuário).

use std::io::Read;

use regex::Regex;
use sha2::{Digest, Sha256};

use crate::sistema::{com_originais, extras_lume, linhas_do_build, sem_campos};
use crate::util::{absoluto, apagar_se_existe, caminho, criar_pastas, gravar, ler, rodar, s, sair, tamanho};
use crate::v;

pub const USO: &str = "uso: lume-montar atualizacao STAGE VERSAO SAIDA [--notas ARQUIVO.md]
     [--minimo VERSAO --intermediario URL]
     [--substituir CAMINHO ARQUIVO]...   (só para testes: troca um arquivo)
     [--base X.Y --osversion N]          (só para testes: finge outra base)

STAGE   saída de \"installworld distribution installkernel -DNO_ROOT\"
        (usa o METALOG original do build, sem o que é só do pendrive)
VERSAO  versão completa, por exemplo 1.1.1
SAIDA   diretório onde ficam:
          LumeBSD-VERSAO-sistema.tar.zst  o sistema base inteiro, com donos,
                                          permissões e flags do METALOG
          LumeBSD-VERSAO-notas.md         as novidades (com --notas)
          atualizacao.txt                 o índice: versão, base FreeBSD (base,
                                          abi, osversion), arquivo, SHA256,
                                          tamanho, notas; minimo/intermediario
                                          quando quem tem uma versão antiga
                                          precisa passar por outra antes";

pub fn main(args: &[String]) {
    let mut args: Vec<String> = args.to_vec();
    let mut opcoes: std::collections::HashMap<&str, String> = Default::default();
    for nome in ["--notas", "--minimo", "--intermediario", "--base", "--osversion"] {
        if let Some(i) = args.iter().position(|a| a == nome) {
            if i + 1 >= args.len() {
                sair(USO);
            }
            opcoes.insert(nome, args[i + 1].clone());
            args.drain(i..i + 2);
        }
    }
    let mut trocas: Vec<(String, String)> = vec![];
    while let Some(i) = args.iter().position(|a| a == "--substituir") {
        if i + 2 >= args.len() {
            sair(USO);
        }
        trocas.push((args[i + 1].trim_start_matches('/').to_string(), s(&absoluto(&args[i + 2]))));
        args.drain(i..i + 3);
    }
    if args.len() != 3 || !Regex::new(r"^\d+\.\d+\.\d+$").unwrap().is_match(&args[1]) {
        sair(USO);
    }
    let (stage, versao, saida_dir) = (absoluto(&args[0]), args[1].clone(), absoluto(&args[2]));
    criar_pastas(&saida_dir);

    // A base FreeBSD, do __FreeBSD_version do próprio sistema (1501000 é a
    // 15.1): é ela, e não o número do LumeBSD, que diz se os programas
    // instalados (drivers do kernel, pacotes) servem na versão nova.
    let osversion: u64 = match opcoes.get("--osversion") {
        Some(o) => o.parse().unwrap_or_else(|_| sair(USO)),
        None => {
            let param = ler(stage.join("usr/include/sys/param.h"));
            Regex::new(r"#define\s+__FreeBSD_version\s+(\d+)")
                .unwrap()
                .captures(&param)
                .and_then(|c| c[1].parse().ok())
                .unwrap_or_else(|| sair("sem o __FreeBSD_version em sys/param.h"))
        }
    };
    let base = opcoes
        .get("--base")
        .cloned()
        .unwrap_or_else(|| format!("{}.{}", osversion / 100000, osversion / 1000 % 100));
    let abi = format!("FreeBSD:{}:amd64", base.split('.').next().unwrap());

    let (cabecalho, linhas) = linhas_do_build(&stage);
    let mut todas = com_originais(&stage, &linhas);
    todas.extend(extras_lume(&stage, &cabecalho, &linhas, &versao));

    // Arquivos trocados (testes): o pacote leva o arquivo dado no lugar.
    let mut spec = vec![cabecalho.clone()];
    for l in &todas {
        let mut l = l.clone();
        for (rel, arq) in &trocas {
            if caminho(&l) == format!("./{rel}") {
                l = format!("{} contents={arq}", sem_campos(&l, "size|sha256digest|md5digest"));
            }
        }
        spec.push(l);
    }
    let arq_spec = saida_dir.join("pacote.mtree");
    gravar(&arq_spec, spec.join("\n") + "\n");
    let nome = format!("LumeBSD-{versao}-sistema.tar.zst");
    let pacote = saida_dir.join(&nome);
    apagar_se_existe(&pacote);
    rodar(
        &v![
            "bsdtar", "-c", "--zstd", "--options", "zstd:compression-level=12",
            "-f", s(&pacote), format!("@{}", s(&arq_spec))
        ],
        Some(&stage),
    );
    std::fs::remove_file(&arq_spec).ok();

    let mut h = Sha256::new();
    let mut f = std::fs::File::open(&pacote).unwrap_or_else(|e| sair(format!("{e}")));
    let mut buf = vec![0u8; 1 << 20];
    loop {
        let n = f.read(&mut buf).unwrap_or_else(|e| sair(format!("{e}")));
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    let soma = format!("{:x}", h.finalize());
    let mut indice = format!(
        "versao={versao}\nbase={base}\nabi={abi}\nosversion={osversion}\n\
         arquivo={nome}\nsha256={soma}\ntamanho={}\n",
        tamanho(&pacote)
    );
    if let Some(n) = opcoes.get("--notas") {
        let notas = format!("LumeBSD-{versao}-notas.md");
        gravar(saida_dir.join(&notas), ler(n));
        indice += &format!("notas={notas}\n");
    }
    if let Some(m) = opcoes.get("--minimo") {
        let inter = opcoes.get("--intermediario").cloned().unwrap_or_else(|| sair(USO));
        indice += &format!("minimo={m}\nintermediario={inter}\n");
    }
    gravar(saida_dir.join("atualizacao.txt"), indice);
    println!(
        "{nome}: base FreeBSD {base} ({osversion}), {} MiB, {} entradas, sha256 {soma}",
        tamanho(&pacote) / (1 << 20),
        todas.len()
    );
}
