/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * lume-atributo: os atributos estendidos dos arquivos (extattr, espaço
 * "user"), como no BeOS: etiquetas, nota, comentário, origem e qualquer
 * atributo próprio, gravados no próprio arquivo (ZFS e UFS2), e a procura
 * por eles.
 *
 * Os nomes conhecidos são os do KDE (KFileMetaData), para o Dolphin mostrar
 * e editar os mesmos valores:
 *	etiquetas	xdg.tags	(separadas por vírgula)
 *	comentario	xdg.comment
 *	nota		baloo.rating	(0 a 5 estrelas; gravado de 0 a 10)
 *	origem		xdg.origin.url
 * Os outros nomes viram lume.<nome>; um nome com ponto fica como está.
 */

#include <sys/param.h>
#include <sys/extattr.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fts.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define	NS	EXTATTR_NAMESPACE_USER
#define	VALOR_MAX	65536

static const struct {
	const char *curto;
	const char *real;
} nomes[] = {
	{ "etiquetas",	"xdg.tags" },
	{ "comentario",	"xdg.comment" },
	{ "nota",	"baloo.rating" },
	{ "origem",	"xdg.origin.url" },
};

static bool seguir;	/* -L: segue links simbólicos */

static void
uso(void)
{
	fprintf(stderr,
	    "uso: lume atributo ARQUIVO...\n"
	    "     lume atributo ARQUIVO NOME\n"
	    "     lume atributo ARQUIVO NOME=VALOR | NOME+=ETIQUETA | NOME-=ETIQUETA...\n"
	    "     lume atributo procurar [-d PASTA] CONDIÇÃO...\n"
	    "        CONDIÇÃO: NOME | NOME=VALOR | NOME~TEXTO | NOME>=N | NOME<=N\n"
	    "                  | NOME>N | NOME<N  (todas valem juntas)\n"
	    "     nomes: etiquetas, nota (0-5), comentario, origem ou um seu\n");
	exit(2);
}

/* O nome gravado no arquivo para o nome que a pessoa escreveu. */
static const char *
real(const char *nome, char *buf, size_t tam)
{
	for (size_t i = 0; i < nitems(nomes); i++)
		if (strcmp(nome, nomes[i].curto) == 0)
			return (nomes[i].real);
	if (strchr(nome, '.') != NULL)
		return (nome);
	snprintf(buf, tam, "lume.%s", nome);
	return (buf);
}

/* O nome para mostrar a partir do nome gravado. */
static const char *
curto(const char *nome)
{
	for (size_t i = 0; i < nitems(nomes); i++)
		if (strcmp(nome, nomes[i].real) == 0)
			return (nomes[i].curto);
	if (strncmp(nome, "lume.", 5) == 0)
		return (nome + 5);
	return (nome);
}

static bool
nome_valido(const char *n)
{
	if (*n == '\0' || strlen(n) > EXTATTR_MAXNAMELEN)
		return (false);
	for (; *n != '\0'; n++)
		if (!isalnum((unsigned char)*n) && strchr("._-", *n) == NULL)
			return (false);
	return (true);
}

static ssize_t
ler(const char *arq, const char *nome, char *buf, size_t tam)
{
	ssize_t n;

	n = seguir ? extattr_get_file(arq, NS, nome, buf, tam - 1) :
	    extattr_get_link(arq, NS, nome, buf, tam - 1);
	if (n >= 0)
		buf[n] = '\0';
	return (n);
}

static int
gravar(const char *arq, const char *nome, const char *valor)
{
	ssize_t n;

	n = seguir ? extattr_set_file(arq, NS, nome, valor, strlen(valor)) :
	    extattr_set_link(arq, NS, nome, valor, strlen(valor));
	return (n < 0 ? -1 : 0);
}

static int
apagar(const char *arq, const char *nome)
{
	int r;

	r = seguir ? extattr_delete_file(arq, NS, nome) :
	    extattr_delete_link(arq, NS, nome);
	if (r < 0 && errno == ENOATTR)
		return (0);
	return (r);
}

/*
 * Os nomes dos atributos do arquivo: a lista do kernel vem como
 * [tamanho][nome] sem terminador. Devolve quantos, ou -1.
 */
