//! Compila os programas do LumeBSD para o FreeBSD com o clang de fora (o do
//! Linux ou o do FreeBSD), contra o stage (a libc e a libc++ do FreeBSD) e
//! os cabeçalhos e bibliotecas dos pacotes, e os põe no stage com as linhas
//! do METALOG.build:
//!
//!   app      os apps em Qt (app/NOME/main.cpp, sem moc): Atualizações,
//!            Programas, o ícone de rede, os atributos dos arquivos e as
//!            Finanças
//!   motor    o lume-motor: a biblioteca liblume-motor e a demonstração
//!   efeitos  os efeitos do KWin (lume_tinta, lume_abas), a decoração Papel
//!            e as animações em JavaScript
//!   mac      o lume-macho e o programa de teste do macOS (Mach-O)
//!
//! Cada um tem o modo --so-compilar, do CI: compila contra um base.txz do
//! FreeBSD, sem stage, e deixa o resultado numa pasta.

use std::fs;
use std::path::{Path, PathBuf};

use serde_json::Value;

use crate::sistema::{atualizar_build, embutir_papel, RAIZ};
use crate::util::{
    absoluto, achar_pacote, copiar, criar_pastas, gravar, gzip, link, llvm, rodar, rodar_quieto,
    s, sair, tamanho, topo,
};
use crate::v;

const ALVO: &str = "--target=x86_64-unknown-freebsd15.1";
const RPATH: &str = "-Wl,-rpath,/usr/local/lib/qt6:/usr/local/lib";

fn tmp() -> tempfile::TempDir {
    tempfile::tempdir().unwrap_or_else(|e| sair(format!("tmp: {e}")))
}

fn strip(arq: &Path, extra: &[&str]) {
    let mut c = v![llvm("llvm-strip")];
    c.extend(extra.iter().map(|x| x.to_string()));
    c.push(s(arq));
    rodar(&c, None);
}

/// A raiz de compilação: o FreeBSD do stage (lib, usr/lib, usr/include) e
/// os pacotes (só include e lib) em /usr/local. Devolve (local, raiz).
fn preparar_raiz(tmp: &Path, base: &Path, pacotes: &Path, nomes: &[&str], quem: &str) -> (PathBuf, PathBuf) {
    let local = tmp.join("local");
    criar_pastas(&local);
    for nome in nomes {
        // Nem todo pacote tem as duas pastas (xorgproto só tem include/): o
        // bsdtar reclama da que falta, sem problema.
        rodar_quieto(&v![
            "bsdtar", "-xf", s(&achar_pacote(pacotes, nome, quem)), "-C", s(&local),
            "--strip-components", "3", "/usr/local/include", "/usr/local/lib"
        ]);
    }
    let raiz = sysroot(tmp, base);
    link(&local, raiz.join("usr/local"));
    (local, raiz)
}

fn sysroot(tmp: &Path, base: &Path) -> PathBuf {
    let raiz = tmp.join("raiz");
    criar_pastas(raiz.join("usr"));
    for d in ["lib", "usr/lib", "usr/include"] {
        link(base.join(d), raiz.join(d));
    }
    raiz
}

fn modo_ci(args: &[String], n: usize) -> Option<Vec<PathBuf>> {
    if args.len() == n + 1 && args[0] == "--so-compilar" {
        Some(args[1..].iter().map(absoluto).collect())
    } else {
        None
    }
}

// --- app -------------------------------------------------------------------

const APP: &str = "usr/share/lume/app";
const APPS: &[(&str, &[&str])] = &[
    ("lume-atualizacoes", &["lume-atualizacoes.svg", "lume-atualizacoes.desktop",
                            "lume-atualizacoes-bandeja.desktop", "org.lumebsd.atualizar.policy"]),
    ("lume-programas", &["lume-programas.svg", "lume-programas.desktop", "org.lumebsd.programas.policy"]),
    ("lume-rede", &["lume-rede.desktop", "org.lumebsd.rede.policy"]),
    ("lume-atributos", &["lume-atributos.desktop", "lume-atributos-menu.desktop",
                         "lume-atributos-pasta.desktop", "lume-consulta.xml"]),
    ("lume-financas", &["lume-financas.svg", "lume-financas.desktop"]),
    ("lume-paginas", &["lume-paginas.svg", "lume-paginas.desktop"]),
];

