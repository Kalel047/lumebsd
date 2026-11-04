/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * vigiad - o daemon do gerenciador de serviços do LumeBSD.
 *
 * Um único laço kqueue(2) cuida de tudo: pedidos do vigia(8) pelo socket
 * Unix, sockets de escuta dos serviços sob demanda, SIGCHLD (processos que
 * terminaram), SIGTERM/SIGINT (desligar), SIGHUP (recarregar) e um
 * temporizador de 500 ms que leva cada serviço do estado atual ao estado
 * desejado (reconciliação).
 *
 * Cada serviço roda numa classe de login própria ("v-<nome>"), herdada por
 * todos os processos que ele criar. Pela classe o vigia mede memória, CPU e
 * processos do serviço inteiro, encerra processos que sobraram quando o
 * principal termina, e o RCTL aplica os limites do arquivo ao total.
 *
 * Estados: parado, desativado, aguardando (dependências), iniciando,
 * ativo, ouvindo (sob demanda: um processo por conexão), parando,
 * reiniciando (espera crescente), manutencao (caiu MAX_FALHAS vezes
 * seguidas, ou não pôde iniciar) e erro (arquivo inválido).
 */

#include <sys/param.h>
#include <sys/event.h>
#include <sys/rctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "vigia.h"

#define	TICK_MS		500
#define	TEMPO_ATIVO	1	/* segundos vivo para ser considerado ativo */
#define	TEMPO_ESTAVEL	10	/* segundos rodando para zerar as falhas */
#define	MAX_FALHAS	5	/* falhas seguidas até a manutenção */
#define	ESPERA_MAX	30	/* teto da espera entre tentativas */
#define	PRAZO_PARADA	10	/* segundos entre SIGTERM e SIGKILL */

struct servico {
	struct manifesto m;
	int	 valido;
	int	 habilitado;	/* sobe no boot */
	int	 removido;	/* arquivo apagado; sai da lista ao parar */
	enum estado est;
	pid_t	 pid;		/* processo principal (modo normal) */
	time_t	 desde;		/* entrada no estado atual (monotônico) */
	time_t	 inicio;	/* início do processo atual */
	int	 reinicios;	/* reinícios automáticos */
	int	 falhas;	/* falhas seguidas */
	time_t	 proxima;	/* próxima tentativa (reiniciando) */
	time_t	 prazo;		/* SIGKILL se não parar até aqui */
	int	 quer_rodar;	/* estado desejado */
	int	 reiniciar_depois;
	char	 motivo[256];
	/* Modo sob demanda */
	int	 lfd;		/* socket de escuta ou -1 */
	char	 lcaminho[104];	/* socket unix aberto (para apagar) */
	int	 pausado;	/* escuta suspensa: conexões no máximo */
	pid_t	 conexoes[MAX_CONEXOES];
	int	 nconexoes;
	unsigned long atendidas;
	/* Uso medido pela classe de login (medir()) */
	uint64_t uso_mem;
	int	 uso_cpu;
	int	 uso_procs;
	struct servico *prox;
};

static struct servico *servicos;
static int desligando;
static time_t desligar_ate;
static int kq = -1;

static const char *
estado_texto(enum estado e)
{
	switch (e) {
	case E_PARADO:		return ("parado");
	case E_DESATIVADO:	return ("desativado");
	case E_AGUARDANDO:	return ("aguardando");
	case E_INICIANDO:	return ("iniciando");
	case E_ATIVO:		return ("ativo");
	case E_PARANDO:		return ("parando");
	case E_REINICIANDO:	return ("reiniciando");
	case E_MANUTENCAO:	return ("manutenção");
	case E_ERRO:		return ("erro");
	case E_OUVINDO:		return ("ouvindo");
	}
	return ("?");
}

static time_t
agora(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec);
}

static struct servico *
achar(const char *nome)
{
	struct servico *s;

	for (s = servicos; s != NULL; s = s->prox)
		if (strcmp(s->m.nome, nome) == 0)
			return (s);
	return (NULL);
}

static int
tem_processos(const struct servico *s)
{
	return (s->pid > 0 || s->nconexoes > 0);
}

/* Ativo para quem depende dele: rodando ou pronto para atender. */
static int
de_pe(const struct servico *s)
{
	return (s->est == E_ATIVO || s->est == E_OUVINDO);
}

/* Anota um evento no log do próprio serviço. */
static void
registrar(const struct servico *s, const char *fmt, ...)
{
	char caminho[PATH_MAX], quando[32], msg[512];
	time_t t = time(NULL);
	va_list ap;
	int fd;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	strftime(quando, sizeof(quando), "%Y-%m-%d %H:%M:%S", localtime(&t));
	snprintf(caminho, sizeof(caminho), "%s/%s.log", VIGIA_LOGDIR,
	    s->m.nome);
	fd = open(caminho, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0640);
	if (fd == -1)
		return;
	dprintf(fd, "[vigia %s] %s\n", quando, msg);
	close(fd);
}

static void
mudar(struct servico *s, enum estado e, const char *fmt, ...)
{
	va_list ap;

	if (fmt != NULL) {
		va_start(ap, fmt);
		vsnprintf(s->motivo, sizeof(s->motivo), fmt, ap);
		va_end(ap);
	} else {
		s->motivo[0] = '\0';
	}
	if (s->est == e && fmt == NULL)
		return;
	s->est = e;
	s->desde = agora();
	if (s->motivo[0] != '\0') {
		syslog(e == E_MANUTENCAO || e == E_ERRO ? LOG_WARNING :
		    LOG_INFO, "%s: %s (%s)", s->m.nome, estado_texto(e),
		    s->motivo);
		registrar(s, "%s: %s", estado_texto(e), s->motivo);
	} else {
		registrar(s, "%s", estado_texto(e));
	}
}

/* --- classe de login, contabilidade e limites (RACCT/RCTL) ------------ */

static int
classe(const struct servico *s, char *buf, size_t len)
{
	if (strlen(s->m.nome) > CLASSE_NOME_MAX)
		return (-1);
	snprintf(buf, len, "v-%s", s->m.nome);
	return (0);
}

static int
regra(const char *texto, int adicionar)
{
	if (adicionar)
		return (rctl_add_rule(texto, strlen(texto) + 1, NULL, 0));
	return (rctl_remove_rule(texto, strlen(texto) + 1, NULL, 0));
}

