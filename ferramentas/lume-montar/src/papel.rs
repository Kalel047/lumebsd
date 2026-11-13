//! Desenha o papel de parede "Papel" do LumeBSD (estética e-ink).
//!
//! Papel quase branco, tinta quase preta, só traço: o farol à direita, a
//! linha do horizonte e o nome da distribuição em serifa, como a folha de
//! rosto de um livro. Com "noite", as cores se invertem (tela de login e de
//! bloqueio, como a tela de descanso de um leitor de e-book).

use std::f32::consts::PI;
use std::path::Path;

use tiny_skia::{FillRule, LineCap, LineJoin, Paint, PathBuilder, Pixmap, Stroke, Transform};

use crate::util::{ler, sair, topo};

pub const USO: &str = "uso: lume-montar papel-de-parede PASTA_DAS_FONTES SAIDA.png [noite]";

const L: u32 = 3840;
const A: u32 = 2160;

type Rgb = (u8, u8, u8);

struct Folha {
    p: Pixmap,
}

fn tinta(c: Rgb) -> Paint<'static> {
    let mut paint = Paint::default();
    paint.set_color_rgba8(c.0, c.1, c.2, 255);
    paint.anti_alias = true;
    paint
}

impl Folha {
    /// Um traço por vários pontos, com as juntas arredondadas (o
    /// joint="curve" do PIL) e as pontas retas.
    fn linha(&mut self, pontos: &[(f32, f32)], cor: Rgb, w: f32) {
        let mut pb = PathBuilder::new();
        pb.move_to(pontos[0].0, pontos[0].1);
        for q in &pontos[1..] {
            pb.line_to(q.0, q.1);
        }
        let Some(caminho) = pb.finish() else { return };
        let traco = Stroke { width: w, line_cap: LineCap::Butt, line_join: LineJoin::Round, ..Default::default() };
        self.p.stroke_path(&caminho, &tinta(cor), &traco, Transform::identity(), None);
    }

    fn poligono(&mut self, pontos: &[(f32, f32)], cor: Rgb) {
        let mut pb = PathBuilder::new();
        pb.move_to(pontos[0].0, pontos[0].1);
        for q in &pontos[1..] {
            pb.line_to(q.0, q.1);
        }
        pb.close();
        if let Some(c) = pb.finish() {
            self.p.fill_path(&c, &tinta(cor), FillRule::Winding, Transform::identity(), None);
        }
    }

    /// O texto com o canto de cima à esquerda em (x, y), como o d.text do
    /// PIL (y na linha do ascendente da fonte).
    fn texto(&mut self, x: f32, y: f32, texto: &str, fonte: &[u8], tam: f32, cor: Rgb) {
        let face = ttf_parser::Face::parse(fonte, 0).unwrap_or_else(|e| sair(format!("fonte: {e}")));
        let escala = tam / face.units_per_em() as f32;
        let base = y + face.ascender() as f32 * escala;
        let mut pb = PathBuilder::new();
        let mut cx = x;
        struct Contorno<'a> {
            pb: &'a mut PathBuilder,
            x: f32,
            y: f32,
            e: f32,
        }
        impl ttf_parser::OutlineBuilder for Contorno<'_> {
            fn move_to(&mut self, x: f32, y: f32) {
                self.pb.move_to(self.x + x * self.e, self.y - y * self.e);
            }
            fn line_to(&mut self, x: f32, y: f32) {
                self.pb.line_to(self.x + x * self.e, self.y - y * self.e);
            }
            fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
                self.pb.quad_to(self.x + x1 * self.e, self.y - y1 * self.e, self.x + x * self.e, self.y - y * self.e);
            }
            fn curve_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x: f32, y: f32) {
                self.pb.cubic_to(
                    self.x + x1 * self.e, self.y - y1 * self.e,
                    self.x + x2 * self.e, self.y - y2 * self.e,
                    self.x + x * self.e, self.y - y * self.e,
                );
            }
            fn close(&mut self) {
                self.pb.close();
            }
        }
        let mut anterior = None;
        for ch in texto.chars() {
            let Some(g) = face.glyph_index(ch) else { continue };
            if let (Some(a), Some(kern)) = (anterior, face.tables().kern) {
                for sub in kern.subtables {
                    if let Some(k) = sub.glyphs_kerning(a, g) {
                        cx += k as f32 * escala;
                        break;
                    }
                }
            }
            let mut c = Contorno { pb: &mut pb, x: cx, y: base, e: escala };
            face.outline_glyph(g, &mut c);
            cx += face.glyph_hor_advance(g).unwrap_or(0) as f32 * escala;
            anterior = Some(g);
        }
        if let Some(c) = pb.finish() {
            self.p.fill_path(&c, &tinta(cor), FillRule::Winding, Transform::identity(), None);
        }
    }
}

