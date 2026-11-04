/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * vigia - comando para consultar e controlar o vigiad.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vigia.h"

static int cor;

#define	C(code)	(cor ? "\033[" code "m" : "")
#define	VERDE	C("32;1")
#define	AMARELO	C("33;1")
#define	VERMELHO C("31;1")
#define	FRACO	C("2")
#define	NEGRITO	C("1")
#define	FIM	C("0")

static void __dead2
uso(void)
{
	fprintf(stderr,
	    "uso: vigia [estado]\n"
	    "     vigia iniciar | parar | reiniciar <serviço>\n"
	    "     vigia ativar | desativar <serviço>\n"
	    "     vigia info <serviço>\n"
	    "     vigia log <serviço> [-n linhas] [-f]\n"
	    "     vigia recarregar\n");
	exit(2);
}

/*
 * Envia um pedido ao vigiad. Em caso de sucesso devolve o corpo da resposta
 * (depois da linha "ok"); em caso de erro, imprime a mensagem e sai.
 */
static char *
pedir(const char *pedido)
{
	struct sockaddr_un sun;
	char *buf, *p;
	size_t tam = 4096, n = 0;
	ssize_t r;
	int fd;

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, VIGIA_SOCKET, sizeof(sun.sun_path));
	if (connect(fd, (struct sockaddr *)&sun, SUN_LEN(&sun)) == -1)
		errx(1, "o vigiad não está rodando (inicie com: "
		    "service vigia start)");
	if (dprintf(fd, "%s\n", pedido) < 0)
		err(1, "enviar pedido");
	shutdown(fd, SHUT_WR);

	if ((buf = malloc(tam)) == NULL)
		err(1, NULL);
	for (;;) {
		if (n + 1 >= tam) {
			tam *= 2;
			if ((buf = realloc(buf, tam)) == NULL)
				err(1, NULL);
		}
		r = read(fd, buf + n, tam - 1 - n);
		if (r == -1 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		n += (size_t)r;
	}
	buf[n] = '\0';
	close(fd);

	if (strncmp(buf, "ok\n", 3) == 0)
		return (buf + 3);
	if (strncmp(buf, "erro: ", 6) == 0) {
		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		errx(1, "%s", buf + 6);
	}
	errx(1, "resposta inesperada do vigiad");
}

/* Largura em colunas (conta caracteres UTF-8, não bytes). */
static int
largura(const char *s)
{
	int n = 0;

	for (; *s != '\0'; s++)
		if (((unsigned char)*s & 0xC0) != 0x80)
			n++;
	return (n);
}

static void
coluna(const char *s, int w)
{
	int i;

	fputs(s, stdout);
	for (i = largura(s); i < w; i++)
		putchar(' ');
}

static const char *
cor_estado(const char *e)
{
	if (strcmp(e, "ativo") == 0 || strcmp(e, "ouvindo") == 0)
		return (VERDE);
	if (strcmp(e, "manutenção") == 0 || strcmp(e, "erro") == 0)
		return (VERMELHO);
	if (strcmp(e, "parado") == 0 || strcmp(e, "desativado") == 0)
		return (FRACO);
	return (AMARELO);
}

static int
problema(const char *e)
{
	return (strcmp(e, "ativo") != 0 && strcmp(e, "parado") != 0 &&
	    strcmp(e, "desativado") != 0 && strcmp(e, "ouvindo") != 0);
}

/* Divide uma linha em campos separados por tab. */
static int
campos(char *linha, char **f, int max)
{
	int n = 0;

	while (n < max) {
		f[n++] = linha;
		if ((linha = strchr(linha, '\t')) == NULL)
			break;
		*linha++ = '\0';
	}
	return (n);
}