pub const USO_APP: &str = "uso: lume-montar app STAGE QT6BASE.pkg
     lume-montar app --so-compilar RAIZ QT6BASE.pkg PASTA

Cada app (app/NOME/main.cpp) é Qt 6 sem moc. Vai para /usr/bin/NOME (e o
manual NOME.1), com o ícone, os atalhos do menu e a política do polkit em
/usr/share/lume/app (o \"lume tema integrar\" instala esses no KDE).";

fn libs_app(nome: &str) -> &'static [&'static str] {
    match nome {
        "lume-rede" => &["-lcrypto"],
        // Os .odt e .docx são zips: o zlib da base.
        "lume-paginas" => &["-lz"],
        _ => &[],
    }
}

fn compilar_app(base: &Path, qtpkg: &Path, saida: &Path, nome: &str) {
    let t = tmp();
    let qt = t.path().join("qt");
    criar_pastas(&qt);
    // (No pacote, os caminhos são absolutos: /usr/local/...)
    rodar(&v!["bsdtar", "-xf", s(qtpkg), "-C", s(&qt), "/usr/local/include", "/usr/local/lib"], None);
    let raiz = sysroot(t.path(), base);
    link(qt.join("usr/local"), raiz.join("usr/local"));
    let inc = raiz.join("usr/local/include/qt6");
    let mut cmd = v![llvm("clang++"), ALVO, format!("--sysroot={}", s(&raiz)),
                     "-std=c++17", "-O2", "-fPIC", "-Wall", "-isystem", s(&inc),
                     // Sem a pasta temporária dentro do binário (os Q_ASSERT).
                     format!("-ffile-prefix-map={}=/", s(&raiz))];
    for m in ["QtCore", "QtGui", "QtWidgets"] {
        cmd.extend(v!["-isystem", s(&inc.join(m))]);
    }
    cmd.extend(v![s(&topo().join("app").join(nome).join("main.cpp")), "-o", s(saida),
                  "-fuse-ld=lld", format!("-L{}/usr/local/lib/qt6", s(&raiz)),
                  "-lQt6Widgets", "-lQt6Gui", "-lQt6Core"]);
    cmd.extend(libs_app(nome).iter().map(|x| x.to_string()));
    cmd.extend(v![RPATH, "-Wl,--allow-shlib-undefined"]);
    rodar(&cmd, None);
    strip(saida, &[]);
}

pub fn app(args: &[String]) {
    if let Some(a) = modo_ci(args, 3) {
        criar_pastas(&a[2]);
        for (nome, _) in APPS {
            let saida = a[2].join(nome);
            compilar_app(&a[0], &a[1], &saida, nome);
            println!("app: {} ({} KiB)", saida.display(), tamanho(&saida) / 1024);
        }
        return;
    }
    if args.len() != 2 {
        sair(USO_APP);
    }
    let (stage, qtpkg) = (absoluto(&args[0]), absoluto(&args[1]));
    criar_pastas(stage.join(APP));
    let mut novas = vec![format!("./{APP} type=dir {RAIZ} mode=0755 tags=package=lume")];
    for (nome, arquivos) in APPS {
        let fonte = topo().join("app").join(nome);
        let binario = stage.join("usr/bin").join(nome);
        compilar_app(&stage, &qtpkg, &binario, nome);
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&binario, fs::Permissions::from_mode(0o555)).ok();
        }
        for a in *arquivos {
            copiar(fonte.join(a), stage.join(APP).join(a));
        }
        gzip(&fonte.join(format!("{nome}.1")), &stage.join(format!("usr/share/man/man1/{nome}.1.gz")));
        novas.push(format!("./usr/bin/{nome} type=file {RAIZ} mode=0555 tags=package=lume"));
        novas.push(format!("./usr/share/man/man1/{nome}.1.gz type=file {RAIZ} mode=0444 tags=package=lume"));
        for a in *arquivos {
            novas.push(format!("./{APP}/{a} type=file {RAIZ} mode=0444 tags=package=lume"));
        }
        println!("app: {} ({} KiB)", binario.display(), tamanho(&binario) / 1024);
    }
    atualizar_build(&stage, &novas, None);
}

