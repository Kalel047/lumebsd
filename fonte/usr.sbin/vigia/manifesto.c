/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Leitura dos arquivos de serviço do vigia. Formato: linhas "chave = valor",
 * comentários com '#'. Veja vigia.conf(5).
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigia.h"

int
nome_valido(const char *s)
{
	size_t i, n = strlen(s);

	if (n == 0 || n >= NOME_MAX)
		return (0);
	for (i = 0; i < n; i++) {
		if (!(islower((unsigned char)s[i]) ||
		    isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '_'))
			return (0);
	}
	return (1);
}

const char *
reiniciar_texto(enum reiniciar r)
{
	switch (r) {
	case R_SEMPRE:
		return ("sempre");
	case R_FALHA:
		return ("falha");
	case R_NUNCA:
		return ("nunca");
	}
	return ("?");
}

void
duracao(time_t t, char *buf, size_t len)
{
	if (t < 0)
		t = 0;
	if (t < 60)
		snprintf(buf, len, "%d s", (int)t);
	else if (t < 3600)
		snprintf(buf, len, "%d min", (int)(t / 60));
	else if (t < 86400)
		snprintf(buf, len, "%d h %d min", (int)(t / 3600),
		    (int)(t % 3600 / 60));
	else
		snprintf(buf, len, "%d d %d h", (int)(t / 86400),
		    (int)(t % 86400 / 3600));
}

void
tamanho(uint64_t b, char *buf, size_t len)
{
	if (b >= 1024ULL * 1024 * 1024)
		snprintf(buf, len, "%.1f GiB", (double)b / (1024.0 * 1024 * 1024));
	else if (b >= 1024 * 1024)
		snprintf(buf, len, "%.1f MiB", (double)b / (1024.0 * 1024));
	else
		snprintf(buf, len, "%ju KiB", (uintmax_t)(b / 1024));
}

void
escuta_texto(const struct manifesto *m, char *buf, size_t len)
{
	if (m->escuta == ESCUTA_TCP)
		snprintf(buf, len, "tcp %s:%d", m->escuta_endereco[0] ?
		    m->escuta_endereco : "0.0.0.0", m->escuta_porta);
	else if (m->escuta == ESCUTA_UNIX)
		snprintf(buf, len, "unix %s", m->escuta_caminho);
	else
		snprintf(buf, len, "-");
}

/* "256M", "1G", "512K" ou bytes. */
static int
ler_tamanho(const char *v, uint64_t *out)
{
	char *fim;
	unsigned long long n;

	errno = 0;
	n = strtoull(v, &fim, 10);
	if (errno != 0 || fim == v)
		return (-1);
	switch (*fim) {
	case 'K': case 'k':
		n *= 1024ULL; fim++;
		break;
	case 'M': case 'm':
		n *= 1024ULL * 1024; fim++;
		break;
	case 'G': case 'g':
		n *= 1024ULL * 1024 * 1024; fim++;
		break;
	}
	if (*fim == 'B' || *fim == 'b')
		fim++;
	if (*fim != '\0')
		return (-1);
	*out = n;
	return (0);
}

static int
ler_inteiro(const char *v, int min, int max, int *out)
{
	char *fim;
	long n;

	errno = 0;
	n = strtol(v, &fim, 10);
	if (*fim == '%')
		fim++;
	if (errno != 0 || fim == v || *fim != '\0' || n < min || n > max)
		return (-1);
	*out = (int)n;
	return (0);
}

static int
ler_escuta(const char *v, struct manifesto *m)
{
	struct in_addr a;
	const char *p, *dp;
	int porta;

	if (strncmp(v, "unix:", 5) == 0) {
		if (v[5] != '/' || strlcpy(m->escuta_caminho, v + 5,
		    sizeof(m->escuta_caminho)) >= sizeof(m->escuta_caminho))
			return (-1);
		m->escuta = ESCUTA_UNIX;
		return (0);
	}
	if (strncmp(v, "tcp:", 4) != 0)
		return (-1);
	p = v + 4;
	if ((dp = strrchr(p, ':')) != NULL) {
		if ((size_t)(dp - p) >= sizeof(m->escuta_endereco))
			return (-1);
		memcpy(m->escuta_endereco, p, (size_t)(dp - p));
		m->escuta_endereco[dp - p] = '\0';
		if (inet_pton(AF_INET, m->escuta_endereco, &a) != 1)
			return (-1);
		p = dp + 1;
	}
	if (ler_inteiro(p, 1, 65535, &porta) == -1 || strchr(p, '%'))
		return (-1);
	m->escuta_porta = porta;
	m->escuta = ESCUTA_TCP;
	return (0);
}

static char *
aparar(char *s)
{
	char *e;

	while (isspace((unsigned char)*s))
		s++;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		*--e = '\0';
	return (s);
}

static int
copiar(char *dst, size_t len, const char *src)
{
	return (strlcpy(dst, src, len) < len);
}

static void
erro(struct manifesto *m, int linha, const char *msg, const char *arg)
{
	if (m->erro[0] != '\0')
		return;
	if (linha > 0)
		snprintf(m->erro, sizeof(m->erro), "linha %d: %s%s%s", linha,
		    msg, arg ? " " : "", arg ? arg : "");
	else
		snprintf(m->erro, sizeof(m->erro), "%s%s%s", msg,
		    arg ? " " : "", arg ? arg : "");
}

