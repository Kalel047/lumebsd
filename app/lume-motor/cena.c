/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * A cena: caminhos de curvas e texto, no espaço do documento.
 *
 * Tudo vira curva quadrática monótona em y: linhas (com o ponto de controle
 * no meio), quadráticas divididas no ponto mais alto ou mais baixo, cúbicas
 * aproximadas por quadráticas. Assim o shader acha, para cada linha de
 * pixels, no máximo um cruzamento por curva, e a cobertura sai exata.
 * As letras vêm do contorno da fonte (FreeType), sem bitmaps.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include "motor.h"

static void *
crescer(void *p, uint32_t *cap, uint32_t precisa, size_t tam)
{
	if (precisa <= *cap)
		return p;
	uint32_t n = *cap ? *cap : 256;
	while (n < precisa)
		n *= 2;
	void *q = realloc(p, (size_t)n * tam);
	if (q == NULL)
		abort();
	*cap = n;
	return q;
}

void
cena_iniciar(motor_cena *c)
{
	memset(c, 0, sizeof(*c));
}

void
cena_limpar(motor_cena *c)
{
	c->nquads = c->ncaminhos = 0;
	c->aberto = false;
}

void
cena_liberar(motor_cena *c)
{
	free(c->quads);
	free(c->caminhos);
	memset(c, 0, sizeof(*c));
}

static void
quad_mono(motor_cena *c, float x0, float y0, float x1, float y1, float x2,
    float y2)
{
	/* Divide no extremo em y, se houver um dentro da curva. */
	float d = y0 - 2.0f * y1 + y2;
	if (fabsf(d) > 1e-9f) {
		float t = (y0 - y1) / d;
		if (t > 1e-4f && t < 1.0f - 1e-4f) {
			float ax = x0 + (x1 - x0) * t, ay = y0 + (y1 - y0) * t;
			float bx = x1 + (x2 - x1) * t, by = y1 + (y2 - y1) * t;
			float mx = ax + (bx - ax) * t, my = ay + (by - ay) * t;
			/* O ponto do meio é o extremo: os controles ficam na
			 * mesma altura, e cada metade é monótona. */
			ay = by = my;
			quad_mono(c, x0, y0, ax, ay, mx, my);
			quad_mono(c, mx, my, bx, by, x2, y2);
			return;
		}
	}
	if (y0 == y2 && y1 == y0)
		return; /* horizontal: não cruza nenhuma linha de pixels */
	c->quads = crescer(c->quads, &c->capq, c->nquads + 1, sizeof(motor_quad));
	c->quads[c->nquads++] = (motor_quad){ x0, y0, x1, y1, x2, y2 };
}

void
cena_comecar(motor_cena *c)
{
	c->inicio = c->nquads;
	c->aberto = false;
}

void
cena_mover(motor_cena *c, float x, float y)
{
	if (c->aberto)
		cena_fechar(c);
	c->px = c->ix = x;
	c->py = c->iy = y;
	c->aberto = true;
}

void
cena_linha(motor_cena *c, float x, float y)
{
	quad_mono(c, c->px, c->py, (c->px + x) * 0.5f, (c->py + y) * 0.5f, x, y);
	c->px = x;
	c->py = y;
}

void
cena_quad(motor_cena *c, float cx, float cy, float x, float y)
{
	quad_mono(c, c->px, c->py, cx, cy, x, y);
	c->px = x;
	c->py = y;
}

void
cena_cubica(motor_cena *c, float c1x, float c1y, float c2x, float c2y,
    float x, float y)
{
	/* Pedaços pela extensão do polígono de controle (~1 a cada 24 pontos,
	 * de 2 a 16); cada pedaço vira uma quadrática com o controle na média
	 * das tangentes. */
	float x0 = c->px, y0 = c->py;
	float len = hypotf(c1x - x0, c1y - y0) + hypotf(c2x - c1x, c2y - c1y) +
	    hypotf(x - c2x, y - c2y);
	int n = (int)(len / 24.0f) + 2;
	if (n > 16)
		n = 16;
	float px = x0, py = y0;
	for (int i = 1; i <= n; i++) {
		float t0 = (float)(i - 1) / n, t1 = (float)i / n;
		float tm = (t0 + t1) * 0.5f;
#define CUB(a, b, cc, d, t) \
	((1 - (t)) * (1 - (t)) * (1 - (t)) * (a) + 3 * (1 - (t)) * (1 - (t)) * (t) * (b) + \
	 3 * (1 - (t)) * (t) * (t) * (cc) + (t) * (t) * (t) * (d))
		float ex = CUB(x0, c1x, c2x, x, t1), ey = CUB(y0, c1y, c2y, y, t1);
		float mx = CUB(x0, c1x, c2x, x, tm), my = CUB(y0, c1y, c2y, y, tm);
#undef CUB
		/* O controle que faz a quadrática passar pelo ponto do meio. */
		float qx = 2.0f * mx - 0.5f * (px + ex);
		float qy = 2.0f * my - 0.5f * (py + ey);
		quad_mono(c, px, py, qx, qy, ex, ey);
		px = ex;
		py = ey;
	}
	c->px = x;
	c->py = y;
}

