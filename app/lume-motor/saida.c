/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Onde o lume-motor desenha:
 *   - DRM/KMS: direto na tela, sem X11 nem Wayland. Abre /dev/dri/cardN,
 *     escolhe o monitor ligado e o modo da resolução preferida com a maior
 *     taxa de atualização (180 Hz, se o painel tiver), e troca os quadros
 *     no vblank (page flip), com GBM e EGL;
 *   - sem tela (surfaceless): o mesmo motor num contexto EGL sem janela,
 *     para testes e para gerar imagens (PNG).
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <gbm.h>
#include <png.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "saida.h"

static char erro[512];

const char *
saida_erro(void)
{
	return erro;
}

static bool
contexto(saida *s, EGLenum plataforma, void *nativo, bool janela)
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
	    (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (get_display == NULL) {
		snprintf(erro, sizeof(erro), "EGL sem eglGetPlatformDisplayEXT");
		return false;
	}
	s->dpy = get_display(plataforma, nativo, NULL);
	if (s->dpy == EGL_NO_DISPLAY || !eglInitialize(s->dpy, NULL, NULL)) {
		snprintf(erro, sizeof(erro), "EGL: não abriu o display (0x%x)",
		    eglGetError());
		return false;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint attrs[] = {
		EGL_SURFACE_TYPE, janela ? EGL_WINDOW_BIT : EGL_PBUFFER_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_NONE
	};
	EGLConfig cfgs[64];
	EGLint n = 0;
	eglChooseConfig(s->dpy, attrs, cfgs, 64, &n);
	if (n == 0 && !janela) {
		/* Sem tela, qualquer configuração com GLES 3 serve. */
		EGLint min[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
		eglChooseConfig(s->dpy, min, cfgs, 64, &n);
	}
	if (n == 0) {
		snprintf(erro, sizeof(erro), "EGL: nenhuma configuração GLES 3");
		return false;
	}
	s->cfg = cfgs[0];
	if (janela) {
		/* A configuração tem que bater com o formato do GBM (XRGB8888). */
		for (EGLint i = 0; i < n; i++) {
			EGLint id;
			eglGetConfigAttrib(s->dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &id);
			if ((uint32_t)id == GBM_FORMAT_XRGB8888) {
				s->cfg = cfgs[i];
				break;
			}
		}
	}
	EGLint ctx[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1,
		EGL_NONE };
	s->ctx = eglCreateContext(s->dpy, janela ? s->cfg : EGL_NO_CONFIG_KHR,
	    EGL_NO_CONTEXT, ctx);
	if (s->ctx == EGL_NO_CONTEXT && !janela)
		s->ctx = eglCreateContext(s->dpy, s->cfg, EGL_NO_CONTEXT, ctx);
	if (s->ctx == EGL_NO_CONTEXT) {
		snprintf(erro, sizeof(erro), "EGL: sem contexto OpenGL ES 3.1 (0x%x)",
		    eglGetError());
		return false;
	}
	return true;
}

/* --- sem tela ----------------------------------------------------------- */

bool
saida_sem_tela(saida *s, int w, int h)
{
	memset(s, 0, sizeof(*s));
	s->w = w;
	s->h = h;
	if (!contexto(s, EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, false))
		return false;
	if (!eglMakeCurrent(s->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, s->ctx)) {
		snprintf(erro, sizeof(erro), "EGL: makeCurrent sem superfície");
		return false;
	}
	/* O destino: um framebuffer comum. */
	glGenRenderbuffers(1, &s->rb);
	glBindRenderbuffer(GL_RENDERBUFFER, s->rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
	glGenFramebuffers(1, &s->fb);
	glBindFramebuffer(GL_FRAMEBUFFER, s->fb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	    GL_RENDERBUFFER, s->rb);
	return true;
}

bool
saida_png(saida *s, const char *arquivo)
{
	unsigned char *px = malloc((size_t)s->w * s->h * 4);
	if (px == NULL)
		return false;
	glBindFramebuffer(GL_READ_FRAMEBUFFER, s->fb);
	glReadPixels(0, 0, s->w, s->h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	FILE *f = fopen(arquivo, "wb");
	if (f == NULL) {
		free(px);
		return false;
	}
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
	    NULL, NULL);
	png_infop info = png_create_info_struct(png);
	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(f);
		free(px);
		return false;
	}
	png_init_io(png, f);
	png_set_IHDR(png, info, s->w, s->h, 8, PNG_COLOR_TYPE_RGBA,
	    PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
	    PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);
	/* O OpenGL guarda de baixo para cima. */
	for (int y = s->h - 1; y >= 0; y--)
		png_write_row(png, px + (size_t)y * s->w * 4);
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	fclose(f);
	free(px);
	return true;
}

/* --- DRM/KMS ------------------------------------------------------------ */

struct buffer_fb {
	uint32_t fb;
};

static void
fb_destruir(struct gbm_bo *bo, void *dados)
{
	struct buffer_fb *b = dados;
	int fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	if (b->fb)
		drmModeRmFB(fd, b->fb);
	free(b);
}

static uint32_t
fb_de(saida *s, struct gbm_bo *bo)
{
	struct buffer_fb *b = gbm_bo_get_user_data(bo);
	if (b)
		return b->fb;
	b = calloc(1, sizeof(*b));
	uint32_t alca = gbm_bo_get_handle(bo).u32, passo = gbm_bo_get_stride(bo);
	if (drmModeAddFB(s->fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), 24,
	    32, passo, alca, &b->fb) != 0) {
		free(b);
		return 0;
	}
	gbm_bo_set_user_data(bo, b, fb_destruir);
	return b->fb;
}

static int
taxa(const drmModeModeInfo *m)
{
	/* mHz, como o kernel calcula. */
	return (int)((m->clock * 1000000LL / m->htotal + m->vtotal / 2) / m->vtotal);
}

bool
saida_drm(saida *s, const char *dispositivo)
{
	memset(s, 0, sizeof(*s));
	s->fd = open(dispositivo, O_RDWR | O_CLOEXEC);
	if (s->fd < 0) {
		snprintf(erro, sizeof(erro), "%s: %s", dispositivo, strerror(errno));
		return false;
	}
	drmModeRes *res = drmModeGetResources(s->fd);
	if (res == NULL) {
		snprintf(erro, sizeof(erro), "%s não é um dispositivo KMS (o driver de vídeo está carregado? lume video)",
		    dispositivo);
		return false;
	}
	drmModeConnector *con = NULL;
	for (int i = 0; i < res->count_connectors; i++) {
		con = drmModeGetConnector(s->fd, res->connectors[i]);
		if (con && con->connection == DRM_MODE_CONNECTED && con->count_modes > 0)
			break;
		drmModeFreeConnector(con);
		con = NULL;
	}
	if (con == NULL) {
		snprintf(erro, sizeof(erro), "nenhum monitor ligado");
		return false;
	}
	/* A resolução preferida, na maior taxa que o painel tem nela. */
	drmModeModeInfo *pref = &con->modes[0];
	for (int i = 0; i < con->count_modes; i++)
		if (con->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
			pref = &con->modes[i];
			break;
		}
	s->modo = *pref;
	for (int i = 0; i < con->count_modes; i++) {
		drmModeModeInfo *m = &con->modes[i];
		if (m->hdisplay == pref->hdisplay && m->vdisplay == pref->vdisplay &&
		    taxa(m) > taxa(&s->modo))
			s->modo = *m;
	}
	s->hz = taxa(&s->modo) / 1000.0f;
	s->conector = con->connector_id;
	/* O CRTC: o do encoder atual, ou o primeiro que serve. */
	drmModeEncoder *enc = con->encoder_id ? drmModeGetEncoder(s->fd, con->encoder_id) : NULL;
	if (enc && enc->crtc_id) {
		s->crtc = enc->crtc_id;
	} else {
		for (int e = 0; e < con->count_encoders && !s->crtc; e++) {
			drmModeEncoder *x = drmModeGetEncoder(s->fd, con->encoders[e]);
			for (int c = 0; x && c < res->count_crtcs; c++)
				if (x->possible_crtcs & (1u << c)) {
					s->crtc = res->crtcs[c];
					break;
				}
			drmModeFreeEncoder(x);
		}
	}
	drmModeFreeEncoder(enc);
	drmModeFreeConnector(con);
	drmModeFreeResources(res);
	if (!s->crtc) {
		snprintf(erro, sizeof(erro), "nenhum CRTC para o monitor");
		return false;
	}
	s->crtc_antes = drmModeGetCrtc(s->fd, s->crtc);
	s->w = s->modo.hdisplay;
	s->h = s->modo.vdisplay;

	s->gbm = gbm_create_device(s->fd);
	s->superficie = gbm_surface_create(s->gbm, s->w, s->h, GBM_FORMAT_XRGB8888,
	    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (s->superficie == NULL) {
		snprintf(erro, sizeof(erro), "GBM: não criou a superfície");
		return false;
	}
	if (!contexto(s, EGL_PLATFORM_GBM_KHR, s->gbm, true))
		return false;
	s->surf = eglCreateWindowSurface(s->dpy, s->cfg,
	    (EGLNativeWindowType)s->superficie, NULL);
	if (s->surf == EGL_NO_SURFACE || !eglMakeCurrent(s->dpy, s->surf, s->surf, s->ctx)) {
		snprintf(erro, sizeof(erro), "EGL: superfície da tela (0x%x)", eglGetError());
		return false;
	}
	return true;
}

static void
virou(int fd, unsigned int seq, unsigned int seg, unsigned int us, void *dados)
{
	(void)fd;
	(void)seq;
	(void)seg;
	(void)us;
	*(bool *)dados = false;
}

double
saida_agora_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

bool
saida_mostrar(saida *s)
{
	if (s->gbm == NULL)
		return true; /* sem tela: o quadro fica no framebuffer */
	eglSwapBuffers(s->dpy, s->surf);
	struct gbm_bo *bo = gbm_surface_lock_front_buffer(s->superficie);
	uint32_t fb = fb_de(s, bo);
	if (!fb) {
		snprintf(erro, sizeof(erro), "DRM: não criou o framebuffer");
		return false;
	}
	if (!s->modo_ligado) {
		if (drmModeSetCrtc(s->fd, s->crtc, fb, 0, 0, &s->conector, 1, &s->modo) != 0) {
			snprintf(erro, sizeof(erro), "DRM: não ligou o modo (outro programa usa a tela? rode num console sem o X)");
			return false;
		}
		s->modo_ligado = true;
	} else {
		bool esperando = true;
		if (drmModePageFlip(s->fd, s->crtc, fb, DRM_MODE_PAGE_FLIP_EVENT, &esperando) != 0) {
			snprintf(erro, sizeof(erro), "DRM: page flip: %s", strerror(errno));
			return false;
		}
		drmEventContext ev = { .version = 2, .page_flip_handler = virou };
		while (esperando) {
			struct pollfd p = { .fd = s->fd, .events = POLLIN };
			if (poll(&p, 1, 1000) <= 0)
				break;
			drmHandleEvent(s->fd, &ev);
		}
	}
	if (s->bo_anterior)
		gbm_surface_release_buffer(s->superficie, s->bo_anterior);
	s->bo_anterior = bo;
	return true;
}

void
saida_fechar(saida *s)
{
	if (s->crtc_antes) {
		drmModeCrtc *c = s->crtc_antes;
		drmModeSetCrtc(s->fd, c->crtc_id, c->buffer_id, c->x, c->y,
		    &s->conector, 1, &c->mode);
		drmModeFreeCrtc(c);
	}
	if (s->dpy != EGL_NO_DISPLAY && s->dpy) {
		eglMakeCurrent(s->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (s->surf)
			eglDestroySurface(s->dpy, s->surf);
		if (s->ctx)
			eglDestroyContext(s->dpy, s->ctx);
		eglTerminate(s->dpy);
	}
	if (s->superficie)
		gbm_surface_destroy(s->superficie);
	if (s->gbm)
		gbm_device_destroy(s->gbm);
	if (s->fd > 0)
		close(s->fd);
}