/*
 * Troca as regras da classe do serviço pelas do arquivo atual. Devolve 0 ou
 * um errno (ENOSYS: o kernel está com o RACCT desligado).
 */
static int
aplicar_limites(const struct servico *s)
{
	char cls[MAXLOGNAME], r[160];

	if (classe(s, cls, sizeof(cls)) == -1)
		return (0);
	snprintf(r, sizeof(r), "loginclass:%s", cls);
	(void)regra(r, 0);
	if (s->m.limite_memoria > 0) {
		snprintf(r, sizeof(r), "loginclass:%s:memoryuse:deny=%ju", cls,
		    (uintmax_t)s->m.limite_memoria);
		if (regra(r, 1) == -1)
			return (errno);
	}
	if (s->m.limite_cpu > 0) {
		snprintf(r, sizeof(r), "loginclass:%s:pcpu:deny=%d", cls,
		    s->m.limite_cpu);
		if (regra(r, 1) == -1)
			return (errno);
	}
	if (s->m.limite_processos > 0) {
		snprintf(r, sizeof(r), "loginclass:%s:maxproc:deny=%d", cls,
		    s->m.limite_processos);
		if (regra(r, 1) == -1)
			return (errno);
	}
	return (0);
}

static void
remover_limites(const struct servico *s)
{
	char cls[MAXLOGNAME], r[64];

	if (classe(s, cls, sizeof(cls)) == 0) {
		snprintf(r, sizeof(r), "loginclass:%s", cls);
		(void)regra(r, 0);
	}
}

static int
tem_limites(const struct servico *s)
{
	return (s->m.limite_memoria > 0 || s->m.limite_cpu > 0 ||
	    s->m.limite_processos > 0);
}

/* Tabela de processos do sistema; o chamador libera. */
static struct kinfo_proc *
processos(size_t *n)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
	struct kinfo_proc *kp = NULL;
	size_t len;
	int i;

	for (i = 0; i < 5; i++) {
		if (sysctl(mib, 4, NULL, &len, NULL, 0) == -1)
			break;
		len += len / 4;
		free(kp);
		if ((kp = malloc(len)) == NULL)
			break;
		if (sysctl(mib, 4, kp, &len, NULL, 0) == 0) {
			*n = len / sizeof(*kp);
			return (kp);
		}
		if (errno != ENOMEM)
			break;
	}
	free(kp);
	*n = 0;
	return (NULL);
}

static struct servico *
dono(const struct kinfo_proc *kp)
{
	if (strncmp(kp->ki_loginclass, "v-", 2) != 0)
		return (NULL);
	return (achar(kp->ki_loginclass + 2));
}

/* Atualiza uso_mem, uso_cpu e uso_procs de todos os serviços. */
static void
medir(void)
{
	struct kinfo_proc *kp;
	struct servico *s;
	size_t n, i;
	long pagina = sysconf(_SC_PAGESIZE);

	for (s = servicos; s != NULL; s = s->prox) {
		s->uso_mem = 0;
		s->uso_cpu = s->uso_procs = 0;
	}
	if ((kp = processos(&n)) == NULL)
		return;
	for (i = 0; i < n; i++) {
		if ((s = dono(&kp[i])) == NULL)
			continue;
		s->uso_procs++;
		s->uso_mem += (uint64_t)kp[i].ki_rssize * (uint64_t)pagina;
		s->uso_cpu += (int)(100.0 * kp[i].ki_pctcpu / FSCALE + 0.5);
	}
	free(kp);
}

/* Manda sig a todo processo da classe do serviço. Devolve quantos. */
static int
sinalizar_classe(const struct servico *s, int sig)
{
	struct kinfo_proc *kp;
	size_t n, i;
	int total = 0;

	if (strlen(s->m.nome) > CLASSE_NOME_MAX || (kp = processos(&n)) == NULL)
		return (0);
	for (i = 0; i < n; i++) {
		if (dono(&kp[i]) == s && kp[i].ki_pid != getpid() &&
		    kill(kp[i].ki_pid, sig) == 0)
			total++;
	}
	free(kp);
	return (total);
}

static int
racct_ligado(void)
{
	int v = 0;
	size_t len = sizeof(v);

	if (sysctlbyname("kern.racct.enable", &v, &len, NULL, 0) == -1)
		return (0);
	return (v != 0);
}

static const char *
erro_limites(int e)
{
	return (e == ENOSYS ? "o kernel está com o RACCT desligado "
	    "(kern.racct.enable=0)" : strerror(e));
}

/* --- escolha persistente de ativar/desativar -------------------------- */

static int
escolha_salva(const char *nome)
{
	char caminho[PATH_MAX], buf[16];
	ssize_t n;
	int fd;

	snprintf(caminho, sizeof(caminho), "%s/%s", VIGIA_DBDIR, nome);
	if ((fd = open(caminho, O_RDONLY | O_CLOEXEC)) == -1)
		return (-1);
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return (-1);
	buf[n] = '\0';
	return (strncmp(buf, "ativo", 5) == 0);
}

static void
salvar_escolha(const char *nome, int ativo)
{
	char caminho[PATH_MAX];
	int fd;

	snprintf(caminho, sizeof(caminho), "%s/%s", VIGIA_DBDIR, nome);
	fd = open(caminho, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd == -1) {
		syslog(LOG_ERR, "%s: %m", caminho);
		return;
	}
	dprintf(fd, "%s\n", ativo ? "ativo" : "desativado");
	close(fd);
}

static int
calcular_habilitado(const struct servico *s)
{
	int e;

	if (!s->valido)
		return (0);
	e = escolha_salva(s->m.nome);
	return (e == -1 ? s->m.ativo : e);
}

/* --- leitura dos arquivos de serviço ---------------------------------- */

static void
inserir(struct servico *novo)
{
	struct servico **pp;

	for (pp = &servicos; *pp != NULL; pp = &(*pp)->prox)
		if (strcmp((*pp)->m.nome, novo->m.nome) > 0)
			break;
	novo->prox = *pp;
	*pp = novo;
}

