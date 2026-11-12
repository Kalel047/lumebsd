/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * lume-motor: demonstração do motor de renderização do LumeBSD.
 *
 * Uma página de papel e tinta (título, texto e o farol, tudo em curvas),
 * com um zoom até 8x (as letras continuam nítidas: são equações) e a troca
 * de um parágrafo (a tinta assenta, sem piscar).
 *
 *   lume-motor                      na tela, pelo DRM/KMS (num console, sem
 *                                   o X; Enter sai)
 *   lume-motor --png PASTA          sem tela: grava quadros em PNG
 *
 * Opções: --drm /dev/dri/cardN, --tamanho LxA e --hz N (só PNG; o padrão é
 * 180), --segundos N,
 * --viscosidade MS (0 desliga), --grao 0-100, --colorido, --fonte ARQUIVO.
 */
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <GLES3/gl31.h>

#include "papel.h"
#include "motor.h"
#include "saida.h"

static const char *FONTES[] = {
	"/usr/local/share/fonts/plex-ttf/IBMPlexSerif-Regular.ttf",
	"/usr/local/share/fonts/IBMPlex/IBMPlexSerif-Regular.ttf",
	"/usr/share/fonts/truetype/ibm-plex/IBMPlexSerif-Regular.ttf",
	"/usr/local/share/fonts/dejavu/DejaVuSerif.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
	NULL
};

static motor_fonte *
fonte_variante(const char *regular, const char *de, const char *para)
{
	char nome[512];
	const char *p = strstr(regular, de);
	if (p == NULL)
		return fonte_abrir(regular);
	snprintf(nome, sizeof(nome), "%.*s%s%s", (int)(p - regular), regular,
	    para, p + strlen(de));
	motor_fonte *f = fonte_abrir(nome);
	return f ? f : fonte_abrir(regular);
}

static const char *TEXTO_A =
    "O motor desenha cada letra pelas equações das suas curvas, direto na "
    "placa de vídeo. Não há bitmaps nem atlas de fontes: o que está na tela "
    "é o mesmo desenho de um documento impresso, nítido em qualquer zoom e "
    "em qualquer densidade de tela.";

static const char *TEXTO_B =
    "Quando a página muda, a tinta não pisca como num e-ink de verdade: ela "
    "assenta sobre o quadro anterior, como pigmento migrando no papel. As "
    "fibras de celulose ficam presas à tela, e as bordas da tinta seguem "
    "por elas.";