int
manifesto_ler(const char *caminho, const char *nome, struct manifesto *m)
{
	FILE *f;
	char buf[2048], *linha, *chave, *valor, *igual, *tok, *ctx;
	int n = 0;

	memset(m, 0, sizeof(*m));
	m->reiniciar = R_FALHA;
	m->ativo = 1;
	m->max_conexoes = 16;
	if (!copiar(m->nome, sizeof(m->nome), nome) || !nome_valido(nome)) {
		erro(m, 0, "nome de serviço inválido (use a-z, 0-9, - e _):",
		    nome);
		return (-1);
	}

	if ((f = fopen(caminho, "r")) == NULL) {
		erro(m, 0, "não foi possível abrir:", strerror(errno));
		return (-1);
	}
	while (fgets(buf, sizeof(buf), f) != NULL) {
		n++;
		if (strchr(buf, '\n') == NULL && !feof(f)) {
			erro(m, n, "linha longa demais", NULL);
			break;
		}
		linha = aparar(buf);
		if (*linha == '\0' || *linha == '#')
			continue;
		if ((igual = strchr(linha, '=')) == NULL) {
			erro(m, n, "esperava 'chave = valor'", NULL);
			continue;
		}
		*igual = '\0';
		chave = aparar(linha);
		valor = aparar(igual + 1);

		if (strcmp(chave, "descricao") == 0) {
			if (!copiar(m->descricao, sizeof(m->descricao), valor))
				erro(m, n, "descrição longa demais", NULL);
		} else if (strcmp(chave, "comando") == 0) {
			if (!copiar(m->comando, sizeof(m->comando), valor))
				erro(m, n, "comando longo demais", NULL);
		} else if (strcmp(chave, "usuario") == 0) {
			if (!copiar(m->usuario, sizeof(m->usuario), valor))
				erro(m, n, "usuário longo demais", NULL);
		} else if (strcmp(chave, "diretorio") == 0) {
			if (valor[0] != '/')
				erro(m, n, "diretório precisa ser absoluto:",
				    valor);
			else if (!copiar(m->diretorio, sizeof(m->diretorio),
			    valor))
				erro(m, n, "diretório longo demais", NULL);
		} else if (strcmp(chave, "reiniciar") == 0) {
			if (strcmp(valor, "sempre") == 0)
				m->reiniciar = R_SEMPRE;
			else if (strcmp(valor, "falha") == 0)
				m->reiniciar = R_FALHA;
			else if (strcmp(valor, "nunca") == 0)
				m->reiniciar = R_NUNCA;
			else
				erro(m, n, "reiniciar deve ser sempre, falha "
				    "ou nunca, não", valor);
		} else if (strcmp(chave, "ativo") == 0) {
			if (strcmp(valor, "sim") == 0)
				m->ativo = 1;
			else if (strcmp(valor, "nao") == 0 ||
			    strcmp(valor, "não") == 0)
				m->ativo = 0;
			else
				erro(m, n, "ativo deve ser sim ou nao, não",
				    valor);
		} else if (strcmp(chave, "depende") == 0) {
			for (tok = strtok_r(valor, " \t,", &ctx); tok != NULL;
			    tok = strtok_r(NULL, " \t,", &ctx)) {
				if (!nome_valido(tok)) {
					erro(m, n, "dependência inválida:", tok);
					break;
				}
				if (strcmp(tok, m->nome) == 0) {
					erro(m, n, "o serviço não pode depender "
					    "de si mesmo", NULL);
					break;
				}
				if (m->ndepende == MAX_DEPS) {
					erro(m, n, "dependências demais", NULL);
					break;
				}
				copiar(m->depende[m->ndepende++], NOME_MAX, tok);
			}
		} else if (strcmp(chave, "ambiente") == 0) {
			if (strchr(valor, '=') == NULL || valor[0] == '=')
				erro(m, n, "ambiente deve ser NOME=valor:",
				    valor);
			else if (m->nambiente == MAX_AMBIENTE)
				erro(m, n, "variáveis de ambiente demais", NULL);
			else if (!copiar(m->ambiente[m->nambiente++], 256,
			    valor))
				erro(m, n, "variável longa demais", NULL);
		} else if (strcmp(chave, "limite_memoria") == 0) {
			if (ler_tamanho(valor, &m->limite_memoria) == -1 ||
			    m->limite_memoria < 1024 * 1024)
				erro(m, n, "limite_memoria deve ser algo como "
				    "64M ou 1G (mínimo 1M), não", valor);
		} else if (strcmp(chave, "limite_cpu") == 0) {
			if (ler_inteiro(valor, 1, 25600, &m->limite_cpu) == -1)
				erro(m, n, "limite_cpu é a porcentagem de um "
				    "núcleo (ex.: 50%), não", valor);
		} else if (strcmp(chave, "limite_processos") == 0) {
			if (ler_inteiro(valor, 1, 100000,
			    &m->limite_processos) == -1 || strchr(valor, '%'))
				erro(m, n, "limite_processos deve ser um número "
				    "positivo, não", valor);
		} else if (strcmp(chave, "escutar") == 0) {
			if (ler_escuta(valor, m) == -1)
				erro(m, n, "escutar deve ser tcp:PORTA, "
				    "tcp:IP:PORTA ou unix:/caminho, não", valor);
		} else if (strcmp(chave, "max_conexoes") == 0) {
			if (ler_inteiro(valor, 1, MAX_CONEXOES,
			    &m->max_conexoes) == -1 || strchr(valor, '%'))
				erro(m, n, "max_conexoes deve ir de 1 a 256, não",
				    valor);
		} else {
			erro(m, n, "chave desconhecida:", chave);
		}
	}
	fclose(f);

	if (m->comando[0] == '\0')
		erro(m, 0, "falta a chave 'comando'", NULL);
	if ((m->limite_memoria || m->limite_cpu || m->limite_processos) &&
	    strlen(m->nome) > CLASSE_NOME_MAX)
		erro(m, 0, "para usar limites, o nome do serviço precisa ter "
		    "até 30 caracteres", NULL);
	return (m->erro[0] == '\0' ? 0 : -1);
}
