/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * O renderizador do lume-motor, em OpenGL ES 3.1.
 *
 * Por quadro:
 *   1. tinta: um retângulo por caminho (instanciado); o fragment shader
 *      calcula, pelas equações das curvas, quanto do pixel está dentro do
 *      caminho (regra do número de voltas, cobertura horizontal exata em
 *      quatro sublinhas). Nada de tesselação nem atlas de fontes: as curvas
 *      ficam num buffer da GPU, escritas em anel (sem copiar bitmaps);
 *   2. papel: as fibras de celulose, geradas uma vez por resolução num
 *      compute shader, e a tinta sobre elas (bordas orgânicas);
 *   3. viscosidade: o quadro novo assenta sobre o anterior com decaimento
 *      exponencial (constante de tempo em ms), sem o piscar do e-ink;
 *   4. o resultado vai para o framebuffer de quem chamou (a tela, pelo
 *      DRM/KMS, ou um PNG).
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLES3/gl31.h>

#include "motor.h"
/* As contas do papel (papel.glsl), embutidas pelo lume-montar (ferramentas/montar motor). */
#include "papel_glsl.inc"

static char erro[512];

const char *
motor_erro(void)
{
	return erro;
}

#define GLSL "#version 310 es\nprecision highp float;\nprecision highp int;\n"

/* 1. Tinta: cobertura analítica. */
static const char VS_TINTA[] = GLSL
    "layout(location = 0) in vec4 caixa;\n"
    "layout(location = 1) in uvec2 info;\n"
    "layout(location = 2) in vec4 tinta;\n"
    "uniform vec3 vista;  // escala, dx, dy\n"
    "uniform vec2 tela;\n"
    "flat out uvec2 vInfo;\n"
    "flat out vec4 vTinta;\n"
    "out vec2 vDoc;\n"
    "void main() {\n"
    "    int k = gl_VertexID;\n"
    "    float m = 1.5 / vista.x;\n"
    "    vec2 c = vec2((k == 1 || k == 2 || k == 4) ? caixa.z + m : caixa.x - m,\n"
    "                  (k == 2 || k == 4 || k == 5) ? caixa.w + m : caixa.y - m);\n"
    "    vDoc = c; vInfo = info; vTinta = tinta;\n"
    "    vec2 px = c * vista.x + vista.yz;\n"
    "    gl_Position = vec4(px.x / tela.x * 2.0 - 1.0, 1.0 - px.y / tela.y * 2.0, 0.0, 1.0);\n"
    "}\n";

static const char FS_TINTA[] = GLSL
    "layout(std430, binding = 0) readonly buffer Curvas { vec4 q[]; };\n"
    "uniform float pixel;  // tamanho do pixel no documento\n"
    "flat in uvec2 vInfo;\n"
    "flat in vec4 vTinta;\n"
    "in vec2 vDoc;\n"
    "out vec4 cor;\n"
    "// Soma dos cruzamentos à direita na linha y (curvas monótonas em y):\n"
    "// a fração do pixel à esquerda do cruzamento conta.\n"
    "float voltas(vec2 p) {\n"
    "    float w = 0.0;\n"
    "    for (uint i = 0u; i < vInfo.y; i++) {\n"
    "        uint j = 2u * (vInfo.x + i);\n"
    "        vec4 a = q[j]; vec2 p2 = q[j + 1u].xy;\n"
    "        vec2 p0 = a.xy, p1 = a.zw;\n"
    "        if (p.y < min(p0.y, p2.y) || p.y >= max(p0.y, p2.y)) continue;\n"
    "        float A = p0.y - 2.0 * p1.y + p2.y, B = 2.0 * (p1.y - p0.y), C = p0.y - p.y;\n"
    "        float t;\n"
    "        if (abs(A) < 1e-5 * max(1.0, abs(B))) {\n"
    "            t = -C / B;\n"
    "        } else {\n"
    "            float d = sqrt(max(B * B - 4.0 * A * C, 0.0));\n"
    "            float t1 = (-B + d) / (2.0 * A), t2 = (-B - d) / (2.0 * A);\n"
    "            t = (t1 >= -1e-4 && t1 <= 1.0001) ? t1 : t2;\n"
    "        }\n"
    "        t = clamp(t, 0.0, 1.0);\n"
    "        float x = mix(mix(p0.x, p1.x, t), mix(p1.x, p2.x, t), t);\n"
    "        w += (p2.y > p0.y ? 1.0 : -1.0) * clamp((x - p.x) / pixel + 0.5, 0.0, 1.0);\n"
    "    }\n"
    "    return w;\n"
    "}\n"
    "void main() {\n"
    "    float c = 0.0;\n"
    "    for (int s = 0; s < 4; s++) {\n"
    "        float dy = (float(s) - 1.5) * 0.25 * pixel;\n"
    "        c += min(abs(voltas(vDoc + vec2(0.0, dy))), 1.0);\n"
    "    }\n"
    "    float a = c * 0.25 * vTinta.a;\n"
    "    if (a <= 0.0) discard;\n"
    "    cor = vec4(vTinta.rgb * a, a);\n"
    "}\n";

