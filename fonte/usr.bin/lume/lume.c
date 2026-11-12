/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * lume(1) - painel do sistema e utilitário de identidade do LumeBSD.
 *
 *   lume            painel: sistema, kernel, CPU, memória, disco, carga, uptime
 *   lume farol      desenha o emblema (farol) da distribuição
 *   lume dica       mostra uma dica aleatória
 *   lume versao     imprime a versão da distribuição
 *   lume configurar abre o assistente de configuração (lume-configurar(8))
 *   lume hardware   diz o que funciona neste computador
 *   lume video      escolhe e testa o driver de vídeo
 *   lume wifi       conecta a uma rede Wi-Fi
 *   lume desktop    instala o desktop KDE Plasma
 *   lume instalar   instala o LumeBSD no disco (a partir do pendrive)
 *   lume atualizar  atualiza o sistema num novo ambiente de boot
 *   lume tempo      máquina do tempo: fotos do disco e o caminho de volta
 */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/un.h>

#include <kenv.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define	LUME_NAME	"LumeBSD"
#define	LUME_VERSION	"1.5"
#define	LUME_CODENAME	"Farol"
#define	LUME_MARCA	"/var/db/lume/configurado"
#define	LUME_ASSISTENTE	"/usr/sbin/lume-configurar"
/* Versão completa do sistema (1.1.0, 1.1.1...), gravada no build. */
#define	LUME_VERSAO_ARQ	"/usr/share/lume/versao"
/* Atualização instalada que ainda não passou pela checagem de saúde. */
#define	LUME_PENDENTE	"/var/db/lume/atualizacao-pendente"
/* Resumo da máquina do tempo, gravado pelo lume-tempo a cada 15 minutos. */
#define	LUME_TEMPO_ESTADO	"/var/db/lume/tempo-estado"

/* Subcomandos que são scripts em /usr/sbin/lume-<nome>. */
static const char *scripts[] = {
	"configurar", "hardware", "video", "wifi", "desktop", "instalar",
	"tema", "atualizar", "tempo", "loja", "compat", "ajuda",
};
#define	VIGIA_SOCKET	"/var/run/vigia.sock"

static int use_color;

#define	C(code)	(use_color ? "\033[" code "m" : "")
#define	AMBER	C("33;1")
#define	WHITE	C("37;1")
#define	BLUE	C("34;1")
#define	RED	C("31")
#define	DIM	C("2")
#define	RESET	C("0")

static const char *tips[] = {
	"lume sem argumentos mostra o painel do sistema.",
	"Usava o MATLAB? lume loja procurar matlab mostra o que usar no lugar.",
	"Um kit por área: lume loja kit exatas (ou sociais, ou geral).",
	"Dois cliques num .exe abrem o programa do Windows com o Wine.",
	"lume ajuda abre o guia do sistema em português.",
	"Ctrl+Meta+T liga e desliga o filtro e-ink; Ctrl+Meta+Y só na janela ativa.",
	"Fotos e vídeos ficam com cor: o filtro e-ink sai sozinho dessas janelas.",
	"lume tema animacao esmaecer troca o refresh de e-ink por um esmaecer suave.",
	"lume tema escuro passa para o Papel noite, o tema escuro; lume tema claro volta.",
	"Programas do Windows de 32 bits também abrem: o LumeBSD baixa o que falta na primeira vez.",
	"Um .dmg do Mac? Dois cliques mostram o que usar no lugar.",
	"Apagou sem querer? lume tempo recuperar ARQUIVO traz de volta.",
	"jail(8) isola serviços inteiros com custo quase zero.",
	"service -e lista todos os serviços habilitados no boot.",
	"sysrc(8) edita o rc.conf sem abrir um editor: sysrc hostname=farol",
	"top -SH mostra também as threads do kernel.",
	"sysctl -a | less: o kernel inteiro, exposto como uma árvore.",
	"pciconf -lv lista o hardware PCI com nomes legíveis.",
	"Desative o emblema do boot com: sysrc lume_farol_enable=NO",
	"freebsd-version -ku mostra as versões do kernel e da userland.",
	"man hier explica a organização de diretórios do sistema.",
};

static void
init_color(void)
{
	const char *term = getenv("TERM");

	use_color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL &&
	    (term == NULL || strcmp(term, "dumb") != 0);
}

