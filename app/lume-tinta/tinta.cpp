// SPDX-License-Identifier: GPL-2.0-or-later
//
// lume_tinta - o filtro e-ink do LumeBSD, um efeito do KWin.
//
// Cada janela é desenhada em tons de papel e tinta: a cor vira luminância,
// uma curva de contraste deixa os pretos de tinta e os brancos de papel, e
// o resultado vai do tom da tinta ao do papel. Numa tela LCD, conteúdo claro
// e sem cores saturadas se lê melhor sob luz forte (as reflexões aparecem
// menos sobre um fundo claro) sem precisar aumentar o brilho.
//
// Fotos e vídeos ficam com as cores de verdade: as janelas dos visualizadores
// e players (e de editores de imagem e vídeo), e qualquer janela em tela
// cheia (um vídeo no navegador, um jogo), não passam pelo filtro.
//
// O papel e a tinta (do motor de renderização do LumeBSD, docs/MOTOR.md):
//   - grão de papel: fibras de celulose em ruído fixo na tela (não mexe
//     quando a janela mexe, como num painel de e-ink) clareiam e escurecem o
//     papel de leve e deixam as bordas da tinta orgânicas;
//   - viscosidade: a tela inteira guarda o quadro anterior e a imagem nova
//     assenta sobre ele com um decaimento exponencial (a tinta migrando), sem
//     o piscar do e-ink de verdade. Só nas janelas com o filtro: fotos, vídeos
//     e tela cheia mudam na hora. Precisa de placa de vídeo (sem aceleração,
//     o KWin desliga as animações, e a viscosidade também). Por enquanto
//     só no KWin do X11 (a sessão do LumeBSD): o do Wayland 6.7 mudou a API
//     de desenhar a tela.
//
// Atalhos: Ctrl+Meta+T liga e desliga o filtro na tela inteira; Ctrl+Meta+Y
// tira ou põe o filtro só na janela ativa. Configuração em kwinrc, grupo
// [Effect-lume_tinta]: Intensidade (0-100), Excecoes (classes de janela),
// Grao (0-100), Viscosidade (ms; 0 desliga).
//
// Sem moc: nenhuma classe daqui declara sinais ou slots (as conexões são
// lambdas), e os metadados do plugin vêm de metadata.json, convertidos para
// CBOR pelo ferramentas/montar-efeitos.py (o que o moc faria). Assim compila
// com o clang de fora do FreeBSD contra os cabeçalhos dos pacotes.

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/offscreeneffect.h"
#include "opengl/glframebuffer.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"

#include <KConfigGroup>
#include <KGlobalAccel>
#include <KSharedConfig>
#include <QAction>
#include <QPointer>
#include <QtPlugin>
#include <QSet>
#include <QVector3D>

#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