/* Lê (ou relê) /etc/vigia/servicos. Retorna quantos serviços há. */
static int
carregar(void)
{
	struct servico *s;
	struct manifesto m;
	struct dirent *de;
	char caminho[PATH_MAX], nome[NOME_MAX];
	size_t n;
	DIR *d;
	int ok, total = 0, era;

	for (s = servicos; s != NULL; s = s->prox)
		s->removido = 1;

	if ((d = opendir(VIGIA_SERVICOS)) == NULL) {
		syslog(LOG_ERR, "%s: %m", VIGIA_SERVICOS);
	} else {
		while ((de = readdir(d)) != NULL) {
			n = strlen(de->d_name);
			if (n <= 5 || n - 5 >= NOME_MAX ||
			    strcmp(de->d_name + n - 5, ".conf") != 0)
				continue;
			memcpy(nome, de->d_name, n - 5);
			nome[n - 5] = '\0';
			snprintf(caminho, sizeof(caminho), "%s/%s",
			    VIGIA_SERVICOS, de->d_name);
			ok = manifesto_ler(caminho, nome, &m) == 0;
			total++;

			if ((s = achar(nome)) == NULL) {
				if ((s = calloc(1, sizeof(*s))) == NULL) {
					syslog(LOG_ERR, "sem memória");
					break;
				}
				s->lfd = -1;
				s->m = m;
				s->valido = ok;
				s->habilitado = calcular_habilitado(s);
				s->quer_rodar = s->habilitado;
				s->est = s->habilitado ? E_PARADO : E_DESATIVADO;
				s->desde = agora();
				inserir(s);
				if (!ok)
					mudar(s, E_ERRO, "%s", m.erro);
				continue;
			}

			/* Já conhecido: aplica a nova configuração. */
			s->removido = 0;
			era = s->habilitado;
			s->m = m;
			s->valido = ok;
			s->habilitado = calcular_habilitado(s);
			if (!ok) {
				if (!tem_processos(s) && s->lfd == -1)
					mudar(s, E_ERRO, "%s", m.erro);
				else
					syslog(LOG_WARNING, "%s: arquivo "
					    "inválido, mantendo o que está "
					    "rodando: %s", nome, m.erro);
			} else if (s->est == E_ERRO) {
				s->quer_rodar = s->habilitado;
				mudar(s, s->habilitado ? E_PARADO :
				    E_DESATIVADO, NULL);
			} else if (!era && s->habilitado) {
				s->quer_rodar = 1;
			} else if (!tem_processos(s) && s->est == E_PARADO &&
			    !s->habilitado) {
				mudar(s, E_DESATIVADO, NULL);
			}
		}
		closedir(d);
	}

	for (s = servicos; s != NULL; s = s->prox) {
		if (s->removido) {
			s->quer_rodar = 0;
			s->reiniciar_depois = 0;
		}
	}
	return (total);
}

static void
limpar_removidos(void)
{
	struct servico **pp, *s;

	for (pp = &servicos; (s = *pp) != NULL;) {
		if (s->removido && !tem_processos(s) && s->lfd == -1) {
			syslog(LOG_INFO, "%s: removido", s->m.nome);
			remover_limites(s);
			*pp = s->prox;
			free(s);
		} else {
			pp = &s->prox;
		}
	}
}

/* --- criação de processos --------------------------------------------- */

struct falha_filho {
	int	etapa;
	int	erro;
};

#define	ETAPA_DIRETORIO	1
#define	ETAPA_CLASSE	2
#define	ETAPA_USUARIO	3
#define	ETAPA_EXEC	4

static const char *
etapa_texto(int e)
{
	switch (e) {
	case ETAPA_DIRETORIO:	return ("diretório");
	case ETAPA_CLASSE:	return ("classe de login");
	case ETAPA_USUARIO:	return ("trocar de usuário");
	default:		return ("exec");
	}
}

