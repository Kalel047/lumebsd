/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * lume-macho: roda programas do macOS (Mach-O, x86-64) no LumeBSD.
 *
 * O primeiro passo da compatibilidade com o Mac (docs/MACOS.md). Duas
 * partes:
 *
 *   1. o carregador: lê o Mach-O (também o "universal", com várias
 *      arquiteturas), põe os segmentos na memória nos endereços do
 *      programa e chama o ponto de entrada (LC_MAIN) ou pula para ele com
 *      a pilha que o kernel do macOS monta (LC_UNIXTHREAD);
 *   2. as chamadas de sistema: o programa fala com o kernel do macOS (XNU)
 *      por "syscall" com o número da classe BSD (0x2000000 + n). O FreeBSD
 *      e o XNU vêm do 4.4BSD: os argumentos vão nos mesmos registradores, o
 *      erro volta no carry e muitos números e flags são iguais. O
 *      lume-macho acompanha o programa pelo ptrace e, na entrada de cada
 *      chamada, troca o número do XNU pelo do FreeBSD (e as flags que
 *      mudam); o kernel relê os registradores e executa a chamada nativa.
 *
 * Ainda não: o dyld e as bibliotecas do macOS (libSystem, Foundation...),
 * que vêm do Darling; as chamadas Mach (mensagens entre processos) e várias
 * chamadas BSD. Programas que precisam delas param com uma mensagem.
 *
 *   lume-macho [-v] PROGRAMA [argumentos...]
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <machine/reg.h>

#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- Mach-O (os campos usados; <mach-o/loader.h> da Apple) -------------- */

#define MH_MAGIC_64	0xfeedfacfu
#define FAT_MAGIC	0xcafebabeu	/* big-endian no arquivo */
#define CPU_TYPE_X86_64	0x01000007
#define MH_EXECUTE	0x2
#define LC_REQ_DYLD	0x80000000u
#define LC_SEGMENT_64	0x19
#define LC_UNIXTHREAD	0x5
#define LC_LOAD_DYLIB	0xc
#define LC_LOAD_WEAK_DYLIB (0x18 | LC_REQ_DYLD)
#define LC_REEXPORT_DYLIB (0x1f | LC_REQ_DYLD)
#define LC_MAIN		(0x28 | LC_REQ_DYLD)
#define VM_PROT_READ	1
#define VM_PROT_WRITE	2
#define VM_PROT_EXECUTE	4

struct mach_header_64 {
	uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds,
	    flags, reserved;
};
struct load_command {
	uint32_t cmd, cmdsize;
};
struct segment_command_64 {
	uint32_t cmd, cmdsize;
	char segname[16];
	uint64_t vmaddr, vmsize, fileoff, filesize;
	uint32_t maxprot, initprot, nsects, flags;
};
struct entry_point_command {
	uint32_t cmd, cmdsize;
	uint64_t entryoff, stacksize;
};

static int verboso;

static void
morrer(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "lume-macho: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(127);
}

static uint32_t
be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* O pedaço x86-64 de um Mach-O universal (ou o arquivo inteiro). */
static off_t
fatia(int fd)
{
	unsigned char h[8];
	if (pread(fd, h, 8, 0) != 8)
		morrer("arquivo curto demais");
	if (be32(h) != FAT_MAGIC)
		return 0;
	uint32_t n = be32(h + 4);
	for (uint32_t i = 0; i < n && i < 32; i++) {
		unsigned char a[20];
		if (pread(fd, a, 20, 8 + 20 * i) != 20)
			break;
		if (be32(a) == CPU_TYPE_X86_64)
			return be32(a + 8);
	}
	morrer("este programa do Mac não tem a versão Intel (x86-64); "
	    "programas só para Apple Silicon (arm64) não rodam num PC");
	return 0;
}

typedef struct {
	uint64_t entrada;	/* endereço do ponto de entrada */
	int tipo_main;		/* 1: LC_MAIN (função); 0: LC_UNIXTHREAD */
	uint64_t pilha;		/* tamanho pedido da pilha (LC_MAIN) */
} programa;