namespace KWin
{

// O mesmo esquema do efeito "Inverter" do KWin: a gestão de cor do KWin
// (colormanagement.glsl) leva a textura para a escala de gama 2.2, onde a
// conta é feita, e de volta.
static const char FRAGMENTO[] = R"GLSL(#version 140

#include "colormanagement.glsl"
#include "saturation.glsl"

uniform sampler2D sampler;
uniform vec4 modulation;
uniform vec3 papel;
uniform vec3 tinta;
uniform float intensidade;
uniform float grao;

in vec2 texcoord0;

out vec4 fragColor;

// Ruído de valor: fibras de papel compridas nas duas direções e um grão fino,
// presos ao pixel da tela (gl_FragCoord), não à janela.
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float ruido(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fibras(vec2 p)
{
    return 0.40 * ruido(p * vec2(0.07, 0.55))
         + 0.30 * ruido(p * vec2(0.55, 0.07))
         + 0.15 * ruido(p * 0.9)
         + 0.15 * hash(floor(p));
}

void main()
{
    vec4 tex = texture(sampler, texcoord0);
    tex = sourceEncodingToNitsInDestinationColorspace(tex);
    tex = adjustSaturation(tex);
    tex = nitsToEncoding(tex, gamma22_EOTF, 0.0, destinationReferenceLuminance);
    tex.rgb /= max(0.001, tex.a);

    // Luminância (Rec. 709) e a curva do e-ink: pretos de tinta, brancos de
    // papel, meios-tons um pouco comprimidos.
    float l = dot(tex.rgb, vec3(0.2126, 0.7152, 0.0722));
    l = smoothstep(0.03, 0.97, l);
    l = mix(l, l * l * (3.0 - 2.0 * l), 0.35);
    // O grão: nas bordas (meios-tons) a tinta avança ou recua um pouco pelas
    // fibras; no papel, o tom varia de leve. Pretos e brancos chapados ficam.
    float n = fibras(gl_FragCoord.xy) - 0.5;
    float borda = 4.0 * l * (1.0 - l);
    l = clamp(l + grao * 0.16 * n * borda, 0.0, 1.0);
    vec3 papelFibra = papel * (1.0 + grao * 0.045 * n);
    vec3 eink = mix(tinta, papelFibra, l);
    tex.rgb = mix(tex.rgb, eink, intensidade);

    tex *= modulation;
    tex.rgb *= tex.a;
    tex = encodingToNits(tex, gamma22_EOTF, 0.0, destinationReferenceLuminance);
    fragColor = nitsToDestinationEncoding(tex);
}
)GLSL";

// A viscosidade: o quadro novo (sampler) assenta sobre o anterior
// (historico, lido pelo pixel), na proporção "mistura" deste quadro.
static const char MISTURA[] = R"GLSL(#version 140

uniform sampler2D sampler;
uniform sampler2D historico;
uniform float mistura;

in vec2 texcoord0;

out vec4 fragColor;

void main()
{
    vec4 novo = texture(sampler, texcoord0);
    vec4 antes = texelFetch(historico, ivec2(gl_FragCoord.xy), 0);
    fragColor = mix(antes, novo, mistura);
}
)GLSL";

// Janelas que mostram fotos e vídeos, ou onde a cor é o trabalho: ficam
// com a cor de verdade. A classe da janela (windowClass) é comparada por
// pedaço, sem diferenciar maiúsculas.
static const char *EXCECOES_PADRAO[] = {
    // fotos
    "gwenview", "showfoto", "digikam", "darktable", "rawtherapee", "shotwell",
    "eog", "loupe", "nomacs", "qimgv", "feh", "sxiv", "nsxiv", "imv",
    // vídeo e música com capa
    "vlc", "mpv", "haruna", "celluloid", "totem", "smplayer", "kaffeine",
    "dragon", "elisa", "kodi",
    // edição de imagem, vídeo e 3D
    "gimp", "krita", "inkscape", "kolourpaint", "kdenlive", "shotcut",
    "openshot", "obs", "blender", "scribus", "darktable",
    // jogos
    "steam", "gamescope", "dosbox", "retroarch",
};

class Tinta : public OffscreenEffect
{
public:
    Tinta();
    ~Tinta() override = default;

    static bool supported()
    {
        return OffscreenEffect::supported() && effects->compositingType() == OpenGLCompositing;
    }

    void reconfigure(ReconfigureFlags flags) override;
#ifdef LUME_KWIN_X11
    void prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime) override;
    void paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen) override;
    void postPaintScreen() override;
#endif
    bool isActive() const override
    {
        return m_valido && m_ligado;
    }
    // Depois dos efeitos que mexem na geometria, antes dos que desenham
    // por cima da tela inteira.
    int requestedEffectChainPosition() const override
    {
        return 70;
    }

private:
    bool carregar();
    bool deveTingir(EffectWindow *w) const;
    void atualizar(EffectWindow *w);
    void atualizarTodas();
    void observar(EffectWindow *w);