// --- motor -----------------------------------------------------------------

const PACOTES_MOTOR: &[&str] = &["libglvnd", "mesa-libs", "libdrm", "freetype2", "png"];
const BIBLIOTECA: &[&str] = &["motor.c", "cena.c", "saida.c"];
const CABECALHOS: &[&str] = &["motor.h", "saida.h", "papel.h"];
const SONAME: &str = "liblume-motor.so.1";

pub const USO_MOTOR: &str = "uso: lume-montar motor STAGE PACOTES
     lume-montar motor --so-compilar RAIZ PACOTES PASTA

PACOTES é uma pasta com os .pkg do FreeBSD de onde vêm os cabeçalhos e as
bibliotecas. O motor usa o Mesa (EGL, GLES 3.1, GBM), o libdrm, o FreeType
e o libpng, que chegam com o desktop. É uma biblioteca,
/usr/lib/liblume-motor.so.1, com os cabeçalhos em /usr/include/lume
(motor.h, saida.h, papel.h); a demonstração, /usr/bin/lume-motor, é ligada
a ela.";

fn compilar_motor(base: &Path, pacotes: &Path, pasta: &Path) {
    let t = tmp();
    let (local, raiz) = preparar_raiz(t.path(), base, pacotes, PACOTES_MOTOR, "montar-motor");
    let inc = local.join("include");
    // As contas do papel (papel.glsl), comuns com o filtro do KWin.
    let gerado = t.path().join("gerado");
    embutir_papel(&gerado);
    let fonte = topo().join("app/lume-motor");
    let base_cmd = v![llvm("clang"), ALVO, format!("--sysroot={}", s(&raiz)), "-std=c11", "-O2",
                      "-Wall", "-Wextra", "-Wno-unused-parameter", "-D_XOPEN_SOURCE=700",
                      "-D__BSD_VISIBLE=1", "-I", s(&gerado), "-isystem", s(&inc),
                      "-isystem", s(&inc.join("libdrm")), "-isystem", s(&inc.join("freetype2")),
                      "-fuse-ld=lld"];
    criar_pastas(pasta);
    let lib = pasta.join(SONAME);
    let mut cmd = base_cmd.clone();
    cmd.extend(v!["-fPIC", "-shared", format!("-Wl,-soname,{SONAME}")]);
    cmd.extend(BIBLIOTECA.iter().map(|a| s(&fonte.join(a))));
    cmd.extend(v!["-o", s(&lib), format!("-L{}/lib", s(&local)), "-lEGL", "-lGLESv2", "-lgbm",
                  "-ldrm", "-lfreetype", "-lpng16", "-lm", "-Wl,-rpath,/usr/local/lib",
                  "-Wl,--allow-shlib-undefined"]);
    rodar(&cmd, None);
    link(SONAME, pasta.join("liblume-motor.so"));
    let prog = pasta.join("lume-motor");
    let mut cmd = base_cmd;
    cmd.extend(v![s(&fonte.join("demo.c")), "-o", s(&prog), format!("-L{}", s(pasta)),
                  "-llume-motor", format!("-L{}/lib", s(&local)), "-lGLESv2", "-lm",
                  "-Wl,--allow-shlib-undefined"]);
    rodar(&cmd, None);
    for f in [&lib, &prog] {
        strip(f, &["--strip-unneeded"]);
    }
    println!("motor: {SONAME} ({} KiB), lume-motor ({} KiB)", tamanho(&lib) / 1024, tamanho(&prog) / 1024);
}

