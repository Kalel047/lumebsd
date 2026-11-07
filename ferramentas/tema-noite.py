"""Gera o tema "Papel noite" (escuro) a partir do Papel.

uso: tema-noite.py TEMA
     TEMA: a pasta do tema na árvore (usr.sbin/lume-configurar/tema)

O Papel é tinta sobre papel; o Papel noite é o mesmo papel visto à noite: o
fundo é a tinta (um preto quente, não o preto puro, que cansa num LCD) e o
texto é o papel, um pouco menos claro, para não ofuscar no escuro. Cada cor do
Papel tem a sua par aqui (MAPA): os dois temas ficam sempre coerentes, e uma
cor nova no Papel que não estiver no mapa para a geração com erro.

Gera: PapelNoite.colors (KDE), PapelNoite.colorscheme e .profile (Konsole),
kdeglobals-noite, konsolerc-noite, gtk-settings-noite.ini e o tema global
(metadata-noite.json, defaults-noite, layout-noite.js).
"""
import os
import re
import sys

# Papel -> Papel noite. Fundos viram tinta, tinta vira papel, cinzas trocam
# de lado; o vermelho de revisor fica mais claro para ler sobre o escuro.
MAPA = {
    (244, 242, 236): (30, 29, 27),     # papel: o fundo
    (235, 233, 226): (39, 38, 35),     # papel alternado, botões, cabeçalhos
    (26, 26, 26): (222, 218, 208),     # tinta: o texto
    (105, 104, 100): (150, 147, 139),  # texto inativo
    (90, 90, 90): (176, 172, 163),     # contorno ao passar o mouse
    (96, 92, 84): (178, 171, 156),     # neutro
    (150, 40, 32): (226, 124, 108),    # vermelho de revisor (erros)
    (60, 60, 58): (204, 201, 193),     # positivo
    (90, 89, 86): (170, 166, 158),     # link visitado
    (196, 193, 185): (78, 76, 72),     # desabilitado
    # Seleção: tinta sobre papel no Papel; papel sobre tinta aqui.
    (50, 50, 50): (200, 196, 186),
    (70, 70, 70): (184, 180, 170),
    (200, 198, 192): (72, 70, 66),
    # Complementar (OSD, painéis escuros no Papel): fica um tom abaixo do fundo.
    (40, 40, 40): (26, 25, 23),
    (170, 168, 162): (150, 147, 139),
    (220, 120, 110): (226, 124, 108),
}

COR = re.compile(r"^(\w+)=(\d+),(\d+),(\d+)$")


def trocar_kde(texto):
    """Troca as cores de um arquivo de cores do KDE (Papel.colors, kdeglobals)."""
    saida, secao = [], ""
    for linha in texto.splitlines():
        if linha.startswith("["):
            secao = linha
        m = COR.match(linha)
        if m:
            chave, rgb = m.group(1), tuple(int(x) for x in m.group(2, 3, 4))
            if secao == "[Colors:Complementary]":
                novo = {(26, 26, 26): (20, 19, 18), (244, 242, 236): (222, 218, 208),
                        (200, 200, 200): (176, 172, 163)}.get(rgb) or MAPA.get(rgb)
            else:
                novo = MAPA.get(rgb)
            if novo is None:
                sys.exit(f"tema-noite: a cor {rgb} ({secao} {chave}) não está no mapa")
            linha = f"{chave}={novo[0]},{novo[1]},{novo[2]}"
        saida.append(linha)
    return "\n".join(saida) + "\n"


def nomes(texto):
    return (texto.replace("ColorScheme=Papel", "ColorScheme=PapelNoite")
            .replace("Name=Papel\n", "Name=Papel noite\n")
            .replace("LookAndFeelPackage=org.lumebsd.papel", "LookAndFeelPackage=org.lumebsd.papel-noite")
            .replace("Theme=lume-papel", "Theme=lume-papel-noite"))


# O terminal à noite: fundo de tinta, letras de papel; as cores ANSI são as
# mesmas tintas desbotadas do Papel, agora claras sobre o escuro.
KONSOLE = """# Esquema de cores do Konsole "Papel noite" (LumeBSD): o Papel à noite.
# Gerado por ferramentas/tema-noite.py a partir do Papel.
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
"""


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    t = sys.argv[1]

    def ler(n):
        return open(os.path.join(t, n)).read()

    def gravar(n, texto):
        with open(os.path.join(t, n), "w") as f:
            f.write(texto)

    cab = "# Gerado por ferramentas/tema-noite.py a partir do Papel; não edite à mão.\n"
    colors = trocar_kde(ler("Papel.colors"))
    colors = colors.replace('# Esquema de cores "Papel" do LumeBSD: estética e-ink.',
                            '# Esquema de cores "Papel noite" do LumeBSD: o Papel à noite.')
    gravar("PapelNoite.colors", cab + nomes(colors))

    kg = trocar_kde(ler("kdeglobals"))
    kg = kg.replace("(tema Papel)", "(tema Papel noite)")
    kg = re.sub(r"^AccentColor=.*$", "AccentColor=222,218,208", kg, flags=re.M)
    gravar("kdeglobals-noite", cab + nomes(kg))

    gravar("PapelNoite.colorscheme", KONSOLE)
    gravar("PapelNoite.profile", cab + ler("Papel.profile")
           .replace("ColorScheme=Papel", "ColorScheme=PapelNoite")
           .replace("Name=Papel", "Name=Papel noite"))
    gravar("konsolerc-noite", cab + ler("konsolerc")
           .replace("DefaultProfile=Papel.profile", "DefaultProfile=PapelNoite.profile"))
    gravar("gtk-settings-noite.ini", ler("gtk-settings.ini")
           .replace("gtk-theme-name=Breeze\n", "gtk-theme-name=Breeze-Dark\n")
           .replace("gtk-icon-theme-name=lume-papel", "gtk-icon-theme-name=lume-papel-noite")
           .replace("gtk-application-prefer-dark-theme=false", "gtk-application-prefer-dark-theme=true"))

    meta = ler("metadata.json")
    meta = (meta.replace('"Papel: tinta sobre papel, sem animações nem sombras (estética e-ink)."',
                         '"Papel noite: o Papel à noite, papel claro sobre tinta, sem animações nem sombras."')
            .replace('"org.lumebsd.papel"', '"org.lumebsd.papel-noite"')
            .replace('"Papel (LumeBSD)"', '"Papel noite (LumeBSD)"'))
    gravar("metadata-noite.json", meta)
    gravar("layout-noite.js", ler("layout.js")
           .replace("(tema Papel)", "(tema Papel noite)")
           .replace("wallpapers/LumePapel/", "wallpapers/LumePapelNoite/"))
    gravar("defaults-noite", nomes(ler("defaults")).replace("Image=LumePapel\n", "Image=LumePapelNoite\n"))
    print("tema-noite: PapelNoite.colors, kdeglobals-noite, Konsole, GTK e o tema global gerados")


if __name__ == "__main__":
    main()