/// Grava em PNG com paleta: o papel e as misturas dele com cada tinta (o
/// desenho só tem essas cores, com as bordas suavizadas).
fn gravar_png(p: &Pixmap, saida: &Path, papel: Rgb, tintas: &[Rgb]) {
    let mut paleta: Vec<Rgb> = vec![papel];
    for t in tintas {
        for i in 1..=10 {
            let f = i as f32 / 10.0;
            let mix = |a: u8, b: u8| (a as f32 + (b as f32 - a as f32) * f).round() as u8;
            paleta.push((mix(papel.0, t.0), mix(papel.1, t.1), mix(papel.2, t.2)));
        }
    }
    let mut indices = Vec::with_capacity((L * A) as usize);
    let mut memo = std::collections::HashMap::new();
    for px in p.pixels() {
        let c = px.demultiply();
        let k = (c.red(), c.green(), c.blue());
        let i = *memo.entry(k).or_insert_with(|| {
            paleta
                .iter()
                .enumerate()
                .min_by_key(|(_, q)| {
                    let d = |a: u8, b: u8| (a as i32 - b as i32).pow(2);
                    d(q.0, k.0) + d(q.1, k.1) + d(q.2, k.2)
                })
                .map(|(i, _)| i as u8)
                .unwrap()
        });
        indices.push(i);
    }
    let f = std::fs::File::create(saida).unwrap_or_else(|e| sair(format!("{}: {e}", saida.display())));
    let mut enc = png::Encoder::new(std::io::BufWriter::new(f), L, A);
    enc.set_color(png::ColorType::Indexed);
    enc.set_depth(png::BitDepth::Eight);
    enc.set_compression(png::Compression::Best);
    enc.set_palette(paleta.iter().flat_map(|c| [c.0, c.1, c.2]).collect::<Vec<u8>>());
    let mut w = enc.write_header().unwrap_or_else(|e| sair(format!("png: {e}")));
    w.write_image_data(&indices).unwrap_or_else(|e| sair(format!("png: {e}")));
}

