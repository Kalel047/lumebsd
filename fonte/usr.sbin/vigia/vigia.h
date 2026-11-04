/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * vigia - gerenciador de serviços do LumeBSD.
 */

#ifndef VIGIA_H
#define	VIGIA_H

#include <sys/types.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>

#define	VIGIA_SOCKET	"/var/run/vigia.sock"
#define	VIGIA_PIDFILE	"/var/run/vigiad.pid"
#define	VIGIA_SERVICOS	"/etc/vigia/servicos"
#define	VIGIA_LOGDIR	"/var/log/vigia"
#define	VIGIA_DBDIR	"/var/db/vigia"

#define	NOME_MAX	64
#define	MAX_DEPS	8
#define	MAX_AMBIENTE	16
#define	MAX_CONEXOES	256	/* teto de max_conexoes */
#define	CLASSE_NOME_MAX	30	/* nome cabe na classe de login "v-<nome>" */

enum escuta {
	ESCUTA_NENHUMA,
	ESCUTA_TCP,
	ESCUTA_UNIX,
};

enum reiniciar {
	R_SEMPRE,
	R_FALHA,
	R_NUNCA,
};

enum estado {
	E_PARADO,
	E_DESATIVADO,
	E_AGUARDANDO,
	E_INICIANDO,
	E_ATIVO,
	E_PARANDO,
	E_REINICIANDO,
	E_MANUTENCAO,
	E_ERRO,
	E_OUVINDO,		/* socket aberto, sobe a cada conexão */
};

/* Configuração lida de /etc/vigia/servicos/<nome>.conf */
struct manifesto {
	char	nome[NOME_MAX];
	char	descricao[256];
	char	comando[1024];
	char	usuario[NOME_MAX];
	char	diretorio[PATH_MAX];
	enum reiniciar reiniciar;
	char	depende[MAX_DEPS][NOME_MAX];
	int	ndepende;
	char	ambiente[MAX_AMBIENTE][256];
	int	nambiente;
	int	ativo;			/* "ativo = sim" no arquivo */
	uint64_t limite_memoria;	/* bytes; 0 = sem limite */
	int	limite_cpu;		/* % de um núcleo; 0 = sem limite */
	int	limite_processos;	/* 0 = sem limite */
	enum escuta escuta;
	char	escuta_endereco[64];	/* IPv4; vazio = todas */
	int	escuta_porta;
	char	escuta_caminho[104];
	int	max_conexoes;
	char	erro[256];		/* vazio se o arquivo é válido */
};

int	 nome_valido(const char *);
int	 manifesto_ler(const char *caminho, const char *nome,
	    struct manifesto *);
const char *reiniciar_texto(enum reiniciar);
void	 duracao(time_t, char *, size_t);
void	 tamanho(uint64_t, char *, size_t);
void	 escuta_texto(const struct manifesto *, char *, size_t);

int	 servidor_main(int, char **);
int	 cliente_main(int, char **);

#endif