pub fn motor(args: &[String]) {
    if let Some(a) = modo_ci(args, 3) {
        compilar_motor(&a[0], &a[1], &a[2]);
        return;
    }
    if args.len() != 2 {
        sair(USO_MOTOR);
    }
    let (stage, pacotes) = (absoluto(&args[0]), absoluto(&args[1]));
    let t = tmp();
    compilar_motor(&stage, &pacotes, t.path());
    copiar(t.path().join("lume-motor"), stage.join("usr/bin/lume-motor"));
    copiar(t.path().join(SONAME), stage.join("usr/lib").join(SONAME));
    link(SONAME, stage.join("usr/lib/liblume-motor.so"));
    let inc = stage.join("usr/include/lume");
    criar_pastas(&inc);
    let fonte = topo().join("app/lume-motor");
    for h in CABECALHOS {
        copiar(fonte.join(h), inc.join(h));
    }
    gzip(&fonte.join("lume-motor.1"), &stage.join("usr/share/man/man1/lume-motor.1.gz"));
    let mut novas = vec![
        format!("./usr/bin/lume-motor type=file {RAIZ} mode=0555 tags=package=lume"),
        format!("./usr/lib/{SONAME} type=file {RAIZ} mode=0444 tags=package=lume"),
        format!("./usr/lib/liblume-motor.so type=link {RAIZ} mode=0755 link={SONAME} tags=package=lume"),
        format!("./usr/include/lume type=dir {RAIZ} mode=0755 tags=package=lume"),
    ];
    for h in CABECALHOS {
        novas.push(format!("./usr/include/lume/{h} type=file {RAIZ} mode=0444 tags=package=lume"));
    }
    novas.push(format!("./usr/share/man/man1/lume-motor.1.gz type=file {RAIZ} mode=0444 tags=package=lume"));
    atualizar_build(&stage, &novas, None);
}

// --- mac -------------------------------------------------------------------

pub const USO_MAC: &str = "uso: lume-montar mac STAGE
     lume-montar mac --so-compilar RAIZ SAIDA

Vai para /usr/libexec/lume-macho e /usr/share/lume/mac/ola-mac; o
lume-compat usa os dois (lume compat mac testar; dois cliques num
executável do Mac).";

fn compilar_mac(base: &Path, saida: &Path) {
    let t = tmp();
    let raiz = sysroot(t.path(), base);
    let mac = topo().join("mac");
    let macho = saida.join("lume-macho");
    rodar(&v![llvm("clang"), ALVO, format!("--sysroot={}", s(&raiz)), "-std=c11", "-O2",
              "-Wall", "-Wextra", "-Werror", "-D__BSD_VISIBLE=1", "-fuse-ld=lld",
              s(&mac.join("lume-macho/lume-macho.c")), "-o", s(&macho)], None);
    strip(&macho, &[]);
    // O programa de teste: um Mach-O de verdade, sem a libSystem.
    let obj = t.path().join("ola-mac.o");
    rodar(&v![llvm("clang"), "--target=x86_64-apple-macos11", "-O2", "-fno-stack-protector",
              "-fno-builtin", "-ffreestanding", "-c", s(&mac.join("teste/ola-mac.c")),
              "-o", s(&obj)], None);
    let ola = saida.join("ola-mac");
    rodar(&v![llvm("ld64.lld"), "-arch", "x86_64", "-platform_version", "macos", "11.0",
              "11.0", "-e", "_start", "-o", s(&ola), s(&obj)], None);
    println!("mac: lume-macho ({} KiB), ola-mac ({} bytes)", tamanho(&macho) / 1024, tamanho(&ola));
}

pub fn mac(args: &[String]) {
    if let Some(a) = modo_ci(args, 2) {
        criar_pastas(&a[1]);
        compilar_mac(&a[0], &a[1]);
        return;
    }
    if args.len() != 1 {
        sair(USO_MAC);
    }
    let stage = absoluto(&args[0]);
    let t = tmp();
    compilar_mac(&stage, t.path());
    criar_pastas(stage.join("usr/libexec"));
    criar_pastas(stage.join("usr/share/lume/mac"));
    copiar(t.path().join("lume-macho"), stage.join("usr/libexec/lume-macho"));
    copiar(t.path().join("ola-mac"), stage.join("usr/share/lume/mac/ola-mac"));
    gzip(&topo().join("mac/lume-macho/lume-macho.1"), &stage.join("usr/share/man/man1/lume-macho.1.gz"));
    let novas = vec![
        format!("./usr/libexec/lume-macho type=file {RAIZ} mode=0555 tags=package=lume"),
        format!("./usr/share/lume/mac type=dir {RAIZ} mode=0755 tags=package=lume"),
        format!("./usr/share/lume/mac/ola-mac type=file {RAIZ} mode=0555 tags=package=lume"),
        format!("./usr/share/man/man1/lume-macho.1.gz type=file {RAIZ} mode=0444 tags=package=lume"),
    ];
    atualizar_build(&stage, &novas, None);
}