#ifdef LUME_KWIN_X11
    bool viscosa() const;

    // Por tela: o quadro de agora e dois históricos (um lido, outro escrito).
    struct Tela {
        std::unique_ptr<GLTexture> atual, hist[2];
        std::unique_ptr<GLFramebuffer> fbAtual, fbHist[2];
        int lido = 0;
        bool limpa = true; // sem histórico: o próximo quadro entra inteiro
    };
    bool prepararTela(Tela &t, const RenderTarget &renderTarget);
    std::map<Output *, Tela> m_telas;
    float m_fracao = 1.0f;  // quanto do quadro novo entra neste quadro
    std::chrono::milliseconds m_ultimoQuadro{0};
    std::chrono::milliseconds m_ultimaMudanca{0};
    std::chrono::milliseconds m_agora{0};
    bool m_pedido = false;   // o repaint deste quadro fomos nós que pedimos
    bool m_assentar = false; // último quadro do decaimento: entra inteiro
#endif

    std::unique_ptr<GLShader> m_shader;
    std::unique_ptr<GLShader> m_mistura;
    float m_grao = 0.4f;
    int m_viscosidade = 35; // ms (a constante de tempo do decaimento)
    bool m_iniciado = false;
    bool m_valido = true;
    bool m_ligado = true;
    float m_intensidade = 1.0f;
    QStringList m_excecoes;
    QSet<EffectWindow *> m_trocadas; // Ctrl+Meta+Y: o contrário do padrão
    QSet<EffectWindow *> m_tingidas;
};

Tinta::Tinta()
{
    auto *tudo = new QAction(this);
    tudo->setAutoRepeat(false);
    tudo->setObjectName(QStringLiteral("LumeTinta"));
    tudo->setText(QStringLiteral("Filtro e-ink: ligar ou desligar"));
    KGlobalAccel::self()->setDefaultShortcut(tudo, QList<QKeySequence>() << (Qt::CTRL | Qt::META | Qt::Key_T));
    KGlobalAccel::self()->setShortcut(tudo, QList<QKeySequence>() << (Qt::CTRL | Qt::META | Qt::Key_T));
    connect(tudo, &QAction::triggered, this, [this] {
        m_ligado = !m_ligado;
        atualizarTodas();
    });

    auto *janela = new QAction(this);
    janela->setAutoRepeat(false);
    janela->setObjectName(QStringLiteral("LumeTintaJanela"));
    janela->setText(QStringLiteral("Filtro e-ink: só nesta janela"));
    KGlobalAccel::self()->setDefaultShortcut(janela, QList<QKeySequence>() << (Qt::CTRL | Qt::META | Qt::Key_Y));
    KGlobalAccel::self()->setShortcut(janela, QList<QKeySequence>() << (Qt::CTRL | Qt::META | Qt::Key_Y));
    connect(janela, &QAction::triggered, this, [this] {
        EffectWindow *w = effects->activeWindow();
        if (!w) {
            return;
        }
        if (m_trocadas.contains(w)) {
            m_trocadas.remove(w);
        } else {
            m_trocadas.insert(w);
        }
        atualizar(w);
    });

    connect(effects, &EffectsHandler::windowAdded, this, [this](EffectWindow *w) {
        observar(w);
        atualizar(w);
    });
    connect(effects, &EffectsHandler::windowClosed, this, [this](EffectWindow *w) {
        m_trocadas.remove(w);
        m_tingidas.remove(w);
    });

    reconfigure(ReconfigureAll);
    const auto janelas = effects->stackingOrder();
    for (EffectWindow *w : janelas) {
        observar(w);
    }
    atualizarTodas();
}

void Tinta::observar(EffectWindow *w)
{
    // Um vídeo que vai para a tela cheia sai do filtro, e volta depois.
    connect(w, &EffectWindow::windowFullScreenChanged, this, [this](EffectWindow *janela) {
        atualizar(janela);
    });
}