static int
cmd_estado(void)
{
	char *corpo, *linha, *ctx, *f[10], dur[32], num[32], mem[32];
	long long m;
	int n, total = 0, ativos = 0, sob_demanda;

	corpo = pedir("estado");
	printf("\n  %s", NEGRITO);
	coluna("SERVIÇO", 14);
	coluna("ESTADO", 15);
	coluna("PID", 9);
	coluna("MEMÓRIA", 11);
	coluna("HÁ", 11);
	coluna("REINÍCIOS", 11);
	printf("DESCRIÇÃO%s\n", FIM);

	for (linha = strtok_r(corpo, "\n", &ctx); linha != NULL;
	    linha = strtok_r(NULL, "\n", &ctx)) {
		if ((n = campos(linha, f, 10)) < 10)
			continue;
		total++;
		if (strcmp(f[1], "ativo") == 0 || strcmp(f[1], "ouvindo") == 0)
			ativos++;
		sob_demanda = atoi(f[8]);
		printf("  ");
		coluna(f[0], 14);
		printf("%s●%s ", cor_estado(f[1]), FIM);
		coluna(f[1], 13);
		if (sob_demanda && atoi(f[9]) > 0)
			snprintf(num, sizeof(num), "%s con.", f[9]);
		else
			snprintf(num, sizeof(num), "%s",
			    strcmp(f[2], "0") == 0 ? "-" : f[2]);
		coluna(num, 9);
		m = strtoll(f[7], NULL, 10);
		if (m >= 0)
			tamanho((uint64_t)m, mem, sizeof(mem));
		else
			strlcpy(mem, "-", sizeof(mem));
		coluna(mem, 11);
		duracao(strtoll(f[3], NULL, 10), dur, sizeof(dur));
		coluna(dur, 11);
		coluna(sob_demanda ? "-" : f[4], 11);
		printf("%s\n", f[5]);
		if (f[6][0] != '\0' && (problema(f[1]) ||
		    strcmp(f[1], "ouvindo") == 0))
			printf("  %s  └ %s%s\n", FRACO, f[6], FIM);
	}
	if (total == 0)
		printf("  (nenhum serviço em %s; exemplos em "
		    "/usr/share/examples/vigia)\n", VIGIA_SERVICOS);
	else
		printf("\n  %d ativo(s) de %d.\n", ativos, total);
	printf("\n");
	return (0);
}

static int
cmd_info(const char *nome)
{
	char pedido[128], *corpo, *linha, *ctx, *f[2];

	snprintf(pedido, sizeof(pedido), "info %s", nome);
	corpo = pedir(pedido);
	printf("\n");
	for (linha = strtok_r(corpo, "\n", &ctx); linha != NULL;
	    linha = strtok_r(NULL, "\n", &ctx)) {
		if (campos(linha, f, 2) < 2)
			continue;
		printf("  %s", AMARELO);
		coluna(f[0], 16);
		printf("%s", FIM);
		if (strcmp(f[0], "estado") == 0)
			printf("%s●%s %s\n", cor_estado(f[1]), FIM, f[1]);
		else
			printf("%s\n", f[1]);
	}
	printf("\n");
	return (0);
}

/* Lê estado, pid e motivo de um serviço. */
static void
ler_estado(const char *nome, char *est, size_t elen, char *pid, size_t plen,
    char *motivo, size_t mlen)
{
	char pedido[128], *corpo, *linha, *ctx, *f[2];

	snprintf(pedido, sizeof(pedido), "info %s", nome);
	corpo = pedir(pedido);
	est[0] = pid[0] = motivo[0] = '\0';
	for (linha = strtok_r(corpo, "\n", &ctx); linha != NULL;
	    linha = strtok_r(NULL, "\n", &ctx)) {
		if (campos(linha, f, 2) < 2)
			continue;
		if (strcmp(f[0], "estado") == 0)
			strlcpy(est, f[1], elen);
		else if (strcmp(f[0], "pid") == 0)
			strlcpy(pid, f[1], plen);
		else if (strcmp(f[0], "motivo") == 0)
			strlcpy(motivo, f[1], mlen);
	}
}

static int
terminal(const char *est, int subindo)
{
	if (strcmp(est, "manutenção") == 0 || strcmp(est, "erro") == 0)
		return (1);
	if (subindo)
		return (strcmp(est, "ativo") == 0 ||
		    strcmp(est, "ouvindo") == 0 ||
		    strcmp(est, "parado") == 0);
	return (strcmp(est, "parado") == 0 || strcmp(est, "desativado") == 0);
}