void
cena_fechar(motor_cena *c)
{
	if (!c->aberto)
		return;
	if (c->px != c->ix || c->py != c->iy)
		cena_linha(c, c->ix, c->iy);
	c->aberto = false;
}

void
cena_pintar(motor_cena *c, float r, float g, float b, float densidade)
{
	cena_fechar(c);
	uint32_t n = c->nquads - c->inicio;
	if (n == 0)
		return;
	float bx0 = INFINITY, by0 = INFINITY, bx1 = -INFINITY, by1 = -INFINITY;
	for (uint32_t i = c->inicio; i < c->nquads; i++) {
		const float *v = &c->quads[i].x0;
		for (int k = 0; k < 6; k += 2) {
			bx0 = fminf(bx0, v[k]);
			bx1 = fmaxf(bx1, v[k]);
			by0 = fminf(by0, v[k + 1]);
			by1 = fmaxf(by1, v[k + 1]);
		}
	}
	c->caminhos = crescer(c->caminhos, &c->capc, c->ncaminhos + 1,
	    sizeof(motor_caminho));
	c->caminhos[c->ncaminhos++] = (motor_caminho){
		c->inicio, n, { bx0, by0, bx1, by1 }, { r, g, b, densidade } };
	c->inicio = c->nquads;
}

void
cena_retangulo(motor_cena *c, float x, float y, float w, float h)
{
	cena_mover(c, x, y);
	cena_linha(c, x + w, y);
	cena_linha(c, x + w, y + h);
	cena_linha(c, x, y + h);
	cena_fechar(c);
}

void
cena_traco(motor_cena *c, float x0, float y0, float x1, float y1,
    float largura)
{
	float dx = x1 - x0, dy = y1 - y0, l = hypotf(dx, dy);
	if (l <= 0.0f)
		return;
	float nx = -dy / l * largura * 0.5f, ny = dx / l * largura * 0.5f;
	cena_mover(c, x0 + nx, y0 + ny);
	cena_linha(c, x1 + nx, y1 + ny);
	cena_linha(c, x1 - nx, y1 - ny);
	cena_linha(c, x0 - nx, y0 - ny);
	cena_fechar(c);
}

void
cena_elipse(motor_cena *c, float cx, float cy, float rx, float ry)
{
	/* Quatro cúbicas (k = 0,5523). */
	const float k = 0.5522847f;
	cena_mover(c, cx + rx, cy);
	cena_cubica(c, cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry);
	cena_cubica(c, cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy);
	cena_cubica(c, cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry);
	cena_cubica(c, cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy);
	cena_fechar(c);
}

/* --- texto -------------------------------------------------------------- */

struct motor_fonte {
	FT_Library ft;
	FT_Face face;
};

motor_fonte *
fonte_abrir(const char *arquivo)
{
	motor_fonte *f = calloc(1, sizeof(*f));
	if (f == NULL || FT_Init_FreeType(&f->ft) != 0) {
		free(f);
		return NULL;
	}
	if (FT_New_Face(f->ft, arquivo, 0, &f->face) != 0) {
		FT_Done_FreeType(f->ft);
		free(f);
		return NULL;
	}
	return f;
}

void
fonte_fechar(motor_fonte *f)
{
	if (f == NULL)
		return;
	FT_Done_Face(f->face);
	FT_Done_FreeType(f->ft);
	free(f);
}

static uint32_t
utf8(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t c = *p++;
	int n = 0;
	if (c >= 0xf0) {
		c &= 0x07;
		n = 3;
	} else if (c >= 0xe0) {
		c &= 0x0f;
		n = 2;
	} else if (c >= 0xc0) {
		c &= 0x1f;
		n = 1;
	}
	while (n-- > 0 && (*p & 0xc0) == 0x80)
		c = (c << 6) | (*p++ & 0x3f);
	*s = (const char *)p;
	return c;
}

/* O contorno da letra, da fonte (y para cima, em unidades da fonte) para o
 * documento (y para baixo). */