static void __dead2
filho(const struct servico *s, const struct passwd *pw, int aviso, int logfd,
    int nulo, int conexao, const char *cliente)
{
	static char envbuf[MAX_AMBIENTE + 8][PATH_MAX + 32];
	char *env[MAX_AMBIENTE + 9], cmd[sizeof(s->m.comando) + 8];
	char *argv[4], cls[MAXLOGNAME];
	struct falha_filho ff;
	sigset_t vazio;
	int i, ne = 0;

	sigemptyset(&vazio);
	sigprocmask(SIG_SETMASK, &vazio, NULL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	setsid();
	if (conexao >= 0) {
		dup2(conexao, STDIN_FILENO);
		dup2(conexao, STDOUT_FILENO);
	} else {
		dup2(nulo, STDIN_FILENO);
		dup2(logfd, STDOUT_FILENO);
	}
	dup2(logfd, STDERR_FILENO);

	ff.etapa = ETAPA_DIRETORIO;
	if (chdir(s->m.diretorio[0] != '\0' ? s->m.diretorio : "/") == -1)
		goto falhou;

	ff.etapa = ETAPA_CLASSE;
	if (classe(s, cls, sizeof(cls)) == 0 && setloginclass(cls) == -1)
		goto falhou;

	ff.etapa = ETAPA_USUARIO;
	if (s->m.usuario[0] != '\0') {
		if (setgid(pw->pw_gid) == -1 ||
		    initgroups(pw->pw_name, pw->pw_gid) == -1 ||
		    setuid(pw->pw_uid) == -1)
			goto falhou;
	}

	snprintf(envbuf[ne], sizeof(envbuf[0]),
	    "PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin");
	env[ne] = envbuf[ne]; ne++;
	snprintf(envbuf[ne], sizeof(envbuf[0]), "HOME=%s", pw->pw_dir);
	env[ne] = envbuf[ne]; ne++;
	snprintf(envbuf[ne], sizeof(envbuf[0]), "USER=%s", pw->pw_name);
	env[ne] = envbuf[ne]; ne++;
	snprintf(envbuf[ne], sizeof(envbuf[0]), "LOGNAME=%s", pw->pw_name);
	env[ne] = envbuf[ne]; ne++;
	snprintf(envbuf[ne], sizeof(envbuf[0]), "SHELL=%s", pw->pw_shell);
	env[ne] = envbuf[ne]; ne++;
	snprintf(envbuf[ne], sizeof(envbuf[0]), "VIGIA_SERVICO=%s",
	    s->m.nome);
	env[ne] = envbuf[ne]; ne++;
	if (cliente != NULL) {
		snprintf(envbuf[ne], sizeof(envbuf[0]), "VIGIA_CLIENTE=%s",
		    cliente);
		env[ne] = envbuf[ne]; ne++;
	}
	for (i = 0; i < s->m.nambiente; i++) {
		strlcpy(envbuf[ne], s->m.ambiente[i], sizeof(envbuf[0]));
		env[ne] = envbuf[ne]; ne++;
	}
	env[ne] = NULL;

	snprintf(cmd, sizeof(cmd), "exec %s", s->m.comando);
	argv[0] = __DECONST(char *, "sh");
	argv[1] = __DECONST(char *, "-c");
	argv[2] = cmd;
	argv[3] = NULL;
	ff.etapa = ETAPA_EXEC;
	execve("/bin/sh", argv, env);
falhou:
	ff.erro = errno;
	(void)write(aviso, &ff, sizeof(ff));
	_exit(127);
}

/*
 * Cria um processo do serviço: o principal (conexao == -1) ou o que atende
 * uma conexão. Devolve o pid, ou -1 com a explicação em motivo.
 */
static pid_t
lancar(struct servico *s, int conexao, const char *cliente, char *motivo,
    size_t mlen)
{
	struct falha_filho ff;
	struct passwd *pw;
	char logpath[PATH_MAX];
	int aviso[2], logfd, nulo;
	ssize_t n;
	pid_t pid;

	if (s->m.usuario[0] != '\0')
		pw = getpwnam(s->m.usuario);
	else
		pw = getpwuid(0);
	if (pw == NULL) {
		snprintf(motivo, mlen, "usuário '%s' não existe",
		    s->m.usuario);
		return (-1);
	}

	snprintf(logpath, sizeof(logpath), "%s/%s.log", VIGIA_LOGDIR,
	    s->m.nome);
	logfd = open(logpath, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0640);
	if (logfd == -1)
		logfd = open("/dev/null", O_WRONLY | O_CLOEXEC);
	nulo = open("/dev/null", O_RDONLY | O_CLOEXEC);
	if (logfd == -1 || nulo == -1 || pipe2(aviso, O_CLOEXEC) == -1) {
		snprintf(motivo, mlen, "não foi possível preparar: %s",
		    strerror(errno));
		if (logfd != -1)
			close(logfd);
		if (nulo != -1)
			close(nulo);
		return (-1);
	}

	pid = fork();
	if (pid == 0)
		filho(s, pw, aviso[1], logfd, nulo, conexao, cliente);
	close(aviso[1]);
	close(logfd);
	close(nulo);
	if (pid == -1) {
		snprintf(motivo, mlen, "fork falhou: %s", strerror(errno));
		close(aviso[0]);
		return (-1);
	}

	do {
		n = read(aviso[0], &ff, sizeof(ff));
	} while (n == -1 && errno == EINTR);
	close(aviso[0]);
	if (n == (ssize_t)sizeof(ff)) {
		waitpid(pid, NULL, 0);
		snprintf(motivo, mlen, "não foi possível iniciar (%s: %s)",
		    etapa_texto(ff.etapa), strerror(ff.erro));
		return (-1);
	}
	return (pid);
}

static void
iniciar_processo(struct servico *s)
{
	char motivo[256];
	pid_t pid;
	int e;

	if ((e = aplicar_limites(s)) != 0 && tem_limites(s)) {
		mudar(s, E_MANUTENCAO, "limites não aplicados: %s",
		    erro_limites(e));
		return;
	}
	registrar(s, "executando: %s", s->m.comando);
	if ((pid = lancar(s, -1, NULL, motivo, sizeof(motivo))) == -1) {
		mudar(s, E_MANUTENCAO, "%s", motivo);
		return;
	}
	s->pid = pid;
	s->inicio = agora();
	mudar(s, E_INICIANDO, "pid %d", (int)pid);
}

/* --- modo sob demanda (escutar = ...) --------------------------------- */

static void
escuta_ligada(struct servico *s, int ligar)
{
	struct kevent kev;

	if (s->lfd == -1)
		return;
	EV_SET(&kev, s->lfd, EVFILT_READ, ligar ? EV_ENABLE : EV_DISABLE, 0,
	    0, NULL);
	(void)kevent(kq, &kev, 1, NULL, 0, NULL);
	s->pausado = !ligar;
}

static int
abrir_escuta(struct servico *s, char *motivo, size_t mlen)
{
	struct sockaddr_in sin;
	struct sockaddr_un sun;
	struct kevent kev;
	char onde[160];
	int fd, um = 1;

	escuta_texto(&s->m, onde, sizeof(onde));
	if (s->m.escuta == ESCUTA_TCP) {
		fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC |
		    SOCK_NONBLOCK, 0);
		if (fd == -1)
			goto falhou;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &um, sizeof(um));
		memset(&sin, 0, sizeof(sin));
		sin.sin_len = sizeof(sin);
		sin.sin_family = AF_INET;
		sin.sin_port = htons((uint16_t)s->m.escuta_porta);
		if (s->m.escuta_endereco[0] != '\0')
			inet_pton(AF_INET, s->m.escuta_endereco, &sin.sin_addr);
		else
			sin.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) == -1)
			goto fechar;
	} else {
		fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC |
		    SOCK_NONBLOCK, 0);
		if (fd == -1)
			goto falhou;
		memset(&sun, 0, sizeof(sun));
		sun.sun_family = AF_UNIX;
		strlcpy(sun.sun_path, s->m.escuta_caminho,
		    sizeof(sun.sun_path));
		unlink(sun.sun_path);
		if (bind(fd, (struct sockaddr *)&sun, SUN_LEN(&sun)) == -1 ||
		    chmod(sun.sun_path, 0666) == -1)
			goto fechar;
		strlcpy(s->lcaminho, s->m.escuta_caminho, sizeof(s->lcaminho));
	}
	if (listen(fd, 64) == -1)
		goto fechar;
	EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
		goto fechar;
	s->lfd = fd;
	s->pausado = 0;
	return (0);

fechar:
	{
		int e = errno;

		close(fd);
		errno = e;
	}
falhou:
	snprintf(motivo, mlen, "não foi possível escutar em %s: %s", onde,
	    strerror(errno));
	return (-1);
}

static void
fechar_escuta(struct servico *s)
{
	if (s->lfd == -1)
		return;
	close(s->lfd);		/* o kqueue esquece o descritor sozinho */
	s->lfd = -1;
	s->pausado = 0;
	if (s->lcaminho[0] != '\0') {
		unlink(s->lcaminho);
		s->lcaminho[0] = '\0';
	}
}