static int
sysctl_str(const char *name, char *buf, size_t len)
{
	size_t l = len;

	if (sysctlbyname(name, buf, &l, NULL, 0) != 0) {
		strlcpy(buf, "?", len);
		return (-1);
	}
	buf[len - 1] = '\0';
	return (0);
}

static uint64_t
sysctl_u64(const char *name)
{
	union {
		uint32_t u32;
		uint64_t u64;
	} v;
	size_t l = sizeof(v);

	memset(&v, 0, sizeof(v));
	if (sysctlbyname(name, &v, &l, NULL, 0) != 0)
		return (0);
	return (l == sizeof(uint32_t) ? v.u32 : v.u64);
}

static void
human(uint64_t bytes, char *out, size_t len)
{
	const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	double v = (double)bytes;
	int i = 0;

	while (v >= 1024.0 && i < 4) {
		v /= 1024.0;
		i++;
	}
	snprintf(out, len, i == 0 ? "%.0f %s" : "%.1f %s", v, units[i]);
}

static void
bar(double frac, char *out, size_t len)
{
	const int width = 20;
	int n, i;
	size_t p = 0;

	if (frac < 0)
		frac = 0;
	if (frac > 1)
		frac = 1;
	n = (int)(frac * width + 0.5);
	p += snprintf(out + p, len - p, "[%s", frac > 0.85 ? RED : AMBER);
	for (i = 0; i < width && p < len; i++)
		p += snprintf(out + p, len - p, "%s", i < n ? "#" : ".");
	snprintf(out + p, len - p, "%s]", RESET);
}

static void
row(const char *label, const char *value)
{
	const unsigned char *s;
	int cols = 0;

	/* Conta colunas, não bytes: rótulos têm acentos em UTF-8. */
	for (s = (const unsigned char *)label; *s != '\0'; s++)
		if ((*s & 0xC0) != 0x80)
			cols++;
	printf("  %s%s%*s%s %s\n", AMBER, label, cols < 10 ? 10 - cols : 0,
	    "", RESET, value);
}

static void
farol(void)
{
	printf("%s     \\    |    /%s\n", AMBER, RESET);
	printf("%s   ---  (( ))  ---%s\n", AMBER, RESET);
	printf("%s     /   %s_|_%s   \\%s\n", AMBER, WHITE, AMBER, RESET);
	printf("%s        /___\\%s\n", WHITE, RESET);
	printf("%s        |[%s_%s]|%s\n", WHITE, AMBER, WHITE, RESET);
	printf("%s        |%s===%s|%s      %sLumeBSD %s%s\n", WHITE, RED, WHITE,
	    RESET, WHITE, LUME_VERSION, RESET);
	printf("%s        |   |%s      %s\"%s\"%s\n", WHITE, RESET, AMBER,
	    LUME_CODENAME, RESET);
	printf("%s        |%s===%s|%s\n", WHITE, RED, WHITE, RESET);
	printf("%s       /|   |\\%s     %sluz firme sobre fundação BSD%s\n",
	    WHITE, RESET, DIM, RESET);
	printf("%s        |%s===%s|%s\n", WHITE, RED, WHITE, RESET);
	printf("%s       _|___|_%s\n", WHITE, RESET);
	printf("%s  ~~~~%s/_______\\%s~~~~%s\n", BLUE, WHITE, BLUE, RESET);
	printf("%s   ~~   ~~~~   ~~%s\n", BLUE, RESET);
}

/* Pergunta ao vigiad quantos serviços estão de pé. */
static void
servicos(void)
{
	struct sockaddr_un sun;
	struct timeval tv = { 1, 0 };
	char buf[128], txt[160];
	int fd, total, ativos, problemas;
	ssize_t n;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		return;
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, VIGIA_SOCKET, sizeof(sun.sun_path));
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	if (connect(fd, (struct sockaddr *)&sun, SUN_LEN(&sun)) == -1 ||
	    write(fd, "resumo\n", 7) != 7) {
		close(fd);
		return;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return;
	buf[n] = '\0';
	if (sscanf(buf, "ok\n%d\t%d\t%d", &total, &ativos, &problemas) != 3)
		return;
	if (problemas > 0)
		snprintf(txt, sizeof(txt), "%d de %d ativos, %s%d com "
		    "problema%s (vigia)", ativos, total, RED, problemas, RESET);
	else
		snprintf(txt, sizeof(txt), "%d de %d ativos (vigia)", ativos,
		    total);
	row("serviços", txt);
}