void Tinta::reconfigure(ReconfigureFlags)
{
    KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-lume_tinta"));
    m_intensidade = qBound(0, g.readEntry("Intensidade", 100), 100) / 100.0f;
    m_excecoes.clear();
    for (const char *e : EXCECOES_PADRAO) {
        m_excecoes << QString::fromLatin1(e);
    }
    m_excecoes << g.readEntry("Excecoes", QStringList());
    m_grao = qBound(0, g.readEntry("Grao", 40), 100) / 100.0f;
    m_viscosidade = qBound(0, g.readEntry("Viscosidade", 35), 500);
    if (m_shader) {
        ShaderManager::instance()->pushShader(m_shader.get());
        m_shader->setUniform("intensidade", m_intensidade);
        m_shader->setUniform("grao", m_grao);
        ShaderManager::instance()->popShader();
    }
#ifdef LUME_KWIN_X11
    for (auto &[tela, t] : m_telas) {
        t.limpa = true;
    }
#endif
    if (m_iniciado) {
        atualizarTodas();
    }
}

bool Tinta::carregar()
{
    m_iniciado = true;
    m_shader = ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, QByteArray(), QByteArray(FRAGMENTO));
    if (!m_shader) {
        m_shader.reset();
        return false;
    }
    ShaderManager::instance()->pushShader(m_shader.get());
    // O papel e a tinta do tema Papel, um pouco mais quentes.
    m_shader->setUniform("papel", QVector3D(240 / 255.0f, 236 / 255.0f, 226 / 255.0f));
    m_shader->setUniform("tinta", QVector3D(28 / 255.0f, 27 / 255.0f, 25 / 255.0f));
    m_shader->setUniform("intensidade", m_intensidade);
    m_shader->setUniform("grao", m_grao);
    ShaderManager::instance()->popShader();
    // Sem o shader da viscosidade o filtro funciona do mesmo jeito.
    m_mistura = ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, QByteArray(), QByteArray(MISTURA));
    return true;
}

#ifdef LUME_KWIN_X11
// A viscosidade vale com o filtro ligado, com placa de vídeo (o KWin diz se
// anima) e sem nada em tela cheia (um vídeo ou um jogo mudam na hora).
bool Tinta::viscosa() const
{
    if (m_viscosidade <= 0 || !m_mistura || !m_ligado || !effects->animationsSupported()) {
        return false;
    }
    if (effects->hasActiveFullScreenEffect()) {
        return false;
    }
    const auto janelas = effects->stackingOrder();
    for (EffectWindow *w : janelas) {
        if (w->isFullScreen() && w->isOnCurrentDesktop() && !w->isMinimized()) {
            return false;
        }
    }
    return true;
}

void Tinta::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    m_agora = presentTime;
    if (viscosa()) {
        // A tela inteira passa pela textura: o KWin redesenha tudo.
        data.mask |= PAINT_SCREEN_TRANSFORMED;
        if (!m_pedido) {
            // Uma mudança de verdade: começa (ou recomeça) a assentar.
            m_ultimaMudanca = presentTime;
            m_assentar = false;
        }
        // O intervalo entre quadros; depois de um tempo parada, a tela conta
        // como um quadro de 60 Hz (o primeiro quadro novo não entra inteiro).
        double dt = double((presentTime - m_ultimoQuadro).count());
        if (m_ultimoQuadro.count() == 0 || dt > 50.0) {
            dt = 1000.0 / 60.0;
        }
        m_fracao = m_assentar ? 1.0f : float(1.0 - std::exp(-std::max(dt, 1.0) / m_viscosidade));
    } else {
        for (auto &[tela, t] : m_telas) {
            t.limpa = true;
        }
    }
    m_ultimoQuadro = presentTime;
    m_pedido = false;
    effects->prePaintScreen(data, presentTime);
}