static void
comecar_a_ouvir(struct servico *s)
{
	char motivo[256], onde[160];
	int e;

	if ((e = aplicar_limites(s)) != 0 && tem_limites(s)) {
		mudar(s, E_MANUTENCAO, "limites não aplicados: %s",
		    erro_limites(e));
		return;
	}
	if (abrir_escuta(s, motivo, sizeof(motivo)) == -1) {
		mudar(s, E_MANUTENCAO, "%s", motivo);
		return;
	}
	escuta_texto(&s->m, onde, sizeof(onde));
	mudar(s, E_OUVINDO, "%s; sobe a cada conexão", onde);
}

/* Atende as conexões pendentes num socket de escuta. */
static void
aceitar(int lfd)
{
	struct sockaddr_storage ss;
	struct sockaddr_in *sin;
	struct servico *s;
	socklen_t sl;
	char cliente[64], motivo[256];
	pid_t pid;
	int c;

	for (s = servicos; s != NULL; s = s->prox)
		if (s->lfd == lfd)
			break;
	if (s == NULL || s->est != E_OUVINDO)
		return;

	while (s->nconexoes < s->m.max_conexoes) {
		sl = sizeof(ss);
		c = accept4(lfd, (struct sockaddr *)&ss, &sl, SOCK_CLOEXEC);
		if (c == -1)
			return;		/* EAGAIN: nada mais na fila */
		if (ss.ss_family == AF_INET) {
			sin = (struct sockaddr_in *)&ss;
			snprintf(cliente, sizeof(cliente), "%s:%u",
			    inet_ntoa(sin->sin_addr),
			    (unsigned)ntohs(sin->sin_port));
		} else {
			strlcpy(cliente, "local", sizeof(cliente));
		}
		pid = lancar(s, c, cliente, motivo, sizeof(motivo));
		close(c);
		if (pid == -1) {
			fechar_escuta(s);
			mudar(s, E_MANUTENCAO, "%s", motivo);
			return;
		}
		s->conexoes[s->nconexoes++] = pid;
		s->atendidas++;
	}
	/* No máximo: a fila espera no kernel até alguma conexão terminar. */
	escuta_ligada(s, 0);
	registrar(s, "%d conexões simultâneas (máximo); novas aguardam na fila",
	    s->nconexoes);
}

/* --- parada, reconciliação e processos que terminaram ----------------- */

static void
sinalizar(const struct servico *s, int sig)
{
	int i;

	if (s->pid > 0 && killpg(s->pid, sig) == -1)
		kill(s->pid, sig);
	for (i = 0; i < s->nconexoes; i++)
		if (killpg(s->conexoes[i], sig) == -1)
			kill(s->conexoes[i], sig);
	/* E quem tiver escapado do grupo de processos. */
	(void)sinalizar_classe(s, sig);
}

static void
parou(struct servico *s, const char *como)
{
	if (s->reiniciar_depois) {
		s->reiniciar_depois = 0;
		s->quer_rodar = 1;
		s->falhas = 0;
	}
	mudar(s, s->habilitado || s->quer_rodar ? E_PARADO : E_DESATIVADO,
	    "parou (%s)", como);
}

static void
parar_processos(struct servico *s)
{
	fechar_escuta(s);
	if (!tem_processos(s)) {
		parou(s, "nenhum processo rodando");
		return;
	}
	sinalizar(s, SIGTERM);
	s->prazo = agora() + PRAZO_PARADA;
	if (s->pid > 0)
		mudar(s, E_PARANDO, "SIGTERM enviado ao pid %d", (int)s->pid);
	else
		mudar(s, E_PARANDO, "SIGTERM enviado a %d conexão(ões)",
		    s->nconexoes);
}

static void
reconciliar(struct servico *s)
{
	struct servico *d;
	const char *falta;
	time_t t = agora();
	int i;

	/* Prazo de parada vale mesmo se alguém pediu para iniciar de novo. */
	if (s->est == E_PARANDO && tem_processos(s) && t >= s->prazo) {
		sinalizar(s, SIGKILL);
		registrar(s, "não parou em %d s: SIGKILL", PRAZO_PARADA);
		s->prazo = t + 3600;
	}

	if (!s->quer_rodar) {
		switch (s->est) {
		case E_AGUARDANDO:
		case E_REINICIANDO:
			mudar(s, s->habilitado ? E_PARADO : E_DESATIVADO, NULL);
			break;
		case E_INICIANDO:
		case E_ATIVO:
		case E_OUVINDO:
			parar_processos(s);
			break;
		default:
			break;
		}
		return;
	}

	if (!s->valido || s->removido)
		return;
	if (s->est == E_PARADO || s->est == E_DESATIVADO)
		mudar(s, E_AGUARDANDO, NULL);
	if (s->est == E_REINICIANDO && t >= s->proxima)
		mudar(s, E_AGUARDANDO, NULL);

	if (s->est == E_AGUARDANDO) {
		falta = NULL;
		for (i = 0; i < s->m.ndepende; i++) {
			d = achar(s->m.depende[i]);
			if (d == NULL || d->removido || !d->valido) {
				snprintf(s->motivo, sizeof(s->motivo),
				    "dependência '%s' não existe ou é "
				    "inválida", s->m.depende[i]);
				return;
			}
			if (d->est == E_MANUTENCAO) {
				snprintf(s->motivo, sizeof(s->motivo),
				    "dependência '%s' em manutenção",
				    d->m.nome);
				return;
			}
			if (!d->quer_rodar && !desligando)
				d->quer_rodar = 1;
			if (!de_pe(d) && falta == NULL)
				falta = d->m.nome;
		}
		if (falta != NULL) {
			snprintf(s->motivo, sizeof(s->motivo),
			    "esperando '%s' ficar ativo", falta);
			return;
		}
		if (s->m.escuta != ESCUTA_NENHUMA)
			comecar_a_ouvir(s);
		else
			iniciar_processo(s);
	}

	if (s->est == E_INICIANDO && t - s->inicio >= TEMPO_ATIVO)
		mudar(s, E_ATIVO, "rodando (pid %d)", (int)s->pid);
	if (s->est == E_ATIVO && s->falhas > 0 &&
	    t - s->inicio >= TEMPO_ESTAVEL)
		s->falhas = 0;
	if (s->est == E_OUVINDO && s->pausado &&
	    s->nconexoes < s->m.max_conexoes)
		escuta_ligada(s, 1);
}

static void
reconciliar_todos(void)
{
	struct servico *s;
	int i;

	/* Duas passadas: uma dependência puxada na primeira sobe na segunda. */
	for (i = 0; i < 2; i++)
		for (s = servicos; s != NULL; s = s->prox)
			reconciliar(s);
}