static int
cmd_acao(const char *verbo, const char *nome)
{
	char pedido[128], est[32], pid[16], motivo[256], *corpo, *extra;
	int subindo, i, espera;

	subindo = strcmp(verbo, "parar") != 0 &&
	    strcmp(verbo, "desativar") != 0;
	snprintf(pedido, sizeof(pedido), "%s %s", verbo, nome);
	corpo = pedir(pedido);
	extra = corpo[0] != '\0' ? strdup(corpo) : NULL;

	/* Acompanha a mudança por até 8 s (subindo) ou 15 s (parando). */
	espera = subindo ? 32 : 60;
	for (i = 0; i < espera; i++) {
		ler_estado(nome, est, sizeof(est), pid, sizeof(pid), motivo,
		    sizeof(motivo));
		if (terminal(est, subindo))
			break;
		usleep(250000);
	}

	printf("  %s: %s●%s %s", nome, cor_estado(est), FIM, est);
	if (pid[0] != '\0')
		printf(" (pid %s)", pid);
	printf("\n");
	if (motivo[0] != '\0' && (problema(est) ||
	    strcmp(est, "ouvindo") == 0))
		printf("  %s  └ %s%s\n", FRACO, motivo, FIM);
	if (extra != NULL)
		printf("  %s", extra);
	if (strcmp(verbo, "ativar") == 0)
		printf("  %s sobe sozinho nos próximos boots.\n", nome);
	else if (strcmp(verbo, "desativar") == 0)
		printf("  %s não sobe mais no boot.\n", nome);
	return (strcmp(est, "manutenção") == 0 || strcmp(est, "erro") == 0);
}

static int
cmd_log(int argc, char **argv)
{
	char caminho[PATH_MAX];
	const char *linhas = "20";
	int ch, seguir = 0;

	if (argc < 2 || !nome_valido(argv[1]))
		uso();
	snprintf(caminho, sizeof(caminho), "%s/%s.log", VIGIA_LOGDIR, argv[1]);
	optind = 2;
	while ((ch = getopt(argc, argv, "fn:")) != -1) {
		switch (ch) {
		case 'f':
			seguir = 1;
			break;
		case 'n':
			linhas = optarg;
			break;
		default:
			uso();
		}
	}
	if (access(caminho, R_OK) == -1) {
		if (errno == ENOENT)
			errx(1, "%s ainda não tem log", argv[1]);
		err(1, "%s (o log é legível pelo root e pelo grupo wheel)",
		    caminho);
	}
	if (seguir)
		execl("/usr/bin/tail", "tail", "-n", linhas, "-F", caminho,
		    (char *)NULL);
	else
		execl("/usr/bin/tail", "tail", "-n", linhas, caminho,
		    (char *)NULL);
	err(1, "tail");
}

int
cliente_main(int argc, char **argv)
{
	const char *term = getenv("TERM");
	char *corpo;

	cor = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL &&
	    (term == NULL || strcmp(term, "dumb") != 0);

	if (argc < 2 || strcmp(argv[1], "estado") == 0)
		return (cmd_estado());
	if (strcmp(argv[1], "ajuda") == 0 || strcmp(argv[1], "-h") == 0)
		uso();
	if (strcmp(argv[1], "recarregar") == 0) {
		corpo = pedir("recarregar");
		printf("  %s", corpo);
		return (0);
	}
	if (strcmp(argv[1], "log") == 0)
		return (cmd_log(argc - 1, argv + 1));
	if (argc != 3)
		uso();
	if (!nome_valido(argv[2]))
		errx(1, "nome de serviço inválido: %s", argv[2]);
	if (strcmp(argv[1], "info") == 0)
		return (cmd_info(argv[2]));
	if (strcmp(argv[1], "iniciar") == 0 || strcmp(argv[1], "parar") == 0 ||
	    strcmp(argv[1], "reiniciar") == 0 ||
	    strcmp(argv[1], "ativar") == 0 ||
	    strcmp(argv[1], "desativar") == 0)
		return (cmd_acao(argv[1], argv[2]));
	uso();
}