static int
listar_nomes(const char *arq, char nomes_out[][EXTATTR_MAXNAMELEN + 1],
    int max)
{
	char lista[8192];
	ssize_t n;
	int k = 0;

	n = seguir ? extattr_list_file(arq, NS, lista, sizeof(lista)) :
	    extattr_list_link(arq, NS, lista, sizeof(lista));
	if (n < 0)
		return (-1);
	for (ssize_t i = 0; i < n && k < max;) {
		size_t t = (unsigned char)lista[i];

		if (i + 1 + (ssize_t)t > n)
			break;
		memcpy(nomes_out[k], lista + i + 1, t);
		nomes_out[k][t] = '\0';
		k++;
		i += 1 + t;
	}
	return (k);
}

/* A nota gravada (0 a 10) mostrada em estrelas (0 a 5, meia estrela). */
static void
mostrar_valor(const char *nome, const char *valor)
{
	if (strcmp(nome, "baloo.rating") == 0) {
		int v = atoi(valor);

		printf("%d", v / 2);
		if (v % 2)
			printf(",5");
		return;
	}
	for (const char *p = valor; *p != '\0'; p++)
		putchar(isprint((unsigned char)*p) || (*p & 0x80) ? *p : '?');
}

static int
mostrar(const char *arq, bool com_nome)
{
	char lista[256][EXTATTR_MAXNAMELEN + 1];
	char *valor;
	int k;

	k = listar_nomes(arq, lista, nitems(lista));
	if (k < 0) {
		warn("%s", arq);
		return (1);
	}
	if (com_nome)
		printf("%s:\n", arq);
	if ((valor = malloc(VALOR_MAX)) == NULL)
		err(1, NULL);
	for (int i = 0; i < k; i++) {
		if (ler(arq, lista[i], valor, VALOR_MAX) < 0)
			continue;
		printf("%s%s\t", com_nome ? "  " : "", curto(lista[i]));
		mostrar_valor(lista[i], valor);
		putchar('\n');
	}
	free(valor);
	return (0);
}

/* A etiqueta está na lista separada por vírgulas? */
static bool
tem_etiqueta(const char *lista, const char *et)
{
	size_t t = strlen(et);

	for (const char *p = lista; *p != '\0';) {
		const char *fim = strchr(p, ',');
		size_t n = fim ? (size_t)(fim - p) : strlen(p);

		while (n > 0 && isspace((unsigned char)*p)) {
			p++;
			n--;
		}
		while (n > 0 && isspace((unsigned char)p[n - 1]))
			n--;
		if (n == t && strncasecmp(p, et, t) == 0)
			return (true);
		if (fim == NULL)
			break;
		p = fim + 1;
	}
	return (false);
}

static void
tirar_etiqueta(char *lista, const char *et)
{
	char nova[VALOR_MAX] = "";
	char *copia, *p, *tok;

	if ((copia = strdup(lista)) == NULL)
		err(1, NULL);
	p = copia;
	while ((tok = strsep(&p, ",")) != NULL) {
		while (isspace((unsigned char)*tok))
			tok++;
		if (*tok == '\0' || strcasecmp(tok, et) == 0)
			continue;
		if (nova[0] != '\0')
			strlcat(nova, ",", sizeof(nova));
		strlcat(nova, tok, sizeof(nova));
	}
	free(copia);
	strlcpy(lista, nova, VALOR_MAX);
}

