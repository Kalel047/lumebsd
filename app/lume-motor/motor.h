/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * lume-motor: o motor de renderização vetorial do LumeBSD (protótipo).
 *
 * A aplicação descreve o que desenhar (caminhos de curvas e texto); a GPU
 * calcula a cobertura de cada pixel pelas equações das curvas, aplica o
 * papel e a tinta e assenta o quadro novo sobre o anterior (a viscosidade).
 * A arquitetura está em docs/MOTOR.md.
 */
#ifndef LUME_MOTOR_H
#define LUME_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

/* Uma curva quadrática de Bézier, já monótona em y (o motor divide). */
typedef struct {
	float x0, y0, x1, y1, x2, y2;
} motor_quad;

/* Um caminho pronto para a GPU: as curvas, a caixa e a tinta. */
typedef struct {
	uint32_t primeira;      /* índice da primeira curva */
	uint32_t quantas;
	float caixa[4];         /* xmin, ymin, xmax, ymax (espaço do documento) */
	float tinta[4];         /* r, g, b e a densidade (0 = papel, 1 = tinta) */
} motor_caminho;

/* A cena: curvas e caminhos, no espaço do documento (pontos de 1/96"). */
typedef struct {
	motor_quad *quads;
	uint32_t nquads, capq;
	motor_caminho *caminhos;
	uint32_t ncaminhos, capc;
	/* caminho sendo construído */
	float px, py, ix, iy;
	uint32_t inicio;
	bool aberto;
} motor_cena;

void cena_iniciar(motor_cena *c);
void cena_limpar(motor_cena *c);
void cena_liberar(motor_cena *c);
/* Construção de um caminho, como no PostScript/SVG. */
void cena_comecar(motor_cena *c);
void cena_mover(motor_cena *c, float x, float y);
void cena_linha(motor_cena *c, float x, float y);
void cena_quad(motor_cena *c, float cx, float cy, float x, float y);
void cena_cubica(motor_cena *c, float c1x, float c1y, float c2x, float c2y,
    float x, float y);
void cena_fechar(motor_cena *c);
/* Termina o caminho com uma tinta (cor e densidade). */
void cena_pintar(motor_cena *c, float r, float g, float b, float densidade);
/* Atalhos: retângulo, contorno (traço) de um segmento, elipse. */
void cena_retangulo(motor_cena *c, float x, float y, float w, float h);
void cena_traco(motor_cena *c, float x0, float y0, float x1, float y1,
    float largura);
void cena_elipse(motor_cena *c, float cx, float cy, float rx, float ry);

/* Texto: as letras viram curvas (sem bitmaps nem atlas de fontes). */
typedef struct motor_fonte motor_fonte;
motor_fonte *fonte_abrir(const char *arquivo);
void fonte_fechar(motor_fonte *f);
/* Escreve a partir de (x, y) (linha de base), quebrando em "largura"
 * (0: sem quebra). Devolve o y da próxima linha. */
float cena_texto(motor_cena *c, motor_fonte *f, const char *utf8, float x,
    float y, float tamanho, float largura, float entrelinha, float r,
    float g, float b, float densidade);
/* A largura de um texto numa linha só. */
float fonte_largura(motor_fonte *f, const char *utf8, float tamanho);

/* O renderizador (precisa de um contexto GLES 3.1 corrente). */
typedef struct motor motor;
typedef struct {
	float papel[3];         /* cor do papel */
	float grao;             /* 0 a 1: as fibras do papel */
	float viscosidade_ms;   /* constante de tempo do assentamento; 0 desliga */
	bool colorido;          /* e-ink colorido (cores suaves) ou só tinta */
} motor_opcoes;

motor *motor_criar(int largura, int altura, const motor_opcoes *o);
void motor_destruir(motor *m);
/* Envia a cena (as curvas vão para um buffer na GPU). */
void motor_cena_enviar(motor *m, const motor_cena *c);
/* Desenha um quadro no framebuffer atual. "vista" leva o documento para a
 * tela: escala e deslocamento (x' = x * escala + dx). dt_ms: o tempo desde
 * o quadro anterior. */
void motor_quadro(motor *m, float escala, float dx, float dy, float dt_ms);
/* O quadro já assentou (a tinta parou de migrar)? */
bool motor_parado(const motor *m);
void motor_reiniciar_historico(motor *m);
const char *motor_erro(void);

#endif