bool Tinta::prepararTela(Tela &t, const RenderTarget &renderTarget)
{
    const QSize tamanho = renderTarget.size();
    const GLenum formato = renderTarget.colorDescription() == ColorDescription::sRGB ? GL_RGBA8 : GL_RGBA16F;
    if (!t.atual || t.atual->size() != tamanho || t.atual->internalFormat() != formato) {
        t = Tela();
        auto nova = [&](std::unique_ptr<GLTexture> &tex, std::unique_ptr<GLFramebuffer> &fb) {
            tex = GLTexture::allocate(formato, tamanho);
            if (!tex) {
                return false;
            }
            tex->setFilter(GL_NEAREST);
            tex->setWrapMode(GL_CLAMP_TO_EDGE);
            fb = std::make_unique<GLFramebuffer>(tex.get());
            return fb->valid();
        };
        if (!nova(t.atual, t.fbAtual) || !nova(t.hist[0], t.fbHist[0]) || !nova(t.hist[1], t.fbHist[1])) {
            t = Tela();
            return false;
        }
    }
    for (auto *tex : {t.atual.get(), t.hist[0].get(), t.hist[1].get()}) {
        tex->setContentTransform(renderTarget.transform());
    }
    return true;
}

void Tinta::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport, int mask, const QRegion &region, Output *screen)
{
    if (!viscosa()) {
        effects->paintScreen(renderTarget, viewport, mask, region, screen);
        return;
    }
    Tela &t = m_telas[screen];
    if (!prepararTela(t, renderTarget)) {
        effects->paintScreen(renderTarget, viewport, mask, region, screen);
        return;
    }
    const double escala = viewport.scale();
    const QRectF tela = viewport.renderRect();
    QMatrix4x4 posicao;
    posicao.translate(tela.x() * escala, tela.y() * escala);

    // 1. A cena (com o filtro de cada janela) no quadro de agora.
    RenderTarget alvoAtual(t.fbAtual.get(), renderTarget.colorDescription());
    RenderViewport vistaAtual(tela, escala, alvoAtual);
    GLFramebuffer::pushFramebuffer(t.fbAtual.get());
    effects->paintScreen(alvoAtual, vistaAtual, mask, region, screen);
    GLFramebuffer::popFramebuffer();

    // 2. O quadro de agora assenta sobre o histórico: lido -> escrito.
    const int escrito = 1 - t.lido;
    RenderTarget alvoHist(t.fbHist[escrito].get(), renderTarget.colorDescription());
    RenderViewport vistaHist(tela, escala, alvoHist);
    GLFramebuffer::pushFramebuffer(t.fbHist[escrito].get());
    ShaderManager::instance()->pushShader(m_mistura.get());
    m_mistura->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, vistaHist.projectionMatrix() * posicao);
    m_mistura->setUniform("historico", 1);
    m_mistura->setUniform("mistura", t.limpa ? 1.0f : m_fracao);
    glActiveTexture(GL_TEXTURE1);
    t.hist[t.lido]->bind();
    glActiveTexture(GL_TEXTURE0);
    t.atual->render(tela.size() * escala);
    glActiveTexture(GL_TEXTURE1);
    t.hist[t.lido]->unbind();
    glActiveTexture(GL_TEXTURE0);
    ShaderManager::instance()->popShader();

    // 3. Janelas sem o filtro (fotos, vídeos, Ctrl+Meta+Y) mudam na hora:
    //    o quadro de agora entra inteiro no retângulo delas.
    QRegion naHora;
    const auto janelas = effects->stackingOrder();
    for (EffectWindow *w : janelas) {
        if (w->isVisible() && !w->isDeleted() && !m_tingidas.contains(w) && !w->isDesktop() && !w->isDock()) {
            naHora += w->frameGeometry().toAlignedRect();
        }
    }
    if (!naHora.isEmpty()) {
        GLShader *simples = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture);
        simples->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, vistaHist.projectionMatrix() * posicao);
        glEnable(GL_SCISSOR_TEST);
        const int altura = alvoHist.size().height();
        for (const QRect &r : naHora) {
            const QRect d = vistaHist.mapToRenderTarget(r);
            glScissor(d.x(), altura - d.y() - d.height(), d.width(), d.height());
            t.atual->render(tela.size() * escala);
        }
        glDisable(GL_SCISSOR_TEST);
        ShaderManager::instance()->popShader();
    }
    GLFramebuffer::popFramebuffer();
    t.lido = escrito;
    t.limpa = false;

    // 4. O histórico novo vai para a tela.
    GLShader *saida = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
    saida->setColorspaceUniforms(renderTarget.colorDescription(), renderTarget.colorDescription(), RenderingIntent::Perceptual);
    saida->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix() * posicao);
    t.hist[t.lido]->render(tela.size() * escala);
    ShaderManager::instance()->popShader();
}