/* NOME=VALOR, NOME+=ETIQUETA ou NOME-=ETIQUETA num arquivo. */
static int
alterar(const char *arq, const char *expr)
{
	char nome[EXTATTR_MAXNAMELEN + 1], buf[EXTATTR_MAXNAMELEN + 8];
	const char *igual, *rnome, *valor;
	char op = '=';
	size_t t;
	char *atual;
	int r;

	igual = strchr(expr, '=');
	t = igual - expr;
	if (t > 0 && (expr[t - 1] == '+' || expr[t - 1] == '-'))
		op = expr[--t];
	if (t == 0 || t > EXTATTR_MAXNAMELEN)
		errx(2, "nome inválido: %s", expr);
	memcpy(nome, expr, t);
	nome[t] = '\0';
	if (!nome_valido(nome))
		errx(2, "nome inválido: %s (letras, números, . _ -)", nome);
	rnome = real(nome, buf, sizeof(buf));
	valor = igual + 1;

	if (op == '=' && *valor == '\0')
		return (apagar(arq, rnome) < 0 ? (warn("%s", arq), 1) : 0);

	if (strcmp(rnome, "baloo.rating") == 0 && op == '=') {
		char *fim;
		double estrelas = strtod(valor, &fim);
		char n[8];

		if (*fim == ',')
			estrelas += strtod(fim + 1, &fim) / 10.0;
		if (*fim != '\0' || estrelas < 0 || estrelas > 5)
			errx(2, "nota de 0 a 5: %s", valor);
		snprintf(n, sizeof(n), "%d", (int)(estrelas * 2 + 0.5));
		return (gravar(arq, rnome, n) < 0 ? (warn("%s", arq), 1) : 0);
	}
	if (op == '=')
		return (gravar(arq, rnome, valor) < 0 ? (warn("%s", arq), 1) : 0);

	/* + e - mexem numa lista separada por vírgulas (as etiquetas). */
	if (strchr(valor, ',') != NULL || *valor == '\0')
		errx(2, "uma etiqueta por vez: %s", valor);
	if ((atual = calloc(1, VALOR_MAX)) == NULL)
		err(1, NULL);
	if (ler(arq, rnome, atual, VALOR_MAX) < 0)
		atual[0] = '\0';
	if (op == '+' && !tem_etiqueta(atual, valor)) {
		if (strlen(atual) + strlen(valor) + 2 > VALOR_MAX)
			errx(1, "%s: lista grande demais", arq);
		if (atual[0] != '\0')
			strlcat(atual, ",", VALOR_MAX);
		strlcat(atual, valor, VALOR_MAX);
	} else if (op == '-')
		tirar_etiqueta(atual, valor);
	if (atual[0] == '\0')
		r = apagar(arq, rnome);
	else
		r = gravar(arq, rnome, atual);
	free(atual);
	return (r < 0 ? (warn("%s", arq), 1) : 0);
}

/* As condições da procura. */
enum op { EXISTE, IGUAL, CONTEM, MAIOR_IG, MENOR_IG, MAIOR, MENOR };

struct cond {
	char nome[EXTATTR_MAXNAMELEN + 8];
	enum op op;
	const char *valor;
	double num;
};

static void
ler_cond(const char *s, struct cond *c)
{
	static const struct {
		const char *txt;
		enum op op;
	} ops[] = {
		{ ">=", MAIOR_IG }, { "<=", MENOR_IG }, { "=", IGUAL },
		{ "~", CONTEM }, { ">", MAIOR }, { "<", MENOR },
	};
	char nome[EXTATTR_MAXNAMELEN + 1], buf[EXTATTR_MAXNAMELEN + 8];
	size_t t = strcspn(s, "=~<>");

	if (t == 0 || t > EXTATTR_MAXNAMELEN)
		errx(2, "condição inválida: %s", s);
	memcpy(nome, s, t);
	nome[t] = '\0';
	if (!nome_valido(nome))
		errx(2, "nome inválido: %s", nome);
	strlcpy(c->nome, real(nome, buf, sizeof(buf)), sizeof(c->nome));
	c->op = EXISTE;
	c->valor = "";
	if (s[t] == '\0')
		return;
	for (size_t i = 0; i < nitems(ops); i++) {
		size_t n = strlen(ops[i].txt);

		if (strncmp(s + t, ops[i].txt, n) == 0) {
			c->op = ops[i].op;
			c->valor = s + t + n;
			break;
		}
	}
	if (c->op >= MAIOR_IG) {
		char *fim;

		c->num = strtod(c->valor, &fim);
		if (fim == c->valor || *fim != '\0')
			errx(2, "número esperado: %s", s);
		/* A nota se escreve em estrelas e se grava de 0 a 10. */
		if (strcmp(c->nome, "baloo.rating") == 0)
			c->num *= 2;
	}
}

static bool
contem(const char *v, const char *t)
{
	size_t n = strlen(t);

	for (; *v != '\0'; v++)
		if (strncasecmp(v, t, n) == 0)
			return (true);
	return (n == 0);
}

static bool
cumpre(const char *arq, const struct cond *c, int nc, char *valor)
{
	for (int i = 0; i < nc; i++) {
		double n;
		char *fim;

		if (ler(arq, c[i].nome, valor, VALOR_MAX) < 0)
			return (false);
		switch (c[i].op) {
		case EXISTE:
			break;
		case IGUAL:
			if (strcmp(c[i].nome, "xdg.tags") == 0) {
				if (!tem_etiqueta(valor, c[i].valor))
					return (false);
			} else if (strcmp(c[i].nome, "baloo.rating") == 0) {
				if (atof(valor) != atof(c[i].valor) * 2)
					return (false);
			} else if (strcasecmp(valor, c[i].valor) != 0)
				return (false);
			break;
		case CONTEM:
			if (!contem(valor, c[i].valor))
				return (false);
			break;
		default:
			n = strtod(valor, &fim);
			if (fim == valor)
				return (false);
			if ((c[i].op == MAIOR_IG && !(n >= c[i].num)) ||
			    (c[i].op == MENOR_IG && !(n <= c[i].num)) ||
			    (c[i].op == MAIOR && !(n > c[i].num)) ||
			    (c[i].op == MENOR && !(n < c[i].num)))
				return (false);
		}
	}
	return (true);
}

