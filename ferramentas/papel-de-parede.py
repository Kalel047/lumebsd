"""Desenha o papel de parede "Papel" do LumeBSD (estética e-ink).

uso: papel-de-parede.py PASTA_DAS_FONTES SAIDA.png [noite]

Papel quase branco, tinta quase preta, só traço: o farol à direita, a linha
do horizonte e o nome da distribuição em serifa, como a folha de rosto de um
livro. Com "noite", as cores se invertem (tela de login e de bloqueio, como
a tela de descanso de um leitor de e-book).
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

fontes, saida = sys.argv[1], sys.argv[2]
noite = len(sys.argv) > 3 and sys.argv[3] == "noite"
# A série (1.2) do arquivo VERSAO do repositório.
SERIE = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          "VERSAO")).read().strip().rsplit(".", 1)[0]

L, A = 3840, 2160
S = 2                      # desenha em 2x e reduz: traço suave
PAPEL = (244, 242, 236)
TINTA = (26, 26, 26)
CINZA = (120, 118, 112)
FIO = (196, 193, 185)
if noite:
    PAPEL, TINTA, CINZA, FIO = (22, 22, 22), (232, 229, 221), (150, 148, 142), (70, 69, 66)

img = Image.new("RGB", (L * S, A * S), PAPEL)
d = ImageDraw.Draw(img)


def p(x, y):
    return (x * S, y * S)


def linha(pontos, cor=TINTA, w=4):
    d.line([p(*q) for q in pontos], fill=cor, width=w * S, joint="curve")


def fonte(nome, tam):
    return ImageFont.truetype(f"{fontes}/{nome}.ttf", tam * S)


# Horizonte: um fio que atravessa a página.
hy = 1500
linha([(0, hy), (L, hy)], FIO, 2)

# Farol: torre em trapézio sobre a rocha, à direita.
cx = 2880
base, topo = hy - 10, 760
lb, lt = 150, 92                       # meia-largura na base e no topo
linha([(cx - lb, base), (cx - lt, topo), (cx + lt, topo), (cx + lb, base)])
# Faixas da torre (como as listras pintadas)
for t in (0.18, 0.42, 0.66):
    y = topo + (base - topo) * t
    meia = lt + (lb - lt) * t
    altura = 70
    meia2 = lt + (lb - lt) * ((y + altura - topo) / (base - topo))
    d.polygon([p(cx - meia, y), p(cx + meia, y), p(cx + meia2, y + altura),
               p(cx - meia2, y + altura)], fill=TINTA)
# Porta
linha([(cx - 22, base), (cx - 22, base - 70), (cx + 22, base - 70),
       (cx + 22, base)], TINTA, 3)
# Galeria, guarda-corpo e lanterna
linha([(cx - 130, topo), (cx + 130, topo)], TINTA, 5)
for x in range(-120, 121, 30):
    linha([(cx + x, topo), (cx + x, topo - 34)], TINTA, 2)
linha([(cx - 125, topo - 34), (cx + 125, topo - 34)], TINTA, 3)
lan_b, lan_t = topo - 34, topo - 160
linha([(cx - 62, lan_b), (cx - 62, lan_t), (cx + 62, lan_t), (cx + 62, lan_b)],
      TINTA, 4)
linha([(cx - 20, lan_b), (cx - 20, lan_t)], TINTA, 2)
linha([(cx + 20, lan_b), (cx + 20, lan_t)], TINTA, 2)
# Cúpula
cupula = [(cx + 72 * math.cos(math.radians(a)),
           lan_t - 70 * math.sin(math.radians(a))) for a in range(0, 181, 6)]
linha(cupula, TINTA, 4)
linha([(cx, lan_t - 70), (cx, lan_t - 110)], TINTA, 3)
# A luz: raios tracejados para os dois lados
fy = (lan_b + lan_t) / 2
for ang in (-9, -3, 3, 9):
    for lado in (-1, 1):
        r0, r1 = 110, 1500 if lado < 0 else 700
        a = math.radians(ang)
        r = r0
        while r < r1:
            x0 = cx + lado * r * math.cos(a)
            y0 = fy - r * math.sin(a)
            x1 = cx + lado * (r + 34) * math.cos(a)
            y1 = fy - (r + 34) * math.sin(a)
            linha([(x0, y0), (x1, y1)], CINZA, 2)
            r += 64
# Rocha
linha([(cx - 260, hy), (cx - 190, hy - 26), (cx - 110, hy - 14),
       (cx + 120, hy - 18), (cx + 210, hy - 30), (cx + 290, hy)], TINTA, 4)
# Mar: ondas em fio
for i, y in enumerate((hy + 90, hy + 170, hy + 260)):
    x = 1700 + i * 120
    while x < L - 200:
        onda = [(x + t, y - 12 * math.sin(math.pi * t / 120))
                for t in range(0, 241, 12)]
        linha(onda, FIO if i else CINZA, 2)
        x += 420 + i * 60

# Folha de rosto, à esquerda
mx = 360
d.text(p(mx, 1180), "LumeBSD", font=fonte("IBMPlexSerif-Light", 150),
       fill=TINTA)
d.text(p(mx + 6, 1390), "luz firme sobre fundação BSD",
       font=fonte("IBMPlexSerif-Italic", 52), fill=CINZA)
d.text(p(mx + 8, 1560), f"{SERIE}  ·  FAROL", font=fonte("IBMPlexMono-Regular", 34),
       fill=CINZA)

img = img.resize((L, A), Image.LANCZOS).convert("P", palette=Image.ADAPTIVE,
                                                colors=32)
img.save(saida, optimize=True)
print(saida)
