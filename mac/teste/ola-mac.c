/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Um programa do macOS mínimo, sem a libSystem: fala com o kernel XNU por
 * syscall direto (o número com 0x2000000, como a libsystem_kernel da Apple
 * faz). Serve para testar o lume-macho: o mesmo binário roda num Mac.
 */
#define SYS(n) (0x2000000 | (n))

static long
xnu(long n, long a, long b, long c, long d, long e, long f)
{
	long r;
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	__asm__ volatile("syscall"
	    : "=a"(r)
	    : "a"(SYS(n)), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
	    : "rcx", "r11", "memory", "cc");
	return r;
}

static unsigned long
tam(const char *s)
{
	unsigned long n = 0;
	while (s[n])
		n++;
	return n;
}

static void
escrever(const char *s)
{
	xnu(4, 1, (long)s, (long)tam(s), 0, 0, 0); /* write */
}

static void
numero(long v)
{
	char b[24];
	int i = 23;
	b[i] = 0;
	if (v == 0)
		b[--i] = '0';
	while (v > 0) {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
	}
	escrever(b + i);
}

void
start(void)
{
	escrever("Olá do macOS! Um programa Mach-O rodando no LumeBSD.\n");
	escrever("  getpid() = ");
	numero(xnu(20, 0, 0, 0, 0, 0, 0)); /* getpid */
	escrever("\n");
	/* mmap anônimo (no XNU, 197; MAP_PRIVATE|MAP_ANON = 0x1002) */
	char *p = (char *)xnu(197, 0, 4096, 3, 0x1002, -1, 0);
	if ((long)p > 0) {
		const char *m = "  memória do mmap: ok\n";
		for (int i = 0; m[i]; i++)
			p[i] = m[i];
		p[tam(m)] = 0;
		escrever(p);
	} else {
		escrever("  mmap falhou\n");
	}
	xnu(1, 0, 0, 0, 0, 0, 0); /* exit(0) */
}