/* 2a. Fibras do papel (compute shader). */
static const char CS_FIBRAS[] = GLSL
    "layout(local_size_x = 8, local_size_y = 8) in;\n"
    "layout(r32f, binding = 0) writeonly uniform highp image2D fibras;\n"
    LUME_PAPEL_GLSL
    "void main() {\n"
    "    ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
    "    if (any(greaterThanEqual(p, imageSize(fibras)))) return;\n"
    "    vec2 v = vec2(p);\n"
    "    imageStore(fibras, p, vec4(lume_fibras(v), 0.0, 0.0, 1.0));\n"
    "}\n";

/* Um triângulo que cobre a tela. */
static const char VS_TELA[] = GLSL
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* 2b. Papel e tinta. */
static const char FS_PAPEL[] = GLSL
    "uniform sampler2D tintaTex;\n"
    "uniform highp sampler2D fibrasTex;\n"
    "uniform vec3 papel;\n"
    "uniform vec3 preta;\n"
    "uniform float grao;\n"
    "uniform int colorido;\n"
    "out vec4 cor;\n"
    LUME_PAPEL_GLSL
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    vec4 acc = texelFetch(tintaTex, p, 0);\n"
    "    float n = texelFetch(fibrasTex, p, 0).r - 0.5;\n"
    "    float a = lume_grao(acc.a, n, grao);\n"
    "    vec3 folha = lume_folha(papel, n, grao);\n"
    "    vec3 t = preta;\n"
    "    if (colorido != 0 && acc.a > 0.002) {\n"
    "        t = acc.rgb / acc.a;\n"
    "        float l = dot(t, vec3(0.2126, 0.7152, 0.0722));\n"
    "        t = mix(mix(vec3(l), t, 0.6), preta, 0.15);  // pigmento de e-ink colorido\n"
    "    }\n"
    "    cor = vec4(mix(folha, t, a), 1.0);\n"
    "}\n";

/* 3. Viscosidade. */
static const char FS_VISCOSO[] = GLSL
    "uniform sampler2D atual;\n"
    "uniform sampler2D antes;\n"
    "uniform float f;\n"
    "out vec4 cor;\n"
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    cor = mix(texelFetch(antes, p, 0), texelFetch(atual, p, 0), f);\n"
    "}\n";

struct motor {
	int w, h;
	motor_opcoes o;
	GLuint p_tinta, p_fibras, p_papel, p_viscoso;
	GLuint vao, vbo_caminhos, ssbo;
	GLsizeiptr ssbo_tam, ssbo_cursor, ssbo_inicio, ssbo_usado;
	GLint ssbo_alinhamento;
	uint32_t ncaminhos;
	GLuint tex_tinta, tex_fibras, tex_atual, tex_hist[2];
	GLuint fb_tinta, fb_atual, fb_hist[2];
	int lido;
	bool limpo;
	float desde_mudanca;
	float vista[3];
	bool parado;
};

static GLuint
compilar(GLenum tipo, const char *fonte)
{
	GLuint s = glCreateShader(tipo);
	glShaderSource(s, 1, &fonte, NULL);
	glCompileShader(s);
	GLint ok;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof(erro), NULL, erro);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

static GLuint
programa(const char *vs, const char *fs, const char *cs)
{
	GLuint p = glCreateProgram();
	GLuint a = 0, b = 0;
	if (cs) {
		if (!(a = compilar(GL_COMPUTE_SHADER, cs)))
			return 0;
		glAttachShader(p, a);
	} else {
		if (!(a = compilar(GL_VERTEX_SHADER, vs)) ||
		    !(b = compilar(GL_FRAGMENT_SHADER, fs)))
			return 0;
		glAttachShader(p, a);
		glAttachShader(p, b);
	}
	glLinkProgram(p);
	GLint ok;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	glDeleteShader(a);
	if (b)
		glDeleteShader(b);
	if (!ok) {
		glGetProgramInfoLog(p, sizeof(erro), NULL, erro);
		return 0;
	}
	return p;
}

static GLuint
textura(GLenum formato, int w, int h)
{
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexStorage2D(GL_TEXTURE_2D, 1, formato, w, h);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

static GLuint
framebuffer(GLuint tex)
{
	GLuint f;
	glGenFramebuffers(1, &f);
	glBindFramebuffer(GL_FRAMEBUFFER, f);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	    GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		snprintf(erro, sizeof(erro), "framebuffer incompleto");
		return 0;
	}
	return f;
}