void Tinta::postPaintScreen()
{
    // Enquanto a tinta assenta (umas seis constantes de tempo depois da
    // última mudança), mais um quadro; no fim, um quadro que entra inteiro.
    if (viscosa()) {
        const auto passou = m_agora - m_ultimaMudanca;
        if (passou.count() < 6 * m_viscosidade) {
            m_assentar = false;
            m_pedido = true;
            effects->addRepaintFull();
        } else if (!m_assentar) {
            m_assentar = true;
            m_pedido = true;
            effects->addRepaintFull();
        }
    }
    effects->postPaintScreen();
}
#endif

bool Tinta::deveTingir(EffectWindow *w) const
{
    bool padrao = m_ligado;
    if (padrao) {
        if (w->isFullScreen()) {
            padrao = false;
        } else {
            const QString classe = w->windowClass().toLower();
            for (const QString &e : m_excecoes) {
                if (!e.isEmpty() && classe.contains(e.toLower())) {
                    padrao = false;
                    break;
                }
            }
        }
    }
    return padrao != m_trocadas.contains(w);
}

void Tinta::atualizar(EffectWindow *w)
{
    if (m_valido && !m_iniciado) {
        m_valido = carregar();
    }
    if (!m_valido) {
        return;
    }
    const bool tingir = deveTingir(w);
    if (tingir && !m_tingidas.contains(w)) {
        redirect(w);
        setShader(w, m_shader.get());
        m_tingidas.insert(w);
    } else if (!tingir && m_tingidas.contains(w)) {
        unredirect(w);
        m_tingidas.remove(w);
    }
    w->addRepaintFull();
}

void Tinta::atualizarTodas()
{
    const auto janelas = effects->stackingOrder();
    for (EffectWindow *w : janelas) {
        atualizar(w);
    }
    effects->addRepaintFull();
}

class TintaFabrica : public EffectPluginFactory
{
public:
    bool isSupported() const override
    {
        return Tinta::supported();
    }
    bool enabledByDefault() const override
    {
        return true;
    }
    Effect *createEffect() const override
    {
        return new Tinta();
    }
    // O KPluginFactory é uma "interface" do Qt (Q_DECLARE_INTERFACE): o
    // qobject_cast pergunta ao objeto, pelo nome, se ele é um
    // "org.kde.KPluginFactory". O moc escreveria esta resposta (o
    // Q_INTERFACES das fábricas do KWin); sem o moc, ela fica aqui.
    void *qt_metacast(const char *nome) override
    {
        if (nome && std::strcmp(nome, KPluginFactory_iid) == 0) {
            return static_cast<KPluginFactory *>(this);
        }
        return EffectPluginFactory::qt_metacast(nome);
    }
};

} // namespace KWin

// Os metadados do plugin (IID, classe e o metadata.json em CBOR), gerados
// pelo montar-efeitos.py no lugar do moc.
#include "metadados.inc"
QT_MOC_EXPORT_PLUGIN_V2(KWin::TintaFabrica, TintaFabrica, qt_pluginMetaDataV2_TintaFabrica)
