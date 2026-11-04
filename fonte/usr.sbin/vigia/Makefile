PROG=	vigia
SRCS=	main.c manifesto.c servidor.c cliente.c
MAN=	vigia.8 vigia.conf.5
LINKS=	${BINDIR}/vigia ${BINDIR}/vigiad
MLINKS=	vigia.8 vigiad.8

FILESGROUPS=	EXEMPLOS SERVICOS
EXEMPLOS=	relogio.conf web.conf instavel.conf pesado.conf
EXEMPLOSDIR=	${SHAREDIR}/examples/vigia
SERVICOS=	ntpd.conf
SERVICOSDIR=	/etc/vigia/servicos
SERVICOSMODE=	0644

.include <bsd.prog.mk>
