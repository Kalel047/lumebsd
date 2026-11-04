SCRIPTS=	lume-configurar.sh lume-hardware.sh lume-video.sh lume-wifi.sh \
	lume-desktop.sh lume-instalar.sh lume-tema.sh lume-atualizar.sh \
	lume-tempo.sh
MAN=	lume-configurar.8 lume-tempo.8

FILESGROUPS=	FILES TEMA CRON
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
		tema/navegador.svg tema/lume-tempo.desktop

# Máquina do tempo: o cron confere a cada 15 minutos (lume tempo).
CRONDIR=	/etc/cron.d
CRON=		cron-lume-tempo
CRONNAME=	lume-tempo

.include <bsd.prog.mk>