// --- efeitos ---------------------------------------------------------------

const DESTINO_KWIN: &str = "usr/share/lume/kwin";
const PACOTES_KWIN: &[&str] = &[
    "qt6-base", "kf6-kcoreaddons", "kf6-kconfig", "kf6-kglobalaccel", "plasma6-kwin",
    "plasma6-kwin-x11", "libepoxy", "libxcb", "xorgproto", "libdrm", "wayland",
    "libxkbcommon", "libglvnd", "plasma6-kdecoration", "kf6-kwindowsystem",
];
/// Os efeitos em C++: (pasta em app/, arquivo, nome do .so, classe da
/// fábrica, json).
const EFEITOS: &[(&str, &str, &str, &str, &str)] = &[
    ("lume-tinta", "tinta.cpp", "lume_tinta", "TintaFabrica", "metadata.json"),
    ("lume-janelas", "abas.cpp", "lume_abas", "AbasFabrica", "abas.json"),
];
/// Os dois KWin: (variante, pasta dos cabeçalhos, biblioteca).
const VARIANTES: &[(&str, &str, &str)] = &[("x11", "kwin-x11", "kwin-x11"), ("wayland", "kwin", "kwin")];

pub const USO_EFEITOS: &str = "uso: lume-montar efeitos STAGE PACOTES
     lume-montar efeitos --so-compilar RAIZ PACOTES PASTA

- lume_tinta (app/lume-tinta): o filtro e-ink, em C++, contra o KWin do
  X11 (kwin-x11) e o do Wayland (kwin). Sem moc: os metadados do plugin (o
  que o Q_PLUGIN_METADATA geraria) saem daqui, do metadata.json em CBOR, no
  formato do moc do Qt 6.
- lume_abas (app/lume-janelas/abas.cpp): as abas de janelas (como no BeOS),
  também nas duas variantes.
- org.lumebsd.papel (app/lume-janelas/decoracao.cpp): a decoração de
  janelas \"Papel\"; só usa a KDecoration3, então compila uma vez.
- lume_refresh e lume_esmaecer (app/lume-animacoes): as animações de abrir
  e fechar janelas, em JavaScript.

Tudo vai para /usr/share/lume/kwin; o \"lume tema integrar\" instala no KWin
o que bate com a versão do KWin instalada.";

/// O array qt_pluginMetaDataV2_<classe>, como o moc do Qt 6 escreve: um
/// mapa CBOR indefinido com IID (2), ClassName (3) e MetaData (4).
pub fn metadados(iid: &str, classe: &str, meta: &Value) -> String {
    let mut corpo = vec![0xbf];
    corpo.extend(crate::cbor::cbor(&Value::from(2)));
    corpo.extend(crate::cbor::cbor(&Value::from(iid)));
    corpo.extend(crate::cbor::cbor(&Value::from(3)));
    corpo.extend(crate::cbor::cbor(&Value::from(classe)));
    corpo.extend(crate::cbor::cbor(&Value::from(4)));
    corpo.extend(crate::cbor::cbor(meta));
    corpo.push(0xff);
    let linhas: Vec<String> = corpo
        .chunks(12)
        .map(|c| format!("    {},", c.iter().map(|b| format!("0x{b:02x}")).collect::<Vec<_>>().join(", ")))
        .collect();
    format!("static constexpr unsigned char qt_pluginMetaDataV2_{classe}[] = {{\n{}\n}};\n", linhas.join("\n"))
}