static void
como_terminou(int st, char *buf, size_t len, int *falhou)
{
	char sig[16];
	const char *nome;
	int i;

	if (WIFEXITED(st)) {
		snprintf(buf, len, "saiu com código %d", WEXITSTATUS(st));
		*falhou = WEXITSTATUS(st) != 0;
		return;
	}
	*falhou = 1;
	if (WIFSIGNALED(st) && WTERMSIG(st) < NSIG) {
		nome = sys_signame[WTERMSIG(st)];
		for (i = 0; nome[i] != '\0' && i < (int)sizeof(sig) - 1; i++)
			sig[i] = (char)toupper((unsigned char)nome[i]);
		sig[i] = '\0';
		snprintf(buf, len, "morto por SIG%s%s", sig,
		    WCOREDUMP(st) ? " (core)" : "");
		return;
	}
	snprintf(buf, len, "terminou (estado %d)", st);
}

static void
processo_terminou(struct servico *s, int st)
{
	char como[96];
	time_t rodou = agora() - s->inicio;
	int falhou, espera, sobra;

	como_terminou(st, como, sizeof(como), &falhou);
	s->pid = 0;

	/* Filhos que o processo principal deixou para trás não ficam soltos. */
	if ((sobra = sinalizar_classe(s, SIGKILL)) > 0)
		registrar(s, "%d processo(s) que sobraram foram encerrados",
		    sobra);

	if (s->est == E_PARANDO || !s->quer_rodar) {
		parou(s, como);
		return;
	}

	if (s->m.reiniciar == R_NUNCA ||
	    (s->m.reiniciar == R_FALHA && !falhou)) {
		s->quer_rodar = 0;
		mudar(s, E_PARADO, "%s", como);
		return;
	}

	if (rodou >= TEMPO_ESTAVEL)
		s->falhas = 0;
	s->falhas++;
	if (s->falhas >= MAX_FALHAS) {
		mudar(s, E_MANUTENCAO, "%s; caiu %d vezes seguidas, veja "
		    "'vigia log %s' e use 'vigia reiniciar %s'", como,
		    s->falhas, s->m.nome, s->m.nome);
		return;
	}
	espera = 1 << (s->falhas - 1);
	if (espera > ESPERA_MAX)
		espera = ESPERA_MAX;
	s->reinicios++;
	s->proxima = agora() + espera;
	mudar(s, E_REINICIANDO, "%s; nova tentativa em %d s", como, espera);
}

static void
conexao_terminou(struct servico *s, int i, int st)
{
	char como[96];
	int falhou;

	s->conexoes[i] = s->conexoes[--s->nconexoes];
	como_terminou(st, como, sizeof(como), &falhou);
	if (falhou)
		registrar(s, "conexão: %s", como);
	if (s->est == E_PARANDO && !tem_processos(s))
		parou(s, "conexões encerradas");
}

static void
colher(void)
{
	struct servico *s;
	pid_t pid;
	int st, i;

	while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
		for (s = servicos; s != NULL; s = s->prox) {
			if (s->pid == pid) {
				processo_terminou(s, st);
				break;
			}
			for (i = 0; i < s->nconexoes; i++)
				if (s->conexoes[i] == pid)
					break;
			if (i < s->nconexoes) {
				conexao_terminou(s, i, st);
				break;
			}
		}
	}
}

static int
rodando(void)
{
	struct servico *s;
	int n = 0;

	for (s = servicos; s != NULL; s = s->prox)
		n += (s->pid > 0) + s->nconexoes;
	return (n);
}

static void
iniciar_desligamento(void)
{
	struct servico *s;

	if (desligando)
		return;
	syslog(LOG_INFO, "desligando: parando %d processo(s)", rodando());
	desligando = 1;
	desligar_ate = agora() + PRAZO_PARADA + 3;
	for (s = servicos; s != NULL; s = s->prox) {
		s->quer_rodar = 0;
		s->reiniciar_depois = 0;
	}
}

/* --- dependentes (parada em cascata) ---------------------------------- */

/* t depende de alvo, direta ou indiretamente? */
static int
depende_de(const struct servico *t, const struct servico *alvo, int prof)
{
	const struct servico *d;
	int i;

	if (prof > 16)
		return (0);
	for (i = 0; i < t->m.ndepende; i++) {
		if (strcmp(t->m.depende[i], alvo->m.nome) == 0)
			return (1);
		if ((d = achar(t->m.depende[i])) != NULL &&
		    depende_de(d, alvo, prof + 1))
			return (1);
	}
	return (0);
}

/*
 * Para (ou, com reiniciar, reinicia) quem depende de s. Os nomes afetados
 * vão para lista.
 */
static void
cascata(const struct servico *s, int reiniciar, char *lista, size_t len)
{
	struct servico *t;

	lista[0] = '\0';
	for (t = servicos; t != NULL; t = t->prox) {
		if (t == s || !depende_de(t, s, 0))
			continue;
		if (!t->quer_rodar && !tem_processos(t) && t->lfd == -1)
			continue;
		if (reiniciar) {
			if (!tem_processos(t) && t->lfd == -1)
				continue;
			t->reiniciar_depois = 1;
		} else {
			t->reiniciar_depois = 0;
		}
		t->quer_rodar = 0;
		if (lista[0] != '\0')
			strlcat(lista, ", ", len);
		strlcat(lista, t->m.nome, len);
	}
}

/* --- pedidos do vigia(8) ------------------------------------------------ */