static programa
carregar(const char *caminho)
{
	int fd = open(caminho, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		morrer("%s: %s", caminho, strerror(errno));
	off_t base = fatia(fd);
	struct mach_header_64 mh;
	if (pread(fd, &mh, sizeof(mh), base) != sizeof(mh) || mh.magic != MH_MAGIC_64)
		morrer("%s não é um programa Mach-O de 64 bits", caminho);
	if (mh.cputype != CPU_TYPE_X86_64)
		morrer("%s não é x86-64", caminho);
	if (mh.filetype != MH_EXECUTE)
		morrer("%s não é um executável (é uma biblioteca ou outro tipo)", caminho);
	char *cmds = malloc(mh.sizeofcmds);
	if (cmds == NULL ||
	    pread(fd, cmds, mh.sizeofcmds, base + (off_t)sizeof(mh)) != (ssize_t)mh.sizeofcmds)
		morrer("comandos de carga truncados");

	programa p = { 0, 0, 0 };
	uint64_t texto = 0, entryoff = 0;
	int tem_main = 0;
	char *c = cmds;
	for (uint32_t i = 0; i < mh.ncmds; i++) {
		struct load_command *lc = (struct load_command *)c;
		if (lc->cmdsize < 8 || c + lc->cmdsize > cmds + mh.sizeofcmds)
			morrer("comando de carga inválido");
		switch (lc->cmd) {
		case LC_LOAD_DYLIB:
		case LC_LOAD_WEAK_DYLIB:
		case LC_REEXPORT_DYLIB: {
			const char *nome = c + *(uint32_t *)(c + 8);
			morrer("%s usa a biblioteca %s do macOS.\n"
			    "  O lume-macho ainda não tem o dyld e as bibliotecas do macOS (o\n"
			    "  próximo passo, com o Darling): por enquanto só roda programas\n"
			    "  que falam direto com o kernel. Veja: lume ajuda mac", caminho, nome);
		}
		case LC_SEGMENT_64: {
			struct segment_command_64 *s = (struct segment_command_64 *)c;
			if (s->vmsize == 0 || (s->initprot == 0 && s->filesize == 0))
				break; /* __PAGEZERO: o FreeBSD já deixa o zero vazio */
			if (strncmp(s->segname, "__TEXT", 16) == 0)
				texto = s->vmaddr;
			size_t pg = (size_t)getpagesize();
			size_t tam = (s->vmsize + pg - 1) & ~(pg - 1);
			void *m = mmap((void *)(uintptr_t)s->vmaddr, tam,
			    PROT_READ | PROT_WRITE, MAP_FIXED | MAP_EXCL | MAP_PRIVATE | MAP_ANON,
			    -1, 0);
			if (m == MAP_FAILED)
				morrer("não deu para reservar %.16s em 0x%jx: %s", s->segname,
				    (uintmax_t)s->vmaddr, strerror(errno));
			if (s->filesize > 0 &&
			    pread(fd, m, s->filesize, base + (off_t)s->fileoff) != (ssize_t)s->filesize)
				morrer("%.16s truncado", s->segname);
			int prot = ((s->initprot & VM_PROT_READ) ? PROT_READ : 0) |
			    ((s->initprot & VM_PROT_WRITE) ? PROT_WRITE : 0) |
			    ((s->initprot & VM_PROT_EXECUTE) ? PROT_EXEC : 0);
			if (mprotect(m, tam, prot) != 0)
				morrer("mprotect %.16s: %s", s->segname, strerror(errno));
			if (verboso)
				fprintf(stderr, "lume-macho: %-16.16s 0x%09jx  %6ju KB  %c%c%c\n",
				    s->segname, (uintmax_t)s->vmaddr, (uintmax_t)tam / 1024,
				    prot & PROT_READ ? 'r' : '-', prot & PROT_WRITE ? 'w' : '-',
				    prot & PROT_EXEC ? 'x' : '-');
			break;
		}
		case LC_MAIN: {
			struct entry_point_command *e = (struct entry_point_command *)c;
			entryoff = e->entryoff;
			p.pilha = e->stacksize;
			tem_main = 1;
			break;
		}
		case LC_UNIXTHREAD: {
			/* x86_THREAD_STATE64: flavor, count, e os 21 registradores;
			 * o rip é o 17º. */
			uint32_t *t = (uint32_t *)(c + 8);
			if (t[0] == 4 /* x86_THREAD_STATE64 */)
				p.entrada = *(uint64_t *)((char *)(t + 2) + 16 * 8);
			break;
		}
		}
		c += lc->cmdsize;
	}
	if (tem_main) {
		p.entrada = texto + entryoff;
		p.tipo_main = 1;
	}
	if (p.entrada == 0)
		morrer("%s não tem ponto de entrada", caminho);
	free(cmds);
	close(fd);
	return p;
}

/* --- as chamadas do XNU -------------------------------------------------- */

#define CLASSE(n)	((n) >> 24)
#define CL_MACH		1
#define CL_BSD		2
#define CL_MDEP		3

/* Flags do open(2) que mudam do XNU para o FreeBSD. */
static long
flags_open(long f)
{
	long r = f & 0x0fff; /* acesso, NONBLOCK, APPEND, SHLOCK, EXLOCK, ASYNC,
				FSYNC, NOFOLLOW, CREAT, TRUNC, EXCL: iguais */
	if (f & 0x00008000)	/* O_EVTONLY: só para eventos */
		r |= O_RDONLY;
	if (f & 0x00020000)	/* O_NOCTTY */
		r |= O_NOCTTY;
	if (f & 0x00100000)	/* O_DIRECTORY */
		r |= O_DIRECTORY;
	if (f & 0x01000000)	/* O_CLOEXEC */
		r |= O_CLOEXEC;
	return r;
}

/* Uma chamada do XNU: o número no FreeBSD e o que ajustar. 0 = não sabe. */
enum { NADA, OPEN, MMAP, ENTROPIA };
typedef struct {
	int freebsd;
	int ajuste;
	const char *nome;
} traducao;

static traducao
traduzir(long n)
{
	switch (n) {
	case 1: return (traducao){ SYS_exit, NADA, "exit" };
	case 3: return (traducao){ SYS_read, NADA, "read" };
	case 4: return (traducao){ SYS_write, NADA, "write" };
	case 5: return (traducao){ SYS_open, OPEN, "open" };
	case 6: return (traducao){ SYS_close, NADA, "close" };
	case 10: return (traducao){ SYS_unlink, NADA, "unlink" };
	case 12: return (traducao){ SYS_chdir, NADA, "chdir" };
	case 20: return (traducao){ SYS_getpid, NADA, "getpid" };
	case 24: return (traducao){ SYS_getuid, NADA, "getuid" };
	case 25: return (traducao){ SYS_geteuid, NADA, "geteuid" };
	case 33: return (traducao){ SYS_access, NADA, "access" };
	case 37: return (traducao){ SYS_kill, NADA, "kill" };
	case 39: return (traducao){ SYS_getppid, NADA, "getppid" };
	case 43: return (traducao){ SYS_getegid, NADA, "getegid" };
	case 47: return (traducao){ SYS_getgid, NADA, "getgid" };
	case 54: return (traducao){ SYS_ioctl, NADA, "ioctl" };
	case 73: return (traducao){ SYS_munmap, NADA, "munmap" };
	case 74: return (traducao){ SYS_mprotect, NADA, "mprotect" };
	case 116: return (traducao){ SYS_gettimeofday, NADA, "gettimeofday" };
	case 121: return (traducao){ SYS_writev, NADA, "writev" };
	case 136: return (traducao){ SYS_mkdir, NADA, "mkdir" };
	case 137: return (traducao){ SYS_rmdir, NADA, "rmdir" };
	case 197: return (traducao){ SYS_mmap, MMAP, "mmap" };
	case 199: return (traducao){ SYS_lseek, NADA, "lseek" };
	case 327: return (traducao){ SYS_issetugid, NADA, "issetugid" };
	case 397: return (traducao){ SYS_write, NADA, "write_nocancel" };
	case 396: return (traducao){ SYS_read, NADA, "read_nocancel" };
	case 398: return (traducao){ SYS_open, OPEN, "open_nocancel" };
	case 399: return (traducao){ SYS_close, NADA, "close_nocancel" };
	case 500: return (traducao){ SYS_getrandom, ENTROPIA, "getentropy" };
	}
	return (traducao){ 0, NADA, NULL };
}

static int
acompanhar(pid_t filho)
{
	int st;
	int dentro = 0;		/* entre a entrada e a saída de uma chamada */
	int pendente = NADA;	/* o ajuste da saída */
	int recusada = 0;	/* chamada que o lume-macho não sabe: ENOSYS */
	for (;;) {
		if (waitpid(filho, &st, 0) < 0)
			return 127;
		if (WIFEXITED(st))
			return WEXITSTATUS(st);
		if (WIFSIGNALED(st)) {
			fprintf(stderr, "lume-macho: o programa terminou com o sinal %d\n",
			    WTERMSIG(st));
			return 128 + WTERMSIG(st);
		}
		int sinal = WSTOPSIG(st);
		if (sinal == SIGTRAP) {
			struct ptrace_lwpinfo li;
			if (ptrace(PT_LWPINFO, filho, (caddr_t)&li, sizeof(li)) == 0 &&
			    (li.pl_flags & (PL_FLAG_SCE | PL_FLAG_SCX))) {
				struct reg r;
				ptrace(PT_GETREGS, filho, (caddr_t)&r, 0);
				if (li.pl_flags & PL_FLAG_SCE) {
					long n = r.r_rax;
					dentro = 1;
					pendente = NADA;
					recusada = 0;
					if (CLASSE(n) == CL_BSD) {
						traducao t = traduzir(n & 0xffffff);
						if (t.nome == NULL) {
							fprintf(stderr, "lume-macho: chamada do macOS ainda não traduzida: BSD %ld\n",
							    n & 0xffffff);
							r.r_rax = SYS_getpid; /* inofensiva; a saída vira ENOSYS */
							recusada = 1;
						} else {
							if (verboso > 1)
								fprintf(stderr, "lume-macho: %s\n", t.nome);
							r.r_rax = t.freebsd;
							if (t.ajuste == OPEN)
								r.r_rsi = flags_open(r.r_rsi);
							else if (t.ajuste == MMAP)
								r.r_r10 &= ~0x0800L; /* MAP_JIT */
							else if (t.ajuste == ENTROPIA)
								r.r_rdx = 0;
							pendente = t.ajuste;
						}
						ptrace(PT_SETREGS, filho, (caddr_t)&r, 0);
					} else if (CLASSE(n) == CL_MACH || CLASSE(n) == CL_MDEP) {
						fprintf(stderr, "lume-macho: chamada %s %ld ainda não existe aqui (vem do darlingserver)\n",
						    CLASSE(n) == CL_MACH ? "Mach" : "machdep", n & 0xffffff);
						r.r_rax = SYS_getpid;
						recusada = 1;
						ptrace(PT_SETREGS, filho, (caddr_t)&r, 0);
					}
					/* As chamadas do próprio lume-macho (antes do
					 * programa) passam como estão. */
				} else if (dentro) {
					dentro = 0;
					if (recusada) {
						r.r_rax = ENOSYS;
						r.r_rflags |= 1; /* carry: erro */
						ptrace(PT_SETREGS, filho, (caddr_t)&r, 0);
					} else if (pendente == ENTROPIA && !(r.r_rflags & 1)) {
						r.r_rax = 0; /* getentropy devolve 0 */
						ptrace(PT_SETREGS, filho, (caddr_t)&r, 0);
					}
				}
				sinal = 0;
			} else {
				sinal = 0; /* o SIGTRAP do próprio ptrace */
			}
		} else if (sinal == SIGSTOP) {
			sinal = 0;
		}
		ptrace(PT_SYSCALL, filho, (caddr_t)1, sinal);
	}
}

typedef int (*main_macos)(int, char **, char **, char **);

int
main(int argc, char **argv)
{
	int a = 1;
	while (a < argc && strcmp(argv[a], "-v") == 0) {
		verboso++;
		a++;
	}
	if (a >= argc) {
		fprintf(stderr, "uso: lume-macho [-v] PROGRAMA [argumentos...]\n");
		return 2;
	}
	pid_t filho = fork();
	if (filho < 0)
		morrer("fork: %s", strerror(errno));
	if (filho == 0) {
		programa p = carregar(argv[a]);
		/* "apple": o que o kernel do macOS passa depois do ambiente. */
		char exe[1100];
		snprintf(exe, sizeof(exe), "executable_path=%s", argv[a]);
		char *apple[] = { exe, NULL };
		extern char **environ;
		if (ptrace(PT_TRACE_ME, 0, NULL, 0) != 0)
			morrer("ptrace: %s", strerror(errno));
		raise(SIGSTOP);
		if (p.tipo_main) {
			main_macos m = (main_macos)(uintptr_t)p.entrada;
			int r = m(argc - a, argv + a, environ, apple);
			/* O dyld chamaria exit() com o retorno; aqui o exit do
			 * FreeBSD faz o mesmo. */
			_exit(r);
		}
		/* LC_UNIXTHREAD: a pilha como o kernel do macOS monta
		 * (argc, argv, 0, envp, 0, apple, 0) e um salto. */
		int n = argc - a, ne = 0;
		while (environ[ne])
			ne++;
		uint64_t *pilha = malloc(sizeof(uint64_t) * (size_t)(n + ne + 8)) ;
		uint64_t *topo = pilha;
		*topo++ = (uint64_t)n;
		for (int i = 0; i < n; i++)
			*topo++ = (uint64_t)(uintptr_t)argv[a + i];
		*topo++ = 0;
		for (int i = 0; i < ne; i++)
			*topo++ = (uint64_t)(uintptr_t)environ[i];
		*topo++ = 0;
		*topo++ = (uint64_t)(uintptr_t)exe;
		*topo++ = 0;
		__asm__ volatile("mov %0, %%rsp\n\tjmp *%1" : : "r"(pilha), "r"(p.entrada));
		__builtin_unreachable();
	}
	/* O pai acompanha: na primeira parada (o SIGSTOP), passa a parar em
	 * cada chamada de sistema. */
	return acompanhar(filho);
}