/* O farol do papel de parede, em curvas. */
static void
farol(motor_cena *c, float x, float y, float s)
{
	const float d = 0.92f;
	/* torre */
	cena_comecar(c);
	cena_mover(c, x - 45 * s, y);
	cena_linha(c, x + 45 * s, y);
	cena_linha(c, x + 32 * s, y - 270 * s);
	cena_linha(c, x - 32 * s, y - 270 * s);
	cena_fechar(c);
	cena_mover(c, x - 41 * s, y - 4 * s);
	cena_linha(c, x - 29 * s, y - 266 * s);
	cena_linha(c, x + 29 * s, y - 266 * s);
	cena_linha(c, x + 41 * s, y - 4 * s);
	cena_fechar(c);
	cena_pintar(c, 0, 0, 0, d);
	/* faixas */
	for (int i = 0; i < 3; i++) {
		float y0 = y - (60 + i * 70) * s, y1 = y0 - 26 * s;
		float w0 = 45 - (60 + i * 70) * 13.0f / 270.0f, w1 = 45 - (86 + i * 70) * 13.0f / 270.0f;
		cena_comecar(c);
		cena_mover(c, x - w0 * s, y0);
		cena_linha(c, x + w0 * s, y0);
		cena_linha(c, x + w1 * s, y1);
		cena_linha(c, x - w1 * s, y1);
		cena_pintar(c, 0, 0, 0, d);
	}
	/* galeria, lanterna e cúpula */
	cena_comecar(c);
	cena_retangulo(c, x - 48 * s, y - 280 * s, 96 * s, 10 * s);
	cena_pintar(c, 0, 0, 0, d);
	cena_comecar(c);
	cena_retangulo(c, x - 24 * s, y - 330 * s, 48 * s, 50 * s);
	/* o vidro: o contorno de dentro no sentido contrário vira um furo */
	cena_mover(c, x - 21 * s, y - 327 * s);
	cena_linha(c, x - 21 * s, y - 283 * s);
	cena_linha(c, x + 21 * s, y - 283 * s);
	cena_linha(c, x + 21 * s, y - 327 * s);
	cena_fechar(c);
	cena_pintar(c, 0, 0, 0, d);
	cena_comecar(c);
	cena_mover(c, x - 27 * s, y - 330 * s);
	cena_cubica(c, x - 27 * s, y - 362 * s, x + 27 * s, y - 362 * s, x + 27 * s, y - 330 * s);
	cena_fechar(c);
	cena_pintar(c, 0, 0, 0, d);
	/* a luz */
	for (int i = -2; i <= 2; i++) {
		cena_comecar(c);
		cena_traco(c, x - 34 * s, y - 305 * s, x - 330 * s, y - (305 + i * 38) * s, 1.4f * s);
		cena_traco(c, x + 34 * s, y - 305 * s, x + 330 * s, y - (305 + i * 38) * s, 1.4f * s);
		cena_pintar(c, 0, 0, 0, 0.45f);
	}
	/* o mar: ondas em traços curtos sobre uma senoide */
	for (int f = 0; f < 3; f++) {
		cena_comecar(c);
		float yy = y + (22 + f * 26) * s;
		for (int k = 0; k < 90; k++) {
			float a = (float)k / 90.0f, b = (float)(k + 1) / 90.0f;
			float xa = x - 420 * s + a * 840 * s, xb = x - 420 * s + b * 840 * s;
			if (fmodf(a * 7.0f + f * 0.3f, 1.0f) > 0.7f)
				continue;
			cena_traco(c, xa, yy + sinf(a * 44.0f + f) * 5 * s, xb,
			    yy + sinf(b * 44.0f + f) * 5 * s, 1.3f * s);
		}
		cena_pintar(c, 0, 0, 0, 0.55f - f * 0.12f);
	}
}

static void
pagina(motor_cena *c, motor_fonte *serif, motor_fonte *italico,
    motor_fonte *mono, int w, int h, bool segundo, bool colorido)
{
	cena_limpar(c);
	float m = w * 0.07f, col = w * 0.46f;
	float y = h * 0.16f;
	cena_texto(c, serif, "LumeBSD", m, y, h * 0.10f, 0, 0, 0, 0, 0, 1.0f);
	y += h * 0.07f;
	cena_texto(c, italico, "luz firme sobre fundação BSD", m, y, h * 0.035f,
	    0, 0, 0, 0, 0, 0.62f);
	y += h * 0.035f;
	cena_comecar(c);
	cena_retangulo(c, m, y, col, 1.2f);
	cena_pintar(c, 0, 0, 0, 0.35f);
	y += h * 0.07f;
	y = cena_texto(c, serif, segundo ? TEXTO_B : TEXTO_A, m, y, h * 0.026f,
	    col, h * 0.042f, 0, 0, 0, 0.93f);
	y += h * 0.02f;
	cena_texto(c, mono, "1.5  ·  FAROL  ·  MOTOR VETORIAL", m, y, h * 0.018f,
	    0, 0, 0, 0, 0, 0.55f);
	if (colorido) {
		/* e-ink colorido: pigmentos suaves */
		const float cores[4][3] = { { 0.75f, 0.2f, 0.18f }, { 0.2f, 0.45f, 0.7f },
			{ 0.85f, 0.65f, 0.15f }, { 0.25f, 0.55f, 0.3f } };
		for (int i = 0; i < 4; i++) {
			cena_comecar(c);
			cena_elipse(c, m + i * h * 0.06f + h * 0.02f, h * 0.88f, h * 0.02f, h * 0.02f);
			cena_pintar(c, cores[i][0], cores[i][1], cores[i][2], 0.9f);
		}
	}
	farol(c, w * 0.78f, h * 0.70f, h / 900.0f);
}