motor *
motor_criar(int w, int h, const motor_opcoes *o)
{
	motor *m = calloc(1, sizeof(*m));
	if (m == NULL)
		return NULL;
	m->w = w;
	m->h = h;
	m->o = *o;
	if (!(m->p_tinta = programa(VS_TINTA, FS_TINTA, NULL)) ||
	    !(m->p_fibras = programa(NULL, NULL, CS_FIBRAS)) ||
	    !(m->p_papel = programa(VS_TELA, FS_PAPEL, NULL)) ||
	    !(m->p_viscoso = programa(VS_TELA, FS_VISCOSO, NULL))) {
		free(m);
		return NULL;
	}

	m->tex_tinta = textura(GL_RGBA8, w, h);
	m->tex_fibras = textura(GL_R32F, w, h);
	m->tex_atual = textura(GL_RGBA8, w, h);
	m->tex_hist[0] = textura(GL_RGBA8, w, h);
	m->tex_hist[1] = textura(GL_RGBA8, w, h);
	if (!(m->fb_tinta = framebuffer(m->tex_tinta)) ||
	    !(m->fb_atual = framebuffer(m->tex_atual)) ||
	    !(m->fb_hist[0] = framebuffer(m->tex_hist[0])) ||
	    !(m->fb_hist[1] = framebuffer(m->tex_hist[1]))) {
		free(m);
		return NULL;
	}

	/* As fibras: uma vez por resolução, presas ao pixel da tela. */
	glUseProgram(m->p_fibras);
	glBindImageTexture(0, m->tex_fibras, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
	glDispatchCompute((GLuint)(w + 7) / 8, (GLuint)(h + 7) / 8, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	/* As curvas num anel de 16 MB na GPU. */
	glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &m->ssbo_alinhamento);
	if (m->ssbo_alinhamento < 1)
		m->ssbo_alinhamento = 256;
	m->ssbo_tam = 16 << 20;
	glGenBuffers(1, &m->ssbo);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m->ssbo);
	glBufferData(GL_SHADER_STORAGE_BUFFER, m->ssbo_tam, NULL, GL_STREAM_DRAW);

	glGenVertexArrays(1, &m->vao);
	glBindVertexArray(m->vao);
	glGenBuffers(1, &m->vbo_caminhos);
	glBindBuffer(GL_ARRAY_BUFFER, m->vbo_caminhos);
	const GLsizei passo = sizeof(motor_caminho);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, passo,
	    (void *)offsetof(motor_caminho, caixa));
	glVertexAttribDivisor(0, 1);
	glEnableVertexAttribArray(1);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_INT, passo,
	    (void *)offsetof(motor_caminho, primeira));
	glVertexAttribDivisor(1, 1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, passo,
	    (void *)offsetof(motor_caminho, tinta));
	glVertexAttribDivisor(2, 1);
	glBindVertexArray(0);

	m->limpo = true;
	return m;
}

void
motor_destruir(motor *m)
{
	if (m == NULL)
		return;
	GLuint fbs[] = { m->fb_tinta, m->fb_atual, m->fb_hist[0], m->fb_hist[1] };
	GLuint texs[] = { m->tex_tinta, m->tex_fibras, m->tex_atual,
		m->tex_hist[0], m->tex_hist[1] };
	glDeleteFramebuffers(4, fbs);
	glDeleteTextures(5, texs);
	glDeleteBuffers(1, &m->ssbo);
	glDeleteBuffers(1, &m->vbo_caminhos);
	glDeleteVertexArrays(1, &m->vao);
	glDeleteProgram(m->p_tinta);
	glDeleteProgram(m->p_fibras);
	glDeleteProgram(m->p_papel);
	glDeleteProgram(m->p_viscoso);
	free(m);
}