static int
procurar(int argc, char *argv[])
{
	char *pastas[2] = { NULL, NULL };
	struct cond *c;
	char *valor;
	FTS *fts;
	FTSENT *e;
	int ch, achados = 0;

	optind = 1;
	while ((ch = getopt(argc, argv, "d:")) != -1) {
		switch (ch) {
		case 'd':
			pastas[0] = optarg;
			break;
		default:
			uso();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 0)
		uso();
	if (pastas[0] == NULL && (pastas[0] = getenv("HOME")) == NULL)
		pastas[0] = __DECONST(char *, ".");
	if ((c = calloc(argc, sizeof(*c))) == NULL ||
	    (valor = malloc(VALOR_MAX)) == NULL)
		err(1, NULL);
	for (int i = 0; i < argc; i++)
		ler_cond(argv[i], &c[i]);

	fts = fts_open(pastas, FTS_PHYSICAL | FTS_NOCHDIR | FTS_XDEV, NULL);
	if (fts == NULL)
		err(1, "%s", pastas[0]);
	while ((e = fts_read(fts)) != NULL) {
		switch (e->fts_info) {
		case FTS_D:
			/* Pastas ocultas (.cache, .local, .zfs) ficam de fora. */
			if (e->fts_level > 0 && e->fts_name[0] == '.')
				fts_set(fts, e, FTS_SKIP);
			else if (e->fts_level > 0 &&
			    cumpre(e->fts_accpath, c, argc, valor)) {
				puts(e->fts_path);
				achados++;
			}
			break;
		case FTS_F:
			if (e->fts_name[0] != '.' &&
			    cumpre(e->fts_accpath, c, argc, valor)) {
				puts(e->fts_path);
				achados++;
			}
			break;
		default:
			break;
		}
	}
	fts_close(fts);
	free(valor);
	free(c);
	return (achados > 0 ? 0 : 1);
}

int
main(int argc, char *argv[])
{
	int ch, r = 0, arqs;

	if (argc > 1 && strcmp(argv[1], "procurar") == 0)
		return (procurar(argc - 1, argv + 1));

	while ((ch = getopt(argc, argv, "L")) != -1) {
		switch (ch) {
		case 'L':
			seguir = true;
			break;
		default:
			uso();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 0)
		uso();

	/* Os arquivos vêm antes; o resto são nomes e alterações. */
	for (arqs = 0; arqs < argc; arqs++) {
		struct stat st;

		if (lstat(argv[arqs], &st) < 0)
			break;
	}
	if (arqs == 0)
		err(1, "%s", argv[0]);

	if (arqs == argc) {
		for (int i = 0; i < arqs; i++)
			r |= mostrar(argv[i], arqs > 1);
		return (r);
	}
	/* Um nome só, sem =: mostra o valor. */
	if (argc - arqs == 1 && strchr(argv[arqs], '=') == NULL) {
		char buf[EXTATTR_MAXNAMELEN + 8], *valor;
		const char *rnome;

		if (!nome_valido(argv[arqs]))
			errx(2, "nome inválido: %s", argv[arqs]);
		rnome = real(argv[arqs], buf, sizeof(buf));
		if ((valor = malloc(VALOR_MAX)) == NULL)
			err(1, NULL);
		for (int i = 0; i < arqs; i++) {
			if (ler(argv[i], rnome, valor, VALOR_MAX) < 0) {
				r = 1;
				continue;
			}
			if (arqs > 1)
				printf("%s\t", argv[i]);
			mostrar_valor(rnome, valor);
			putchar('\n');
		}
		free(valor);
		return (r);
	}
	for (int j = arqs; j < argc; j++)
		if (strchr(argv[j], '=') == NULL)
			errx(2, "%s: falta o arquivo ou o =", argv[j]);
	for (int i = 0; i < arqs; i++)
		for (int j = arqs; j < argc; j++)
			r |= alterar(argv[i], argv[j]);
	return (r);
}
