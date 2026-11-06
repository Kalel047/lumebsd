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
// Atalhos: Ctrl+Meta+T liga e desliga o filtro na tela inteira; Ctrl+Meta+Y
// tira ou põe o filtro só na janela ativa. Configuração em kwinrc, grupo
// [Effect-lume_tinta]: Intensidade (0-100), Excecoes (classes de janela).
//
// Sem moc: nenhuma classe daqui declara sinais ou slots (as conexões são
// lambdas), e os metadados do plugin vêm de metadata.json, convertidos para
// CBOR pelo ferramentas/montar-efeitos.py (o que o moc faria). Assim compila
// com o clang de fora do FreeBSD contra os cabeçalhos dos pacotes.

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "effect/offscreeneffect.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"

#include <KConfigGroup>
#include <KGlobalAccel>
#include <KSharedConfig>
#include <QAction>
#include <QPointer>
#include <QtPlugin>
#include <QSet>
#include <QVector3D>

#include <cstring>
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

in vec2 texcoord0;

out vec4 fragColor;

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
    vec3 eink = mix(tinta, papel, l);
    tex.rgb = mix(tex.rgb, eink, intensidade);

    tex *= modulation;
    tex.rgb *= tex.a;
    tex = encodingToNits(tex, gamma22_EOTF, 0.0, destinationReferenceLuminance);
    fragColor = nitsToDestinationEncoding(tex);
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

    std::unique_ptr<GLShader> m_shader;
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
    if (m_shader) {
        ShaderManager::instance()->pushShader(m_shader.get());
        m_shader->setUniform("intensidade", m_intensidade);
        ShaderManager::instance()->popShader();
    }
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
    ShaderManager::instance()->popShader();
    return true;
}

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