static void
cmd_info(int fd, const struct servico *s)
{
	char dur[32], deps[MAX_DEPS * NOME_MAX], a[32], b[32], onde[160];
	int i;

	duracao(agora() - s->desde, dur, sizeof(dur));
	deps[0] = '\0';
	for (i = 0; i < s->m.ndepende; i++) {
		if (i > 0)
			strlcat(deps, " ", sizeof(deps));
		strlcat(deps, s->m.depende[i], sizeof(deps));
	}
	medir();

	dprintf(fd, "ok\n");
	dprintf(fd, "serviço\t%s\n", s->m.nome);
	dprintf(fd, "descrição\t%s\n", s->m.descricao);
	dprintf(fd, "estado\t%s\n", estado_texto(s->est));
	if (s->motivo[0] != '\0')
		dprintf(fd, "motivo\t%s\n", s->motivo);
	dprintf(fd, "há\t%s\n", dur);
	if (s->pid > 0)
		dprintf(fd, "pid\t%d\n", (int)s->pid);
	if (s->m.escuta != ESCUTA_NENHUMA) {
		escuta_texto(&s->m, onde, sizeof(onde));
		dprintf(fd, "escuta\t%s (até %d conexões simultâneas)\n", onde,
		    s->m.max_conexoes);
		dprintf(fd, "conexões\t%d agora, %lu atendidas\n",
		    s->nconexoes, s->atendidas);
	} else {
		dprintf(fd, "reinícios\t%d\n", s->reinicios);
	}

	{
		tamanho(s->uso_mem, a, sizeof(a));
		if (s->m.limite_memoria > 0) {
			tamanho(s->m.limite_memoria, b, sizeof(b));
			dprintf(fd, "memória\t%s (limite %s)\n", a, b);
		} else {
			dprintf(fd, "memória\t%s\n", a);
		}
		if (s->m.limite_cpu > 0)
			dprintf(fd, "cpu\t%d%% (limite %d%%)\n", s->uso_cpu,
			    s->m.limite_cpu);
		else
			dprintf(fd, "cpu\t%d%%\n", s->uso_cpu);
		if (s->m.limite_processos > 0)
			dprintf(fd, "processos\t%d (limite %d)\n",
			    s->uso_procs, s->m.limite_processos);
		else
			dprintf(fd, "processos\t%d\n", s->uso_procs);
		if (tem_limites(s) && !racct_ligado())
			dprintf(fd, "atenção\tlimites inativos: o kernel está "
			    "com kern.racct.enable=0\n");
	}

	dprintf(fd, "no boot\t%s\n", s->habilitado ? "sobe sozinho" :
	    "desativado");
	dprintf(fd, "comando\t%s\n", s->m.comando);
	dprintf(fd, "usuário\t%s\n", s->m.usuario[0] ? s->m.usuario : "root");
	if (s->m.diretorio[0] != '\0')
		dprintf(fd, "diretório\t%s\n", s->m.diretorio);
	if (s->m.escuta == ESCUTA_NENHUMA)
		dprintf(fd, "reiniciar\t%s\n", reiniciar_texto(s->m.reiniciar));
	if (deps[0] != '\0')
		dprintf(fd, "depende de\t%s\n", deps);
	for (i = 0; i < s->m.nambiente; i++)
		dprintf(fd, "ambiente\t%s\n", s->m.ambiente[i]);
	dprintf(fd, "arquivo\t%s/%s.conf\n", VIGIA_SERVICOS, s->m.nome);
	dprintf(fd, "log\t%s/%s.log\n", VIGIA_LOGDIR, s->m.nome);
	if (!s->valido)
		dprintf(fd, "erro no arquivo\t%s\n", s->m.erro);
}

static void
comando(int fd, uid_t uid, char *linha)
{
	struct servico *s;
	char *verbo, *arg, *ctx, lista[512];
	int total, ativos, problemas;
	long long memtxt;

	verbo = strtok_r(linha, " \t\r\n", &ctx);
	arg = verbo != NULL ? strtok_r(NULL, " \t\r\n", &ctx) : NULL;
	if (verbo == NULL) {
		dprintf(fd, "erro: pedido vazio\n");
		return;
	}

	if (strcmp(verbo, "estado") == 0) {
		medir();
		dprintf(fd, "ok\n");
		for (s = servicos; s != NULL; s = s->prox) {
			memtxt = -1;
			if (tem_processos(s) || s->lfd != -1 || s->uso_procs > 0)
				memtxt = (long long)s->uso_mem;
			dprintf(fd, "%s\t%s\t%d\t%lld\t%d\t%s\t%s\t%lld\t%d\t%d\n",
			    s->m.nome, estado_texto(s->est), (int)s->pid,
			    (long long)(agora() - s->desde), s->reinicios,
			    s->m.descricao, s->motivo, memtxt,
			    s->m.escuta != ESCUTA_NENHUMA, s->nconexoes);
		}
		return;
	}
	if (strcmp(verbo, "resumo") == 0) {
		total = ativos = problemas = 0;
		for (s = servicos; s != NULL; s = s->prox) {
			total++;
			if (de_pe(s))
				ativos++;
			else if (s->est == E_MANUTENCAO || s->est == E_ERRO)
				problemas++;
		}
		dprintf(fd, "ok\n%d\t%d\t%d\n", total, ativos, problemas);
		return;
	}
	if (strcmp(verbo, "info") == 0) {
		if (arg == NULL || (s = achar(arg)) == NULL) {
			dprintf(fd, "erro: serviço '%s' não existe\n",
			    arg ? arg : "");
			return;
		}
		cmd_info(fd, s);
		return;
	}

	/* Daqui para baixo, só o root. */
	if (uid != 0) {
		dprintf(fd, "erro: permissão negada (use su - ou peça ao "
		    "administrador)\n");
		return;
	}
	if (desligando) {
		dprintf(fd, "erro: o sistema está desligando\n");
		return;
	}
	if (strcmp(verbo, "recarregar") == 0) {
		total = carregar();
		reconciliar_todos();
		dprintf(fd, "ok\n%d serviço(s) em %s\n", total, VIGIA_SERVICOS);
		return;
	}

	if (arg == NULL) {
		dprintf(fd, "erro: diga qual serviço\n");
		return;
	}
	if ((s = achar(arg)) == NULL || s->removido) {
		dprintf(fd, "erro: serviço '%s' não existe (arquivos em %s)\n",
		    arg, VIGIA_SERVICOS);
		return;
	}
	if (!s->valido && strcmp(verbo, "parar") != 0) {
		dprintf(fd, "erro: %s/%s.conf é inválido: %s\n",
		    VIGIA_SERVICOS, s->m.nome, s->m.erro);
		return;
	}

	lista[0] = '\0';
	if (strcmp(verbo, "iniciar") == 0) {
		if (s->est == E_MANUTENCAO) {
			s->falhas = 0;
			mudar(s, E_PARADO, "saindo da manutenção");
		}
		s->quer_rodar = 1;
	} else if (strcmp(verbo, "parar") == 0) {
		s->quer_rodar = 0;
		s->reiniciar_depois = 0;
		if (s->est == E_MANUTENCAO)
			mudar(s, s->habilitado ? E_PARADO : E_DESATIVADO,
			    NULL);
		cascata(s, 0, lista, sizeof(lista));
	} else if (strcmp(verbo, "reiniciar") == 0) {
		s->falhas = 0;
		if (tem_processos(s) || s->lfd != -1) {
			s->reiniciar_depois = 1;
			s->quer_rodar = 0;
			cascata(s, 1, lista, sizeof(lista));
		} else {
			if (s->est == E_MANUTENCAO)
				mudar(s, E_PARADO, "saindo da manutenção");
			s->quer_rodar = 1;
		}
	} else if (strcmp(verbo, "ativar") == 0) {
		salvar_escolha(s->m.nome, 1);
		s->habilitado = 1;
		if (s->est == E_MANUTENCAO) {
			s->falhas = 0;
			mudar(s, E_PARADO, "saindo da manutenção");
		}
		s->quer_rodar = 1;
	} else if (strcmp(verbo, "desativar") == 0) {
		salvar_escolha(s->m.nome, 0);
		s->habilitado = 0;
		s->quer_rodar = 0;
		s->reiniciar_depois = 0;
		if (s->est == E_PARADO || s->est == E_MANUTENCAO)
			mudar(s, E_DESATIVADO, NULL);
		cascata(s, 0, lista, sizeof(lista));
	} else {
		dprintf(fd, "erro: comando desconhecido '%s'\n", verbo);
		return;
	}
	reconciliar_todos();
	dprintf(fd, "ok\n");
	if (lista[0] != '\0')
		dprintf(fd, "%s também: %s\n", strcmp(verbo, "reiniciar") == 0 ?
		    "reiniciando" : "parando", lista);
}