/* Versão do build (1.1.1) ou, sem o arquivo, a da distribuição (1.1). */
static const char *
versao(void)
{
	static char v[32];
	FILE *f;

	if ((f = fopen(LUME_VERSAO_ARQ, "r")) != NULL) {
		if (fgets(v, sizeof(v), f) != NULL) {
			v[strcspn(v, "\n")] = '\0';
			fclose(f);
			if (v[0] != '\0')
				return (v);
		} else
			fclose(f);
	}
	return (LUME_VERSION);
}

/* Linha "sistema": versão e ambiente de boot em uso. */
static void
sistema(void)
{
	char raiz[128], txt[192];
	const char *be;

	if (kenv(KENV_GET, "vfs.root.mountfrom", raiz, sizeof(raiz)) > 0 &&
	    strncmp(raiz, "zfs:", 4) == 0 && (be = strrchr(raiz, '/')) != NULL)
		snprintf(txt, sizeof(txt), "%s (ambiente de boot %s)", versao(),
		    be + 1);
	else
		snprintf(txt, sizeof(txt), "%s", versao());
	row("sistema", txt);
	if (access(LUME_PENDENTE, F_OK) == 0)
		printf("  %s!%s atualização em teste: ainda não confirmada "
		    "(%slume atualizar estado%s)\n", RED, RESET, AMBER, RESET);
}

/* Linha "tempo": a última foto da máquina do tempo e quantas há. */
static void
tempo(void)
{
	char linha[64], txt[160], quanto[32], ocupa[32];
	long long fotos = -1, ultima = 0, bytes = 0;
	time_t idade;
	FILE *f;

	if ((f = fopen(LUME_TEMPO_ESTADO, "r")) == NULL)
		return;
	while (fgets(linha, sizeof(linha), f) != NULL) {
		sscanf(linha, "fotos=%lld", &fotos);
		sscanf(linha, "ultima=%lld", &ultima);
		sscanf(linha, "ocupa=%lld", &bytes);
	}
	fclose(f);
	if (fotos < 0)
		return;
	if (fotos == 0 || ultima <= 0) {
		row("tempo", "nenhuma foto ainda (lume tempo)");
		return;
	}
	idade = time(NULL) - (time_t)ultima;
	if (idade < 120)
		snprintf(quanto, sizeof(quanto), "agora há pouco");
	else if (idade < 7200)
		snprintf(quanto, sizeof(quanto), "há %jd min",
		    (intmax_t)(idade / 60));
	else if (idade < 172800)
		snprintf(quanto, sizeof(quanto), "há %jd h",
		    (intmax_t)(idade / 3600));
	else
		snprintf(quanto, sizeof(quanto), "%shá %jd dias%s", RED,
		    (intmax_t)(idade / 86400), RESET);
	human((uint64_t)bytes, ocupa, sizeof(ocupa));
	snprintf(txt, sizeof(txt), "última foto %s, %lld no total (%s)",
	    quanto, fotos, ocupa);
	row("tempo", txt);
}

