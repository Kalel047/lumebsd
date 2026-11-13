//! Gera o tema "Papel noite" (escuro) a partir do Papel.
//!
//! O Papel é tinta sobre papel; o Papel noite é o mesmo papel visto à
//! noite: o fundo é a tinta (um preto quente, não o preto puro, que cansa
//! num LCD) e o texto é o papel, um pouco menos claro, para não ofuscar no
//! escuro. Cada cor do Papel tem a sua par aqui (MAPA): os dois temas ficam
//! sempre coerentes, e uma cor nova no Papel que não estiver no mapa para a
//! geração com erro.
//!
//! Gera: PapelNoite.colors (KDE), PapelNoite.colorscheme e .profile
//! (Konsole), kdeglobals-noite, konsolerc-noite, gtk-settings-noite.ini e o
//! tema global (metadata-noite.json, defaults-noite, layout-noite.js).

use std::path::Path;

use regex::Regex;

use crate::util::{gravar, ler, sair};

pub const USO: &str = "uso: lume-montar tema-noite TEMA
     TEMA: a pasta do tema na árvore (usr.sbin/lume-configurar/tema)";

type Rgb = (u8, u8, u8);

/// Papel -> Papel noite. Fundos viram tinta, tinta vira papel, cinzas
/// trocam de lado; o vermelho de revisor fica mais claro para ler sobre o
/// escuro.
const MAPA: &[(Rgb, Rgb)] = &[
    ((244, 242, 236), (30, 29, 27)),     // papel: o fundo
    ((235, 233, 226), (39, 38, 35)),     // papel alternado, botões, cabeçalhos
    ((26, 26, 26), (222, 218, 208)),     // tinta: o texto
    ((105, 104, 100), (150, 147, 139)),  // texto inativo
    ((90, 90, 90), (176, 172, 163)),     // contorno ao passar o mouse
    ((96, 92, 84), (178, 171, 156)),     // neutro
    ((150, 40, 32), (226, 124, 108)),    // vermelho de revisor (erros)
    ((60, 60, 58), (204, 201, 193)),     // positivo
    ((90, 89, 86), (170, 166, 158)),     // link visitado
    ((196, 193, 185), (78, 76, 72)),     // desabilitado
    // Seleção: tinta sobre papel no Papel; papel sobre tinta aqui.
    ((50, 50, 50), (200, 196, 186)),
    ((70, 70, 70), (184, 180, 170)),
    ((200, 198, 192), (72, 70, 66)),
    // Complementar (OSD, painéis escuros no Papel): fica um tom abaixo do fundo.
    ((40, 40, 40), (26, 25, 23)),
    ((170, 168, 162), (150, 147, 139)),
    ((220, 120, 110), (226, 124, 108)),
];

/// Na seção [Colors:Complementary], antes do MAPA.
const COMPLEMENTAR: &[(Rgb, Rgb)] = &[
    ((26, 26, 26), (20, 19, 18)),
    ((244, 242, 236), (222, 218, 208)),
    ((200, 200, 200), (176, 172, 163)),
];

fn achar(lista: &[(Rgb, Rgb)], c: Rgb) -> Option<Rgb> {
    lista.iter().find(|(de, _)| *de == c).map(|(_, para)| *para)
}

/// Troca as cores de um arquivo de cores do KDE (Papel.colors, kdeglobals).
fn trocar_kde(texto: &str) -> String {
    let cor = Regex::new(r"^(\w+)=(\d+),(\d+),(\d+)$").unwrap();
    let mut saida = vec![];
    let mut secao = String::new();
    for linha in texto.lines() {
        if linha.starts_with('[') {
            secao = linha.to_string();
        }
        if let Some(m) = cor.captures(linha) {
            let chave = &m[1];
            let num = |i: usize| m[i].parse::<u8>().unwrap_or_else(|_| sair(format!("tema-noite: cor inválida: {linha}")));
            let rgb = (num(2), num(3), num(4));
            let novo = if secao == "[Colors:Complementary]" {
                achar(COMPLEMENTAR, rgb).or_else(|| achar(MAPA, rgb))
            } else {
                achar(MAPA, rgb)
            };
            let Some(n) = novo else {
                sair(format!("tema-noite: a cor ({}, {}, {}) ({secao} {chave}) não está no mapa", rgb.0, rgb.1, rgb.2))
            };
            saida.push(format!("{chave}={},{},{}", n.0, n.1, n.2));
        } else {
            saida.push(linha.to_string());
        }
    }
    saida.join("\n") + "\n"
}

fn nomes(texto: &str) -> String {
    texto
        .replace("ColorScheme=Papel", "ColorScheme=PapelNoite")
        .replace("Name=Papel\n", "Name=Papel noite\n")
        .replace("LookAndFeelPackage=org.lumebsd.papel", "LookAndFeelPackage=org.lumebsd.papel-noite")
        .replace("Theme=lume-papel", "Theme=lume-papel-noite")
}