fn versao_kwin(local: &Path, pasta: &str) -> String {
    let conf = local.join("include").join(pasta).join("config-kwin.h");
    for l in crate::util::ler(&conf).lines() {
        if l.contains("KWIN_PLUGIN_VERSION_STRING") {
            if let Some(v) = l.split('"').nth(1) {
                return v.to_string();
            }
        }
    }
    sair(format!("montar-efeitos: sem a versão em {}", conf.display()))
}

fn base_cmd_kwin(raiz: &Path, local: &Path) -> Vec<String> {
    let inc = local.join("include");
    let qt = inc.join("qt6");
    let mut cmd = v![llvm("clang++"), ALVO, format!("--sysroot={}", s(raiz)), "-std=c++20", "-O2",
                     "-fPIC", "-shared", "-Wall", "-Wno-deprecated-declarations",
                     "-DQT_NO_KEYWORDS", "-DQT_NO_DEBUG"];
    let mut dirs = vec![qt.clone()];
    dirs.extend(["QtCore", "QtGui", "QtDBus", "QtWidgets"].iter().map(|m| qt.join(m)));
    dirs.extend(["KConfigCore", "KConfig", "KCoreAddons", "KGlobalAccel", "KWindowSystem"]
        .iter().map(|m| inc.join("KF6").join(m)));
    dirs.push(inc.join("KDecoration3"));
    dirs.push(inc.clone());
    for d in dirs {
        cmd.extend(v!["-isystem", s(&d)]);
    }
    cmd
}

fn ler_json(p: &Path) -> Value {
    serde_json::from_str(&crate::util::ler(p)).unwrap_or_else(|e| sair(format!("{}: {e}", p.display())))
}

fn compilar_efeitos(base: &Path, pacotes: &Path, saida_dir: &Path) {
    let t = tmp();
    let (local, raiz) = preparar_raiz(t.path(), base, pacotes, PACOTES_KWIN, "montar-efeitos");
    let inc = local.join("include");
    for (pasta_app, arquivo, so_nome, classe, json_nome) in EFEITOS {
        let fonte = topo().join("app").join(pasta_app);
        let meta = ler_json(&fonte.join(json_nome));
        for (variante, pasta, lib) in VARIANTES {
            let versao = versao_kwin(&local, pasta);
            let gerado = t.path().join(format!("gerado-{so_nome}-{variante}"));
            criar_pastas(&gerado);
            gravar(gerado.join("metadados.inc"),
                   metadados(&format!("org.kde.kwin.EffectPluginFactory{versao}"), classe, &meta));
            // O papel do lume-motor (as contas e as cores), para o lume-tinta.
            embutir_papel(&gerado);
            let so = saida_dir.join(variante).join(format!("{so_nome}.so"));
            criar_pastas(so.parent().unwrap());
            let mut cmd = base_cmd_kwin(&raiz, &local);
            cmd.extend(v![format!("-DLUME_KWIN_{}", variante.to_uppercase()), "-I", s(&gerado),
                          "-I", s(&topo().join("app/lume-motor")), "-isystem", s(&inc.join(pasta))]);
            cmd.extend(v![s(&fonte.join(arquivo)), "-o", s(&so), "-fuse-ld=lld",
                          format!("-L{}/lib", s(&local)), format!("-L{}/lib/qt6", s(&local)),
                          format!("-l{lib}"), "-lkdecorations3", "-lKF6GlobalAccel",
                          "-lKF6ConfigCore", "-lQt6Gui", "-lQt6DBus", "-lQt6Core", RPATH,
                          "-Wl,--allow-shlib-undefined", "-Wl,--no-undefined-version"]);
            rodar(&cmd, None);
            strip(&so, &[]);
            // A versão do KWin contra a qual compilou: o KWin só carrega um
            // plugin da mesma versão (está no IID).
            gravar(saida_dir.join(variante).join("versao"), format!("{versao}\n"));
            println!("efeito: {so_nome} ({variante}, KWin {versao}, {} KiB)", tamanho(&so) / 1024);
        }
    }
    // A decoração: um KPluginFactory comum (IID do KPluginFactory).
    let fonte = topo().join("app/lume-janelas");
    let meta = ler_json(&fonte.join("decoracao.json"));
    let gerado = t.path().join("gerado-decoracao");
    criar_pastas(&gerado);
    gravar(gerado.join("metadados.inc"), metadados("org.kde.KPluginFactory", "PapelFabrica", &meta));
    let so = saida_dir.join("decoracao/org.lumebsd.papel.so");
    criar_pastas(so.parent().unwrap());
    let mut cmd = base_cmd_kwin(&raiz, &local);
    cmd.extend(v!["-I", s(&gerado)]);
    cmd.extend(v![s(&fonte.join("decoracao.cpp")), "-o", s(&so), "-fuse-ld=lld",
                  format!("-L{}/lib", s(&local)), format!("-L{}/lib/qt6", s(&local)),
                  "-lkdecorations3", "-lKF6CoreAddons", "-lQt6Gui", "-lQt6Core", RPATH,
                  "-Wl,--allow-shlib-undefined"]);
    rodar(&cmd, None);
    strip(&so, &[]);
    println!("decoração: org.lumebsd.papel ({} KiB)", tamanho(&so) / 1024);
}