void
motor_cena_enviar(motor *m, const motor_cena *c)
{
	/* As curvas vão para o anel: escreve depois do que a GPU pode estar
	 * lendo, sem esperar por ela; quando o anel acaba, recomeça num buffer
	 * novo (o driver troca por baixo). */
	GLsizeiptr tam = (GLsizeiptr)c->nquads * 8 * sizeof(float);
	if (tam == 0)
		tam = 8 * sizeof(float);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m->ssbo);
	GLsizeiptr ini = (m->ssbo_cursor + m->ssbo_alinhamento - 1) /
	    m->ssbo_alinhamento * m->ssbo_alinhamento;
	if (ini + tam > m->ssbo_tam) {
		if (tam > m->ssbo_tam) {
			m->ssbo_tam = tam * 2;
		}
		glBufferData(GL_SHADER_STORAGE_BUFFER, m->ssbo_tam, NULL, GL_STREAM_DRAW);
		ini = 0;
	}
	float *dst = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, ini, tam,
	    GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
	if (dst != NULL) {
		for (uint32_t i = 0; i < c->nquads; i++) {
			const motor_quad *q = &c->quads[i];
			float *d = dst + 8 * i;
			d[0] = q->x0; d[1] = q->y0; d[2] = q->x1; d[3] = q->y1;
			d[4] = q->x2; d[5] = q->y2; d[6] = 0.0f; d[7] = 0.0f;
		}
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
	}
	m->ssbo_inicio = ini;
	m->ssbo_usado = tam;
	m->ssbo_cursor = ini + tam;

	glBindBuffer(GL_ARRAY_BUFFER, m->vbo_caminhos);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)c->ncaminhos * sizeof(motor_caminho),
	    c->caminhos, GL_STREAM_DRAW);
	m->ncaminhos = c->ncaminhos;
	m->desde_mudanca = 0.0f;
	m->parado = false;
}

void
motor_reiniciar_historico(motor *m)
{
	m->limpo = true;
}

bool
motor_parado(const motor *m)
{
	return m->parado;
}

void
motor_quadro(motor *m, float escala, float dx, float dy, float dt_ms)
{
	GLint destino;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destino);
	if (escala != m->vista[0] || dx != m->vista[1] || dy != m->vista[2]) {
		m->vista[0] = escala;
		m->vista[1] = dx;
		m->vista[2] = dy;
		m->desde_mudanca = 0.0f;
		m->parado = false;
	}
	glViewport(0, 0, m->w, m->h);
	glDisable(GL_DEPTH_TEST);

	/* 1. Tinta. */
	glBindFramebuffer(GL_FRAMEBUFFER, m->fb_tinta);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(m->p_tinta);
	glUniform3f(glGetUniformLocation(m->p_tinta, "vista"), escala, dx, dy);
	glUniform2f(glGetUniformLocation(m->p_tinta, "tela"), (float)m->w, (float)m->h);
	glUniform1f(glGetUniformLocation(m->p_tinta, "pixel"), 1.0f / escala);
	glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, m->ssbo, m->ssbo_inicio,
	    m->ssbo_usado);
	glBindVertexArray(m->vao);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)m->ncaminhos);
	glBindVertexArray(0);
	glDisable(GL_BLEND);

	/* 2. Papel e tinta. */
	glBindFramebuffer(GL_FRAMEBUFFER, m->fb_atual);
	glUseProgram(m->p_papel);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m->tex_tinta);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m->tex_fibras);
	glUniform1i(glGetUniformLocation(m->p_papel, "tintaTex"), 0);
	glUniform1i(glGetUniformLocation(m->p_papel, "fibrasTex"), 1);
	glUniform3fv(glGetUniformLocation(m->p_papel, "papel"), 1, m->o.papel);
	glUniform3f(glGetUniformLocation(m->p_papel, "preta"), 28 / 255.0f,
	    27 / 255.0f, 25 / 255.0f);
	glUniform1f(glGetUniformLocation(m->p_papel, "grao"), m->o.grao);
	glUniform1i(glGetUniformLocation(m->p_papel, "colorido"), m->o.colorido);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	/* 3. Viscosidade: quanto do quadro novo entra agora. Depois de seis
	 *    constantes de tempo sem mudança, um quadro inteiro (assentou). */
	float f = 1.0f;
	if (!m->limpo && m->o.viscosidade_ms > 0.0f) {
		m->desde_mudanca += dt_ms;
		if (m->desde_mudanca < 6.0f * m->o.viscosidade_ms)
			f = 1.0f - expf(-fmaxf(dt_ms, 0.5f) / m->o.viscosidade_ms);
		else
			m->parado = true;
	} else {
		m->parado = m->o.viscosidade_ms <= 0.0f;
	}
	int escrito = 1 - m->lido;
	glBindFramebuffer(GL_FRAMEBUFFER, m->fb_hist[escrito]);
	glUseProgram(m->p_viscoso);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m->tex_atual);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m->tex_hist[m->lido]);
	glUniform1i(glGetUniformLocation(m->p_viscoso, "atual"), 0);
	glUniform1i(glGetUniformLocation(m->p_viscoso, "antes"), 1);
	glUniform1f(glGetUniformLocation(m->p_viscoso, "f"), f);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glActiveTexture(GL_TEXTURE0);
	m->lido = escrito;
	m->limpo = false;

	/* 4. Para o destino. */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m->fb_hist[m->lido]);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destino);
	glBlitFramebuffer(0, 0, m->w, m->h, 0, 0, m->w, m->h,
	    GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)destino);
}