/// O terminal à noite: fundo de tinta, letras de papel; as cores ANSI são as
/// mesmas tintas desbotadas do Papel, agora claras sobre o escuro.
const KONSOLE: &str = r#"# Esquema de cores do Konsole "Papel noite" (LumeBSD): o Papel à noite.
# Gerado por ferramentas/montar tema-noite a partir do Papel.
[Background]
Color=30,29,27

[BackgroundIntense]
Color=39,38,35

[BackgroundFaint]
Color=30,29,27

[Foreground]
Color=222,218,208

[ForegroundIntense]
Color=240,236,226

[ForegroundFaint]
Color=150,147,139

[Color0]
Color=52,50,46

[Color0Intense]
Color=92,89,83

[Color0Faint]
Color=44,43,40

[Color1]
Color=226,124,108

[Color1Intense]
Color=240,150,134

[Color1Faint]
Color=150,96,88

[Color2]
Color=204,201,193

[Color2Intense]
Color=226,223,215

[Color2Faint]
Color=130,128,122

[Color3]
Color=178,171,156

[Color3Intense]
Color=204,196,180

[Color3Faint]
Color=118,114,104

[Color4]
Color=190,190,188

[Color4Intense]
Color=220,220,218

[Color4Faint]
Color=124,124,122

[Color5]
Color=184,178,184

[Color5Intense]
Color=210,204,210

[Color5Faint]
Color=122,118,122

[Color6]
Color=176,186,186

[Color6Intense]
Color=204,214,214

[Color6Faint]
Color=116,122,122

[Color7]
Color=200,196,186

[Color7Intense]
Color=240,236,226

[Color7Faint]
Color=150,147,139

[General]
Description=Papel noite
Opacity=1
Wallpaper=
"#;

pub fn main(args: &[String]) {
    if args.len() != 1 {
        sair(USO);
    }
    let t = Path::new(&args[0]);
    let l = |n: &str| ler(t.join(n));
    let g = |n: &str, texto: String| gravar(t.join(n), texto);

    let cab = "# Gerado por ferramentas/montar tema-noite a partir do Papel; não edite à mão.\n";
    let colors = trocar_kde(&l("Papel.colors")).replace(
        "# Esquema de cores \"Papel\" do LumeBSD: estética e-ink.",
        "# Esquema de cores \"Papel noite\" do LumeBSD: o Papel à noite.",
    );
    g("PapelNoite.colors", format!("{cab}{}", nomes(&colors)));

    let kg = trocar_kde(&l("kdeglobals")).replace("(tema Papel)", "(tema Papel noite)");
    let kg = Regex::new(r"(?m)^AccentColor=.*$").unwrap().replace_all(&kg, "AccentColor=120,116,108").into_owned();
    g("kdeglobals-noite", format!("{cab}{}", nomes(&kg)));

    g("PapelNoite.colorscheme", KONSOLE.to_string());
    g("PapelNoite.profile", format!("{cab}{}", l("Papel.profile")
        .replace("ColorScheme=Papel", "ColorScheme=PapelNoite")
        .replace("Name=Papel", "Name=Papel noite")));
    g("konsolerc-noite", format!("{cab}{}", l("konsolerc")
        .replace("DefaultProfile=Papel.profile", "DefaultProfile=PapelNoite.profile")));
    g("gtk-settings-noite.ini", l("gtk-settings.ini")
        .replace("gtk-theme-name=Breeze\n", "gtk-theme-name=Breeze-Dark\n")
        .replace("gtk-icon-theme-name=lume-papel", "gtk-icon-theme-name=lume-papel-noite")
        .replace("gtk-application-prefer-dark-theme=false", "gtk-application-prefer-dark-theme=true"));

    let meta = l("metadata.json")
        .replace("\"Papel: tinta sobre papel, sem animações nem sombras (estética e-ink).\"",
                 "\"Papel noite: o Papel à noite, papel claro sobre tinta, sem animações nem sombras.\"")
        .replace("\"org.lumebsd.papel\"", "\"org.lumebsd.papel-noite\"")
        .replace("\"Papel (LumeBSD)\"", "\"Papel noite (LumeBSD)\"");
    g("metadata-noite.json", meta);
    g("layout-noite.js", l("layout.js")
        .replace("(tema Papel)", "(tema Papel noite)")
        .replace("wallpapers/LumePapel/", "wallpapers/LumePapelNoite/"));
    g("defaults-noite", nomes(&l("defaults"))
        .replace("Image=LumePapel\n", "Image=LumePapelNoite\n")
        .replace("[KSplash]\nTheme=org.lumebsd.papel\n", "[KSplash]\nTheme=org.lumebsd.papel-noite\n"));
    println!("tema-noite: PapelNoite.colors, kdeglobals-noite, Konsole, GTK e o tema global gerados");
}