typedef struct {
	motor_cena *c;
	float escala, ox, oy;
} contorno;

#define PX(v) (k->ox + (float)(v)->x * k->escala)
#define PY(v) (k->oy - (float)(v)->y * k->escala)

static int
ct_mover(const FT_Vector *p, void *u)
{
	contorno *k = u;
	cena_mover(k->c, PX(p), PY(p));
	return 0;
}

static int
ct_linha(const FT_Vector *p, void *u)
{
	contorno *k = u;
	cena_linha(k->c, PX(p), PY(p));
	return 0;
}

static int
ct_quad(const FT_Vector *c1, const FT_Vector *p, void *u)
{
	contorno *k = u;
	cena_quad(k->c, PX(c1), PY(c1), PX(p), PY(p));
	return 0;
}

static int
ct_cubica(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *p, void *u)
{
	contorno *k = u;
	cena_cubica(k->c, PX(c1), PY(c1), PX(c2), PY(c2), PX(p), PY(p));
	return 0;
}

static float
avanco(motor_fonte *f, uint32_t cp, uint32_t anterior, float escala,
    FT_UInt *glifo)
{
	FT_UInt g = FT_Get_Char_Index(f->face, cp);
	*glifo = g;
	if (FT_Load_Glyph(f->face, g, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) != 0)
		return 0.0f;
	float a = (float)f->face->glyph->advance.x * escala;
	if (anterior && FT_HAS_KERNING(f->face)) {
		FT_Vector kern;
		if (FT_Get_Kerning(f->face, FT_Get_Char_Index(f->face, anterior),
		    g, FT_KERNING_UNSCALED, &kern) == 0)
			a += (float)kern.x * escala;
	}
	return a;
}

float
fonte_largura(motor_fonte *f, const char *s, float tamanho)
{
	float escala = tamanho / (float)f->face->units_per_EM, x = 0.0f;
	uint32_t ant = 0;
	FT_UInt g;
	while (*s) {
		uint32_t cp = utf8(&s);
		x += avanco(f, cp, ant, escala, &g);
		ant = cp;
	}
	return x;
}

static float
palavra(motor_cena *c, motor_fonte *f, const char *ini, const char *fim,
    float x, float y, float escala, float r, float g, float b, float d)
{
	static const FT_Outline_Funcs funcs = { ct_mover, ct_linha, ct_quad,
		ct_cubica, 0, 0 };
	uint32_t ant = 0;
	const char *s = ini;
	while (s < fim) {
		uint32_t cp = utf8(&s);
		FT_UInt glifo;
		float a = avanco(f, cp, ant, escala, &glifo);
		/* O kerning entra antes da letra. */
		float kern = 0.0f;
		if (ant && FT_HAS_KERNING(f->face)) {
			FT_Vector kv;
			if (FT_Get_Kerning(f->face, FT_Get_Char_Index(f->face, ant),
			    glifo, FT_KERNING_UNSCALED, &kv) == 0)
				kern = (float)kv.x * escala;
		}
		x += kern;
		if (f->face->glyph->format == FT_GLYPH_FORMAT_OUTLINE &&
		    f->face->glyph->outline.n_points > 0) {
			contorno k = { c, escala, x, y };
			cena_comecar(c);
			FT_Outline_Decompose(&f->face->glyph->outline, &funcs, &k);
			cena_pintar(c, r, g, b, d);
		}
		x += a - kern;
		ant = cp;
	}
	return x;
}

float
cena_texto(motor_cena *c, motor_fonte *f, const char *s, float x, float y,
    float tamanho, float largura, float entrelinha, float r, float g, float b,
    float d)
{
	float escala = tamanho / (float)f->face->units_per_EM;
	float espaco = fonte_largura(f, " ", tamanho);
	float cx = x;
	while (*s) {
		if (*s == '\n') {
			cx = x;
			y += entrelinha;
			s++;
			continue;
		}
		if (*s == ' ') {
			cx += espaco;
			s++;
			continue;
		}
		const char *fim = s;
		while (*fim && *fim != ' ' && *fim != '\n')
			fim++;
		char buf[256];
		size_t n = (size_t)(fim - s) < sizeof(buf) - 1 ? (size_t)(fim - s) : sizeof(buf) - 1;
		memcpy(buf, s, n);
		buf[n] = '\0';
		float w = fonte_largura(f, buf, tamanho);
		if (largura > 0.0f && cx > x && cx + w > x + largura) {
			cx = x;
			y += entrelinha;
		}
		cx = palavra(c, f, s, fim, cx, y, escala, r, g, b, d);
		s = fim;
	}
	return y + entrelinha;
}
