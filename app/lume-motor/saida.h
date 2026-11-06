/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * As saídas do lume-motor: a tela (DRM/KMS) ou nenhuma (para PNG).
 */
#ifndef LUME_SAIDA_H
#define LUME_SAIDA_H

#include <stdbool.h>
#include <stdint.h>

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <xf86drmMode.h>

struct gbm_device;
struct gbm_surface;
struct gbm_bo;

typedef struct saida {
	int w, h;
	float hz;
	/* EGL */
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLSurface surf;
	/* sem tela */
	GLuint fb, rb;
	/* DRM/KMS */
	int fd;
	uint32_t conector, crtc;
	drmModeModeInfo modo;
	drmModeCrtc *crtc_antes;
	struct gbm_device *gbm;
	struct gbm_surface *superficie;
	struct gbm_bo *bo_anterior;
	bool modo_ligado;
} saida;

bool saida_sem_tela(saida *s, int w, int h);
bool saida_png(saida *s, const char *arquivo);
bool saida_drm(saida *s, const char *dispositivo);
/* Mostra o quadro (na tela: espera o vblank). */
bool saida_mostrar(saida *s);
void saida_fechar(saida *s);
double saida_agora_ms(void);
const char *saida_erro(void);

#endif
