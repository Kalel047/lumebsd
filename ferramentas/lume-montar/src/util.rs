//! O que todas as partes usam: rodar programas, achar pacotes, ler e gravar.

use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

/// Sai com a mensagem (como o sys.exit do Python: código 1, no stderr).
pub fn sair(msg: impl AsRef<str>) -> ! {
    eprintln!("{}", msg.as_ref());
    std::process::exit(1)
}

/// O topo do repositório: LUME_TOPO, ou duas pastas acima deste crate.
pub fn topo() -> PathBuf {
    if let Some(t) = std::env::var_os("LUME_TOPO") {
        return PathBuf::from(t);
    }
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .canonicalize()
        .unwrap_or_else(|_| PathBuf::from(concat!(env!("CARGO_MANIFEST_DIR"), "/../..")))
}

/// A pasta dos programas do LLVM (LLVM_BIN).
pub fn llvm(prog: &str) -> String {
    let d = std::env::var("LLVM_BIN").unwrap_or_else(|_| "/usr/lib/llvm-19/bin".into());
    format!("{d}/{prog}")
}

/// Caminho absoluto (sem resolver links, como o os.path.abspath).
pub fn absoluto(p: impl AsRef<Path>) -> PathBuf {
    let p = p.as_ref();
    let base = if p.is_absolute() {
        PathBuf::new()
    } else {
        std::env::current_dir().unwrap_or_else(|e| sair(format!("pasta atual: {e}")))
    };
    let mut r = PathBuf::new();
    for c in base.join(p).components() {
        match c {
            std::path::Component::ParentDir => {
                r.pop();
            }
            std::path::Component::CurDir => {}
            c => r.push(c),
        }
    }
    r
}

pub fn s(p: &Path) -> String {
    p.to_string_lossy().into_owned()
}

fn mostrar(args: &[String]) -> String {
    args.join(" ")
}

/// Roda e falha se o programa falhar (check=True).
pub fn rodar(args: &[String], cwd: Option<&Path>) {
    let mut c = Command::new(&args[0]);
    c.args(&args[1..]);
    if let Some(d) = cwd {
        c.current_dir(d);
    }
    match c.status() {
        Ok(st) if st.success() => {}
        Ok(st) => sair(format!("falhou ({st}): {}", mostrar(args))),
        Err(e) => sair(format!("{}: {e}", args[0])),
    }
}

/// Roda sem conferir o resultado, sem a saída de erro (o bsdtar das pastas
/// que faltam num pacote).
pub fn rodar_quieto(args: &[String]) {
    let _ = Command::new(&args[0])
        .args(&args[1..])
        .stderr(Stdio::null())
        .status();
}

/// Roda e devolve a saída padrão (capture_output, check=True).
pub fn saida(args: &[String], cwd: Option<&Path>) -> String {
    let mut c = Command::new(&args[0]);
    c.args(&args[1..]);
    if let Some(d) = cwd {
        c.current_dir(d);
    }
    match c.output() {
        Ok(o) if o.status.success() => String::from_utf8_lossy(&o.stdout).into_owned(),
        Ok(o) => {
            std::io::stderr().write_all(&o.stderr).ok();
            sair(format!("falhou ({}): {}", o.status, mostrar(args)))
        }
        Err(e) => sair(format!("{}: {e}", args[0])),
    }
}

/// Vários argumentos de uma vez: v!["a", x, ...] como Vec<String>.
#[macro_export]
macro_rules! v {
    ($($x:expr),* $(,)?) => { vec![$($x.to_string()),*] };
}

pub fn ler(p: impl AsRef<Path>) -> String {
    let p = p.as_ref();
    fs::read_to_string(p).unwrap_or_else(|e| sair(format!("{}: {e}", p.display())))
}

pub fn gravar(p: impl AsRef<Path>, conteudo: impl AsRef<[u8]>) {
    let p = p.as_ref();
    fs::write(p, conteudo).unwrap_or_else(|e| sair(format!("{}: {e}", p.display())))
}

pub fn criar_pastas(p: impl AsRef<Path>) {
    let p = p.as_ref();
    fs::create_dir_all(p).unwrap_or_else(|e| sair(format!("{}: {e}", p.display())))
}