/* O zoom da demonstração: aproxima do título até 8x e volta. */
static void
vista(double t, int w, int h, float *escala, float *dx, float *dy)
{
	float z = 1.0f;
	if (t > 1.5 && t < 5.5) {
		double u = (t - 1.5) / 4.0;
		z = (float)(1.0 + 7.0 * sin(u * M_PI) * sin(u * M_PI));
	}
	/* o ponto fixo: a letra "L" do título */
	float fx = w * 0.09f, fy = h * 0.12f;
	*escala = z;
	*dx = fx - fx * z;
	*dy = fy - fy * z;
}

static int
uso(void)
{
	fprintf(stderr, "uso: lume-motor [--png PASTA] [--drm /dev/dri/cardN] "
	    "[--tamanho LxA] [--hz N] [--segundos N] [--viscosidade MS] [--grao 0-100] "
	    "[--colorido] [--fonte ARQUIVO]\n");
	return 2;
}

int
main(int argc, char **argv)
{
	const char *png = NULL, *drm = "/dev/dri/card0", *arq_fonte = NULL;
	int w = 1920, h = 1080;
	double segundos = 8.0, hz = 180.0;
	motor_opcoes o = { { LUME_PAPEL_DIA }, 0.4f, 35.0f, false };
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(a, "--png") && v) png = argv[++i];
		else if (!strcmp(a, "--drm") && v) drm = argv[++i];
		else if (!strcmp(a, "--tamanho") && v) { sscanf(v, "%dx%d", &w, &h); i++; }
		else if (!strcmp(a, "--segundos") && v) segundos = atof(argv[++i]);
		else if (!strcmp(a, "--hz") && v) hz = atof(argv[++i]);
		else if (!strcmp(a, "--viscosidade") && v) o.viscosidade_ms = (float)atof(argv[++i]);
		else if (!strcmp(a, "--grao") && v) o.grao = (float)atof(argv[++i]) / 100.0f;
		else if (!strcmp(a, "--colorido")) o.colorido = true;
		else if (!strcmp(a, "--fonte") && v) arq_fonte = argv[++i];
		else return uso();
	}

	saida s;
	bool ok = png ? saida_sem_tela(&s, w, h) : saida_drm(&s, drm);
	if (!ok) {
		fprintf(stderr, "lume-motor: %s\n", saida_erro());
		return 1;
	}
	w = s.w;
	h = s.h;
	printf("lume-motor: %dx%d, %s %s\n", w, h, glGetString(GL_RENDERER),
	    png ? "(sem tela)" : "");
	if (!png)
		printf("lume-motor: %.2f Hz (modo de maior taxa na resolução do painel)\n", s.hz);

	for (int i = 0; !arq_fonte && FONTES[i]; i++)
		if (access(FONTES[i], R_OK) == 0)
			arq_fonte = FONTES[i];
	if (arq_fonte == NULL) {
		fprintf(stderr, "lume-motor: nenhuma fonte (use --fonte)\n");
		return 1;
	}
	motor_fonte *serif = fonte_abrir(arq_fonte);
	motor_fonte *italico = fonte_variante(arq_fonte, "Regular", "Italic");
	motor_fonte *mono = fonte_variante(arq_fonte, "Serif-Regular", "Mono-Regular");
	if (!serif) {
		fprintf(stderr, "lume-motor: não abriu a fonte %s\n", arq_fonte);
		return 1;
	}

	motor *m = motor_criar(w, h, &o);
	if (m == NULL) {
		fprintf(stderr, "lume-motor: %s\n", motor_erro());
		return 1;
	}
	motor_cena c;
	cena_iniciar(&c);
	pagina(&c, serif, italico, mono, w, h, false, o.colorido);
	motor_cena_enviar(m, &c);
	printf("lume-motor: %u curvas em %u caminhos (%.1f KB na GPU; um bitmap "
	    "da tela teria %.0f KB)\n", c.nquads, c.ncaminhos,
	    c.nquads * 32 / 1024.0, w * h * 4 / 1024.0);

	struct termios antes;
	bool tty = !png && isatty(0) && tcgetattr(0, &antes) == 0;
	if (tty) {
		struct termios cru = antes;
		cru.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(0, TCSANOW, &cru);
		fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
	}
	if (png)
		mkdir(png, 0755);

	/* Sem tela, o tempo é o de um painel de "hz" (180 Hz por padrão). */
	const double passo_png = 1000.0 / (hz > 1.0 ? hz : 180.0);
	double t0 = saida_agora_ms(), anterior = t0, soma_gpu = 0.0, max_gpu = 0.0;
	double soma_intervalo = 0.0;
	int quadros = 0;
	bool trocou = false, zoom_salvo = false;
	double t_troca = -1.0;
	/* Os quadros salvos depois da troca, em ms. */
	const double marcas[] = { 0.0, 15.0, 40.0, 100.0, 300.0 };
	int proxima_marca = 0;
	for (;;) {
		double agora = png ? t0 + quadros * passo_png : saida_agora_ms();
		double t = (agora - t0) / 1000.0;
		if (t > segundos)
			break;
		if (tty) {
			char ch;
			if (read(0, &ch, 1) == 1)
				break;
		}
		if (!trocou && t > 5.8) {
			pagina(&c, serif, italico, mono, w, h, true, o.colorido);
			motor_cena_enviar(m, &c);
			trocou = true;
			t_troca = agora;
		}
		float e, dx, dy;
		vista(t, w, h, &e, &dx, &dy);
		double ini = saida_agora_ms();
		glBindFramebuffer(GL_FRAMEBUFFER, png ? s.fb : 0);
		motor_quadro(m, e, dx, dy, (float)(agora - anterior));
		glFinish();
		double gpu = saida_agora_ms() - ini;
		if (quadros > 0) {
			soma_gpu += gpu;
			soma_intervalo += agora - anterior;
		}
		if (gpu > max_gpu && quadros > 2)
			max_gpu = gpu;
		if (!saida_mostrar(&s)) {
			fprintf(stderr, "lume-motor: %s\n", saida_erro());
			break;
		}
		if (png) {
			char nome[600];
			/* a página, o zoom no máximo e a tinta assentando */
			bool salvar = quadros == 0;
			if (!zoom_salvo && t >= 3.5) {
				salvar = zoom_salvo = true;
			}
			if (t_troca >= 0.0 && proxima_marca < 5 &&
			    agora - t_troca >= marcas[proxima_marca]) {
				salvar = true;
				while (proxima_marca < 5 && agora - t_troca >= marcas[proxima_marca])
					proxima_marca++;
			}
			if (salvar) {
				snprintf(nome, sizeof(nome), "%s/quadro-%04d.png", png, quadros);
				saida_png(&s, nome);
				printf("  %s (t = %.3f s)\n", nome, t);
			}
		}
		anterior = agora;
		quadros++;
	}
	if (tty)
		tcsetattr(0, TCSANOW, &antes);
	if (quadros > 1)
		printf("lume-motor: %d quadros; desenho na GPU: média %.2f ms, "
		    "máximo %.2f ms; intervalo médio %.2f ms\n", quadros,
		    soma_gpu / (quadros - 1), max_gpu, soma_intervalo / (quadros - 1));
	cena_liberar(&c);
	motor_destruir(m);
	fonte_fechar(serif);
	fonte_fechar(italico);
	fonte_fechar(mono);
	saida_fechar(&s);
	return 0;
}
