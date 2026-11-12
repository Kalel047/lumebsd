// SPDX-License-Identifier: BSD-2-Clause
//
// O papel do LumeBSD: as fibras de celulose e o grão da tinta, uma fonte só
// para o lume-motor (o compute shader das fibras e o shader do papel) e o
// filtro e-ink do KWin (lume-tinta). Os construtores a embutem como texto
// (papel_glsl.inc) no meio dos shaders; só funções, sem #version.
//
// As fibras ficam presas ao pixel da tela: ruído de valor, compridas nas
// duas direções, mais um grão fino.

float lume_hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float lume_ruido(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(lume_hash(i), lume_hash(i + vec2(1.0, 0.0)), f.x),
               mix(lume_hash(i + vec2(0.0, 1.0)), lume_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

// 0..1, preso ao pixel p (gl_FragCoord.xy, ou o pixel do compute shader).
float lume_fibras(vec2 p)
{
    return 0.40 * lume_ruido(p * vec2(0.07, 0.55))
         + 0.30 * lume_ruido(p * vec2(0.55, 0.07))
         + 0.15 * lume_ruido(p * 0.9)
         + 0.15 * lume_hash(floor(p));
}

// O grão na borda da tinta: x é a cobertura (de tinta, ou de papel, tanto
// faz: a conta é simétrica) e n = fibras - 0.5. Nos meios-tons a tinta avança
// ou recua um pouco pelas fibras; chapados (0 e 1) ficam.
float lume_grao(float x, float n, float grao)
{
    return clamp(x + grao * 0.16 * n * 4.0 * x * (1.0 - x), 0.0, 1.0);
}

// O papel com as fibras: o tom varia de leve.
vec3 lume_folha(vec3 papel, float n, float grao)
{
    return papel * (1.0 + grao * 0.045 * n);
}