pub fn copiar(de: impl AsRef<Path>, para: impl AsRef<Path>) {
    let (de, para) = (de.as_ref(), para.as_ref());
    // Como o shutil.copy: para dentro de uma pasta, com o mesmo nome.
    let destino = if para.is_dir() {
        para.join(de.file_name().unwrap())
    } else {
        para.to_path_buf()
    };
    fs::copy(de, &destino)
        .unwrap_or_else(|e| sair(format!("{} -> {}: {e}", de.display(), destino.display())));
}

pub fn apagar_se_existe(p: impl AsRef<Path>) {
    let p = p.as_ref();
    if p.symlink_metadata().is_ok() {
        if p.is_dir() && !p.is_symlink() {
            fs::remove_dir_all(p).ok();
        } else {
            fs::remove_file(p).ok();
        }
    }
}

pub fn link(alvo: impl AsRef<Path>, nome: impl AsRef<Path>) {
    let nome = nome.as_ref();
    apagar_se_existe(nome);
    std::os::unix::fs::symlink(alvo.as_ref(), nome)
        .unwrap_or_else(|e| sair(format!("{}: {e}", nome.display())));
}

pub fn tamanho(p: impl AsRef<Path>) -> u64 {
    fs::metadata(p.as_ref()).map(|m| m.len()).unwrap_or(0)
}

/// O primeiro NOME-<número>*.pkg da pasta (em ordem), como o glob do Python.
pub fn achar_pacote(pasta: &Path, nome: &str, quem: &str) -> PathBuf {
    let mut achados: Vec<String> = fs::read_dir(pasta)
        .map(|it| {
            it.filter_map(|e| e.ok())
                .map(|e| e.file_name().to_string_lossy().into_owned())
                .filter(|n| {
                    n.ends_with(".pkg")
                        && n.strip_prefix(nome)
                            .and_then(|r| r.strip_prefix('-'))
                            .is_some_and(|r| r.starts_with(|c: char| c.is_ascii_digit()))
                })
                .collect()
        })
        .unwrap_or_default();
    achados.sort();
    match achados.first() {
        Some(n) => pasta.join(n),
        None => sair(format!("{quem}: falta o pacote {nome} em {}", pasta.display())),
    }
}

/// Copia um arquivo comprimindo com gzip (o manual), sem data no cabeçalho.
pub fn gzip(de: &Path, para: &Path) {
    let dados = fs::read(de).unwrap_or_else(|e| sair(format!("{}: {e}", de.display())));
    let f = fs::File::create(para).unwrap_or_else(|e| sair(format!("{}: {e}", para.display())));
    let mut g = flate2::GzBuilder::new()
        .mtime(0)
        .write(f, flate2::Compression::best());
    g.write_all(&dados).and_then(|_| g.finish().map(|_| ()))
        .unwrap_or_else(|e| sair(format!("{}: {e}", para.display())));
}

/// O primeiro campo (até o espaço) de uma linha do METALOG.
pub fn caminho(linha: &str) -> &str {
    linha.split_whitespace().next().unwrap_or("")
}

/// Divide nos espaços, no máximo em n partes (o str.split(None, n-1)).
pub fn dividir(linha: &str, n: usize) -> Vec<&str> {
    let mut r = Vec::new();
    let mut resto = linha.trim_start();
    while !resto.is_empty() {
        if r.len() == n - 1 {
            r.push(resto);
            break;
        }
        match resto.find(char::is_whitespace) {
            Some(i) => {
                r.push(&resto[..i]);
                resto = resto[i..].trim_start();
            }
            None => {
                r.push(resto);
                break;
            }
        }
    }
    r
}

/// Um padrão com * e ? (o fnmatch).
pub fn casa(padrao: &str, nome: &str) -> bool {
    fn rec(p: &[char], n: &[char]) -> bool {
        match p.first() {
            None => n.is_empty(),
            Some('*') => (0..=n.len()).any(|i| rec(&p[1..], &n[i..])),
            Some('?') => !n.is_empty() && rec(&p[1..], &n[1..]),
            Some(c) => n.first() == Some(c) && rec(&p[1..], &n[1..]),
        }
    }
    let p: Vec<char> = padrao.chars().collect();
    let n: Vec<char> = nome.chars().collect();
    rec(&p, &n)
}