static void
atender(int lfd)
{
	struct timeval tv = { 2, 0 };
	char buf[512];
	size_t n = 0;
	ssize_t r;
	uid_t uid;
	gid_t gid;
	int c;

	if ((c = accept4(lfd, NULL, NULL, SOCK_CLOEXEC)) == -1)
		return;
	setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (getpeereid(c, &uid, &gid) == -1)
		uid = (uid_t)-1;
	while (n < sizeof(buf) - 1) {
		r = read(c, buf + n, sizeof(buf) - 1 - n);
		if (r <= 0)
			break;
		n += (size_t)r;
		if (memchr(buf, '\n', n) != NULL)
			break;
	}
	buf[n] = '\0';
	comando(c, uid, buf);
	close(c);
}

static void
mkdir_p(const char *dir, mode_t modo)
{
	char tmp[PATH_MAX], *p;

	strlcpy(tmp, dir, sizeof(tmp));
	for (p = tmp + 1; *p != '\0'; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	if (mkdir(tmp, modo) == -1 && errno != EEXIST)
		syslog(LOG_ERR, "%s: %m", dir);
}

int
servidor_main(int argc, char **argv)
{
	struct sockaddr_un sun;
	struct kevent ev[16], mudancas[6];
	struct servico *s;
	int ch, frente = 0, lfd, n, i;
	FILE *f;

	while ((ch = getopt(argc, argv, "f")) != -1) {
		switch (ch) {
		case 'f':
			frente = 1;
			break;
		default:
			fprintf(stderr, "uso: vigiad [-f]\n");
			return (1);
		}
	}
	if (geteuid() != 0)
		errx(1, "precisa rodar como root");

	/* O kqueue não passa por fork(): daemon() vem antes dele. */
	if (!frente && daemon(0, 0) == -1)
		err(1, "daemon");
	openlog("vigiad", LOG_PID | (frente ? LOG_PERROR : 0), LOG_DAEMON);
	umask(022);
	mkdir_p(VIGIA_SERVICOS, 0755);
	mkdir_p(VIGIA_LOGDIR, 0750);
	mkdir_p(VIGIA_DBDIR, 0755);

	if ((f = fopen(VIGIA_PIDFILE, "w")) != NULL) {
		fprintf(f, "%d\n", (int)getpid());
		fclose(f);
	}

	if ((lfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1) {
		syslog(LOG_ERR, "socket: %m");
		return (1);
	}
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, VIGIA_SOCKET, sizeof(sun.sun_path));
	unlink(VIGIA_SOCKET);
	if (bind(lfd, (struct sockaddr *)&sun, SUN_LEN(&sun)) == -1 ||
	    chmod(VIGIA_SOCKET, 0666) == -1 || listen(lfd, 16) == -1) {
		syslog(LOG_ERR, "%s: %m", VIGIA_SOCKET);
		return (1);
	}

	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	/* SIGCHLD fica no padrão: ignorá-lo faria o kernel colher sozinho. */

	if ((kq = kqueue()) == -1) {
		syslog(LOG_ERR, "kqueue: %m");
		return (1);
	}
	EV_SET(&mudancas[0], lfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	EV_SET(&mudancas[1], SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	EV_SET(&mudancas[2], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	EV_SET(&mudancas[3], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	EV_SET(&mudancas[4], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	EV_SET(&mudancas[5], 1, EVFILT_TIMER, EV_ADD, 0, TICK_MS, NULL);
	if (kevent(kq, mudancas, 6, NULL, 0, NULL) == -1) {
		syslog(LOG_ERR, "kevent: %m");
		return (1);
	}

	n = carregar();
	syslog(LOG_INFO, "vigiando %d serviço(s) de %s", n, VIGIA_SERVICOS);
	reconciliar_todos();

	for (;;) {
		n = kevent(kq, NULL, 0, ev, 16, NULL);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			syslog(LOG_ERR, "kevent: %m");
			break;
		}
		for (i = 0; i < n; i++) {
			if (ev[i].filter == EVFILT_READ &&
			    (int)ev[i].ident == lfd) {
				atender(lfd);
			} else if (ev[i].filter == EVFILT_READ) {
				aceitar((int)ev[i].ident);
			} else if (ev[i].filter == EVFILT_SIGNAL) {
				if ((int)ev[i].ident == SIGTERM ||
				    (int)ev[i].ident == SIGINT)
					iniciar_desligamento();
				else if ((int)ev[i].ident == SIGHUP &&
				    !desligando)
					syslog(LOG_INFO, "SIGHUP: %d serviço(s)",
					    carregar());
			}
		}
		colher();
		reconciliar_todos();
		limpar_removidos();
		if (desligando && (rodando() == 0 || agora() > desligar_ate))
			break;
	}

	for (s = servicos; s != NULL; s = s->prox) {
		fechar_escuta(s);
		remover_limites(s);
	}
	unlink(VIGIA_SOCKET);
	unlink(VIGIA_PIDFILE);
	syslog(LOG_INFO, "encerrado");
	return (0);
}