static void
painel(void)
{
	char host[256], ostype[64], osrel[64], model[128], buf[256];
	char a[32], b[32], g[64];
	struct timeval boot;
	struct statfs sfs;
	size_t l;
	double load[3];
	uint64_t physmem, pagesize, freepg, inact, ncpu;
	time_t up;

	sysctl_str("kern.hostname", host, sizeof(host));
	sysctl_str("kern.ostype", ostype, sizeof(ostype));
	sysctl_str("kern.osrelease", osrel, sizeof(osrel));
	sysctl_str("hw.model", model, sizeof(model));
	ncpu = sysctl_u64("hw.ncpu");
	physmem = sysctl_u64("hw.physmem");
	pagesize = sysctl_u64("hw.pagesize");
	freepg = sysctl_u64("vm.stats.vm.v_free_count");
	inact = sysctl_u64("vm.stats.vm.v_inactive_count");

	printf("\n  %s%s %s%s %s\"%s\"%s  %s@ %s%s\n", WHITE, LUME_NAME,
	    LUME_VERSION, RESET, AMBER, LUME_CODENAME, RESET, DIM, host, RESET);
	printf("  %s----------------------------------------------%s\n",
	    DIM, RESET);

	snprintf(buf, sizeof(buf), "%s %s (base FreeBSD 15.1)", ostype, osrel);
	row("kernel", buf);

	snprintf(buf, sizeof(buf), "%s (%ju núcleos)", model, (uintmax_t)ncpu);
	row("cpu", buf);

	if (physmem > 0 && pagesize > 0) {
		uint64_t avail = (freepg + inact) * pagesize;
		uint64_t used = physmem > avail ? physmem - avail : 0;

		human(used, a, sizeof(a));
		human(physmem, b, sizeof(b));
		bar((double)used / (double)physmem, g, sizeof(g));
		snprintf(buf, sizeof(buf), "%s %s / %s", g, a, b);
		row("memória", buf);
	}

	if (statfs("/", &sfs) == 0) {
		uint64_t total = (uint64_t)sfs.f_blocks * sfs.f_bsize;
		uint64_t freeb = (uint64_t)sfs.f_bavail * sfs.f_bsize;
		uint64_t used = total > freeb ? total - freeb : 0;

		human(used, a, sizeof(a));
		human(total, b, sizeof(b));
		bar(total ? (double)used / (double)total : 0, g, sizeof(g));
		snprintf(buf, sizeof(buf), "%s %s / %s (%s)", g, a, b,
		    sfs.f_fstypename);
		row("disco /", buf);
	}

	if (getloadavg(load, 3) == 3) {
		snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f", load[0],
		    load[1], load[2]);
		row("carga", buf);
	}

	l = sizeof(boot);
	if (sysctlbyname("kern.boottime", &boot, &l, NULL, 0) == 0) {
		up = time(NULL) - boot.tv_sec;
		if (up >= 86400)
			snprintf(buf, sizeof(buf), "%jd d %jd h %jd min",
			    (intmax_t)(up / 86400), (intmax_t)(up % 86400 / 3600),
			    (intmax_t)(up % 3600 / 60));
		else
			snprintf(buf, sizeof(buf), "%jd h %jd min %jd s",
			    (intmax_t)(up / 3600), (intmax_t)(up % 3600 / 60),
			    (intmax_t)(up % 60));
		row("ligado há", buf);
	}
	servicos();
	sistema();
	tempo();
	if (access(LUME_MARCA, F_OK) != 0)
		printf("\n  %s!%s sistema ainda não configurado: rode %slume "
		    "configurar%s como root\n", RED, RESET, AMBER, RESET);
	printf("\n");
}

static void
usage(void)
{
	fprintf(stderr,
	    "uso: lume [farol | dica | versao]\n"
	    "     lume configurar | hardware | wifi | desktop | instalar\n"
	    "     lume tema [papel [noite] | escuro | claro | padrao |\n"
	    "                tinta [liga | desliga | 0-100] |\n"
	    "                animacao [refresh | esmaecer | nenhuma]]\n"
	    "     lume video [testar | auto | nvidia | intel | amd | scfb]\n"
	    "     lume atualizar [verificar | aplicar | estado | voltar]\n"
	    "     lume tempo [linha | versoes | recuperar | foto | mudou | voltar]\n"
	    "     lume loja [lista | procurar | info | instalar | remover | kit]\n"
	    "     lume compat [linux ligar | windows | abrir ARQUIVO]\n"
	    "     lume ajuda [programas | trocando | windows | wifi | problemas]\n"
	    "     lume atributo ARQUIVO [NOME[=VALOR]...] | procurar CONDIÇÃO...\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	init_color();

	if (argc == 1) {
		painel();
		return (0);
	}
	for (size_t i = 0; i < nitems(scripts); i++) {
		if (strcmp(argv[1], scripts[i]) == 0) {
			char caminho[64];

			snprintf(caminho, sizeof(caminho), "/usr/sbin/lume-%s",
			    scripts[i]);
			argv[1] = caminho;
			execv(caminho, argv + 1);
			err(1, "%s", caminho);
		}
	}
	/* Programas em /usr/bin/lume-<nome>. */
	if (strcmp(argv[1], "atributo") == 0) {
		static char atributo[] = "/usr/bin/lume-atributo";

		argv[1] = atributo;
		execv(argv[1], argv + 1);
		err(1, "%s", argv[1]);
	}
	if (argc != 2)
		usage();

	if (strcmp(argv[1], "farol") == 0)
		farol();
	else if (strcmp(argv[1], "dica") == 0)
		printf("%sdica:%s %s\n", AMBER, RESET,
		    tips[arc4random_uniform(nitems(tips))]);
	else if (strcmp(argv[1], "versao") == 0 ||
	    strcmp(argv[1], "-v") == 0)
		printf("%s %s \"%s\"\n", LUME_NAME, versao(),
		    LUME_CODENAME);
	else
		usage();
	return (0);
}