pub fn main(args: &[String]) {
    if args.len() < 2 {
        sair(USO);
    }
    let (fontes, saida) = (Path::new(&args[0]), Path::new(&args[1]));
    let noite = args.get(2).map(String::as_str) == Some("noite");
    // A série (1.2) do arquivo VERSAO do repositório.
    let versao = ler(topo().join("VERSAO"));
    let versao = versao.trim();
    let serie = versao.rsplit_once('.').map(|(a, _)| a).unwrap_or(versao);

    let (papel, tinta_, cinza, fio): (Rgb, Rgb, Rgb, Rgb) = if noite {
        ((22, 22, 22), (232, 229, 221), (150, 148, 142), (70, 69, 66))
    } else {
        ((244, 242, 236), (26, 26, 26), (120, 118, 112), (196, 193, 185))
    };
    let fonte = |n: &str| {
        let p = fontes.join(format!("{n}.ttf"));
        std::fs::read(&p).unwrap_or_else(|e| sair(format!("{}: {e}", p.display())))
    };
    let mut pm = Pixmap::new(L, A).unwrap();
    pm.fill(tiny_skia::Color::from_rgba8(papel.0, papel.1, papel.2, 255));
    let mut d = Folha { p: pm };
    let (lf, af) = (L as f32, A as f32);
    let _ = af;

    // Horizonte: um fio que atravessa a página.
    let hy = 1500.0;
    d.linha(&[(0.0, hy), (lf, hy)], fio, 2.0);

    // Farol: torre em trapézio sobre a rocha, à direita.
    let cx = 2880.0;
    let (base, topo_) = (hy - 10.0, 760.0);
    let (lb, lt) = (150.0, 92.0); // meia-largura na base e no topo
    d.linha(&[(cx - lb, base), (cx - lt, topo_), (cx + lt, topo_), (cx + lb, base)], tinta_, 4.0);
    // Faixas da torre (como as listras pintadas)
    for t in [0.18f32, 0.42, 0.66] {
        let y = topo_ + (base - topo_) * t;
        let meia = lt + (lb - lt) * t;
        let altura = 70.0;
        let meia2 = lt + (lb - lt) * ((y + altura - topo_) / (base - topo_));
        d.poligono(&[(cx - meia, y), (cx + meia, y), (cx + meia2, y + altura), (cx - meia2, y + altura)], tinta_);
    }
    // Porta
    d.linha(&[(cx - 22.0, base), (cx - 22.0, base - 70.0), (cx + 22.0, base - 70.0), (cx + 22.0, base)], tinta_, 3.0);
    // Galeria, guarda-corpo e lanterna
    d.linha(&[(cx - 130.0, topo_), (cx + 130.0, topo_)], tinta_, 5.0);
    for x in (-120..=120).step_by(30) {
        let x = x as f32;
        d.linha(&[(cx + x, topo_), (cx + x, topo_ - 34.0)], tinta_, 2.0);
    }
    d.linha(&[(cx - 125.0, topo_ - 34.0), (cx + 125.0, topo_ - 34.0)], tinta_, 3.0);
    let (lan_b, lan_t) = (topo_ - 34.0, topo_ - 160.0);
    d.linha(&[(cx - 62.0, lan_b), (cx - 62.0, lan_t), (cx + 62.0, lan_t), (cx + 62.0, lan_b)], tinta_, 4.0);
    d.linha(&[(cx - 20.0, lan_b), (cx - 20.0, lan_t)], tinta_, 2.0);
    d.linha(&[(cx + 20.0, lan_b), (cx + 20.0, lan_t)], tinta_, 2.0);
    // Cúpula
    let cupula: Vec<(f32, f32)> = (0..=180)
        .step_by(6)
        .map(|a| {
            let r = (a as f32).to_radians();
            (cx + 72.0 * r.cos(), lan_t - 70.0 * r.sin())
        })
        .collect();
    d.linha(&cupula, tinta_, 4.0);
    d.linha(&[(cx, lan_t - 70.0), (cx, lan_t - 110.0)], tinta_, 3.0);
    // A luz: raios tracejados para os dois lados
    let fy = (lan_b + lan_t) / 2.0;
    for ang in [-9.0f32, -3.0, 3.0, 9.0] {
        for lado in [-1.0f32, 1.0] {
            let (r0, r1) = (110.0, if lado < 0.0 { 1500.0 } else { 700.0 });
            let a = ang.to_radians();
            let mut r = r0;
            while r < r1 {
                let (x0, y0) = (cx + lado * r * a.cos(), fy - r * a.sin());
                let (x1, y1) = (cx + lado * (r + 34.0) * a.cos(), fy - (r + 34.0) * a.sin());
                d.linha(&[(x0, y0), (x1, y1)], cinza, 2.0);
                r += 64.0;
            }
        }
    }
    // Rocha
    d.linha(
        &[(cx - 260.0, hy), (cx - 190.0, hy - 26.0), (cx - 110.0, hy - 14.0),
          (cx + 120.0, hy - 18.0), (cx + 210.0, hy - 30.0), (cx + 290.0, hy)],
        tinta_, 4.0,
    );
    // Mar: ondas em fio
    for (i, y) in [hy + 90.0, hy + 170.0, hy + 260.0].into_iter().enumerate() {
        let mut x = 1700.0 + i as f32 * 120.0;
        while x < lf - 200.0 {
            let onda: Vec<(f32, f32)> = (0..=240)
                .step_by(12)
                .map(|t| (x + t as f32, y - 12.0 * (PI * t as f32 / 120.0).sin()))
                .collect();
            d.linha(&onda, if i > 0 { fio } else { cinza }, 2.0);
            x += 420.0 + i as f32 * 60.0;
        }
    }

    // Folha de rosto, à esquerda
    let mx = 360.0;
    d.texto(mx, 1180.0, "LumeBSD", &fonte("IBMPlexSerif-Light"), 150.0, tinta_);
    d.texto(mx + 6.0, 1390.0, "luz firme sobre fundação BSD", &fonte("IBMPlexSerif-Italic"), 52.0, cinza);
    d.texto(mx + 8.0, 1560.0, &format!("{serie}  ·  FAROL"), &fonte("IBMPlexMono-Regular"), 34.0, cinza);

    gravar_png(&d.p, saida, papel, &[tinta_, cinza, fio]);
    println!("{}", saida.display());
}