fn copiar_arvore(de: &Path, para: &Path) {
    criar_pastas(para);
    for e in fs::read_dir(de).unwrap_or_else(|e| sair(format!("{}: {e}", de.display()))).flatten() {
        let p = e.path();
        if p.is_dir() {
            copiar_arvore(&p, &para.join(e.file_name()));
        } else {
            copiar(&p, para.join(e.file_name()));
        }
    }
}

/// As linhas do METALOG de uma árvore: as pastas e os arquivos (os .so
/// executáveis), em ordem.
fn linhas_arvore(stage: &Path, pasta: &Path, novas: &mut Vec<String>) {
    let rel = format!("./{}", pasta.strip_prefix(stage).unwrap().display());
    novas.push(format!("{rel} type=dir {RAIZ} mode=0755 tags=package=lume"));
    let mut itens: Vec<_> = fs::read_dir(pasta).map(|it| it.flatten().collect()).unwrap_or_else(|_| vec![]);
    itens.sort_by_key(|e| e.file_name());
    let (dirs, arqs): (Vec<_>, Vec<_>) = itens.into_iter().partition(|e| e.path().is_dir());
    for a in arqs {
        let n = a.file_name().to_string_lossy().into_owned();
        let modo = if n.ends_with(".so") { "0555" } else { "0444" };
        novas.push(format!("{rel}/{n} type=file {RAIZ} mode={modo} tags=package=lume"));
    }
    for d in dirs {
        linhas_arvore(stage, &d.path(), novas);
    }
}

pub fn efeitos(args: &[String]) {
    if let Some(a) = modo_ci(args, 3) {
        compilar_efeitos(&a[0], &a[1], &a[2]);
        return;
    }
    if args.len() != 2 {
        sair(USO_EFEITOS);
    }
    let (stage, pacotes) = (absoluto(&args[0]), absoluto(&args[1]));
    let destino = stage.join(DESTINO_KWIN);
    if destino.is_dir() {
        fs::remove_dir_all(&destino).ok();
    }
    criar_pastas(&destino);
    compilar_efeitos(&stage, &pacotes, &destino);
    // As animações (JS): copiadas como estão.
    let anim = topo().join("app/lume-animacoes");
    let mut nomes: Vec<_> = fs::read_dir(&anim).map(|it| it.flatten().collect()).unwrap_or_else(|_| vec![]);
    nomes.sort_by_key(|e| e.file_name());
    for d in nomes.iter().filter(|e| e.path().is_dir()) {
        copiar_arvore(&d.path(), &destino.join("efeitos").join(d.file_name()));
    }
    // METALOG.build: diretórios e arquivos, sem repetir se rodar de novo.
    let mut novas = vec![];
    linhas_arvore(&stage, &destino, &mut novas);
    atualizar_build(&stage, &novas, Some(&format!("./{DESTINO_KWIN}/")));
}
