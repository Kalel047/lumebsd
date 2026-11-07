SCRIPTS=	lume-configurar.sh lume-hardware.sh lume-video.sh lume-wifi.sh \
	lume-desktop.sh lume-instalar.sh lume-tema.sh lume-atualizar.sh \
	lume-tempo.sh lume-loja.sh lume-compat.sh lume-ajuda.sh
MAN=	lume-configurar.8 lume-tempo.8 lume-loja.8 lume-compat.8

FILESGROUPS=	FILES TEMA CRON LOJA ATALHOS AJUDA
FILESDIR=	${SHAREDIR}/lume
FILES=		desktop.txt

# Tema "Papel" (estética e-ink) para o KDE; aplicado por lume tema.
TEMADIR=	${SHAREDIR}/lume/tema
TEMA=		tema/Papel.colors tema/Papel.colorscheme tema/Papel.profile \
		tema/kdeglobals tema/kwinrc tema/breezerc tema/ksplashrc \
		tema/plasmarc tema/konsolerc tema/kscreenlockerrc \
		tema/metadata.json tema/defaults tema/layout.js \
		tema/sddm.conf tema/sddm-breeze.conf.user tema/fontes.conf \
		tema/gtk-settings.ini tema/papel.png tema/papel-noite.png \
		tema/navegador.svg tema/lume-tempo.desktop \
		tema/org.kde.kglobalaccel.service \
		tema/PapelNoite.colors tema/PapelNoite.colorscheme \
		tema/PapelNoite.profile tema/kdeglobals-noite tema/konsolerc-noite \
		tema/gtk-settings-noite.ini tema/metadata-noite.json \
		tema/defaults-noite tema/layout-noite.js

# Programas do LumeBSD (lume loja): o catálogo, os kits e o atalho que abre
# programas do Windows com dois cliques (lume compat).
LOJADIR=	${SHAREDIR}/lume/loja
LOJA=		loja/catalogo loja/lume-windows.desktop loja/lume-mac.desktop
ATALHOSDIR=	${SHAREDIR}/lume/loja/atalhos
ATALHOS=	loja/atalhos/jupyterlab.desktop

# A Ajuda do LumeBSD (lume ajuda), em HTML.
AJUDADIR=	${SHAREDIR}/lume/ajuda
AJUDA=		ajuda/index.html ajuda/programas.html ajuda/trocando.html \
		ajuda/compatibilidade.html ajuda/internet.html \
		ajuda/atualizacoes.html ajuda/problemas.html ajuda/tela.html \
		ajuda/estilo.css \
		ajuda/lume-ajuda.desktop

# Máquina do tempo: o cron confere a cada 15 minutos (lume tempo).
CRONDIR=	/etc/cron.d
CRON=		cron-lume-tempo
CRONNAME=	lume-tempo

.include <bsd.prog.mk>
