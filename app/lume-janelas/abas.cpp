// SPDX-License-Identifier: BSD-2-Clause
//
// lume_abas: abas de janelas, como no BeOS e no Haiku (lá, "Stack"). Várias
// janelas viram um grupo que ocupa um lugar só na tela: uma aparece, as
// outras ficam minimizadas atrás dela, e a barra de título (a decoração
// "Papel") mostra uma aba para cada uma, lado a lado.
//
//   Ctrl+Meta+E          empilha a janela ativa sobre a janela anterior
//   Ctrl+Meta+Shift+E    tira a janela ativa do grupo
//   Ctrl+Meta+PgDown/Up  a próxima aba / a anterior
//   clique numa aba      mostra aquela janela
//   clique do meio       tira aquela janela do grupo
//
// No BeOS isso fica no servidor de janelas; aqui, num plugin do KWin (que
// vê todas as janelas e pode movê-las, minimizá-las e ativá-las). A
// decoração pergunta a este controlador o que desenhar (abas.h).
//
// Sem moc: como o lume_tinta, só sinais ligados a lambdas e os metadados
// gerados pelo lume-montar (ferramentas/montar efeitos).

#include "abas.h"

#include "effect/effect.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "window.h"
#include "workspace.h"

#include <KDecoration3/Decoration>
#include <KGlobalAccel>
#include <QAction>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QtPlugin>

#include <algorithm>
#include <cstring>
#include <memory>

namespace KWin
{

struct Grupo {
    QList<Window *> membros;
    Window *atual = nullptr;
};

class Abas : public Effect, public Lume::ControleAbas
{
public:
    Abas();
    ~Abas() override;

    static bool supported() { return true; }

    // Lume::ControleAbas
    QList<Lume::Aba> abas(const KDecoration3::Decoration *d) const override;
    void ativar(const KDecoration3::Decoration *d, int indice) override;
    void separar(const KDecoration3::Decoration *d, int indice) override;

private:
    void ativou(Window *w);
    void empilhar();
    void separarAtiva();
    void vizinha(int passo);
    void mostrar(Grupo *g, Window *w);
    void tirar(Window *w, bool afastar);
    void redesenhar(Grupo *g) const;
    void acompanhar(Window *w);
    void retomar();
    Window *janelaDe(const KDecoration3::Decoration *d) const;
    QAction *atalho(const char *nome, const QString &texto, QKeySequence tecla,
                    std::function<void()> f);

    QHash<Window *, std::shared_ptr<Grupo>> m_grupo;
    QPointer<Window> m_ativa;
    QPointer<Window> m_anterior;
    bool m_trocando = false;  // minimizando e ativando por conta própria
};

static bool candidata(Window *w)
{
    return w && !w->isDeleted() && w->isNormalWindow() && !w->isSpecialWindow();
}

Abas::Abas()
{
    qApp->setProperty(Lume::PROPRIEDADE, QVariant::fromValue(
        reinterpret_cast<quintptr>(static_cast<Lume::ControleAbas *>(this))));

    connect(workspace(), &Workspace::windowActivated, this, [this](Window *w) {
        ativou(w);
    });
    connect(workspace(), &Workspace::windowRemoved, this, [this](Window *w) {
        if (m_grupo.contains(w)) {
            tirar(w, false);
        }
    });

    atalho("LumeAbasEmpilhar", QStringLiteral("Abas: empilhar a janela sobre a anterior"),
           Qt::CTRL | Qt::META | Qt::Key_E, [this] { empilhar(); });
    atalho("LumeAbasSeparar", QStringLiteral("Abas: tirar a janela do grupo"),
           Qt::CTRL | Qt::META | Qt::SHIFT | Qt::Key_E, [this] { separarAtiva(); });
    atalho("LumeAbasProxima", QStringLiteral("Abas: a próxima"),
           Qt::CTRL | Qt::META | Qt::Key_PageDown, [this] { vizinha(1); });
    atalho("LumeAbasAnterior", QStringLiteral("Abas: a anterior"),
           Qt::CTRL | Qt::META | Qt::Key_PageUp, [this] { vizinha(-1); });

    if (Window *w = workspace()->activeWindow()) {
        m_ativa = w;
    }
    retomar();
}

// O KWin do X11 recria todos os efeitos a cada "reconfigure" (o compositor
// recomeça: trocar o tema, mudar uma opção do KWin). Os grupos não podem
// sumir com isso: o controlador que sai guarda os grupos numa propriedade do
// aplicativo (o KWin continua o mesmo processo), e o que entra os retoma.
// Se ninguém retomar (o efeito foi desligado), as janelas escondidas voltam
// à tela pouco depois. (O KWin não descarrega a biblioteca de um efeito,
// então o temporizador pode chamar o código daqui.)
static const char *const ESTADO = "lume.abas.estado";

static void soltarSobras()
{
    const QVariant v = qApp->property(ESTADO);
    if (!v.isValid()) {
        return;
    }
    qApp->setProperty(ESTADO, QVariant());
    const QVariantList grupos = v.toList();
    for (const QVariant &gv : grupos) {
        const QVariantMap g = gv.toMap();
        const QString atual = g.value(QStringLiteral("atual")).toString();
        const QStringList ids = g.value(QStringLiteral("membros")).toStringList();
        for (const QString &id : ids) {
            Window *w = workspace()->findWindow(QUuid(id));
            if (!w || w->isDeleted()) {
                continue;
            }
            if (id != atual && w->isMinimized()) {
                w->setMinimized(false);
            }
            if (auto *d = w->decoration()) {
                d->update();
            }
        }
    }
}

void Abas::retomar()
{
    const QVariant v = qApp->property(ESTADO);
    if (!v.isValid()) {
        return;
    }
    qApp->setProperty(ESTADO, QVariant());
    const QVariantList grupos = v.toList();
    for (const QVariant &gv : grupos) {
        const QVariantMap gm = gv.toMap();
        auto g = std::make_shared<Grupo>();
        const QStringList ids = gm.value(QStringLiteral("membros")).toStringList();
        for (const QString &id : ids) {
            Window *w = workspace()->findWindow(QUuid(id));
            if (candidata(w) && !m_grupo.contains(w)) {
                g->membros << w;
            }
        }
        if (g->membros.size() < 2) {
            // Sobrou uma janela só: ela volta a ser solta (e aparece).
            for (Window *w : std::as_const(g->membros)) {
                if (w->isMinimized()) {
                    w->setMinimized(false);
                }
            }
            continue;
        }
        g->atual = workspace()->findWindow(QUuid(gm.value(QStringLiteral("atual")).toString()));
        if (!g->membros.contains(g->atual)) {
            g->atual = g->membros.first();
        }
        for (Window *w : std::as_const(g->membros)) {
            m_grupo.insert(w, g);
            acompanhar(w);
        }
        redesenhar(g.get());
    }
}

Abas::~Abas()
{
    if (Lume::controle() == static_cast<Lume::ControleAbas *>(this)) {
        qApp->setProperty(Lume::PROPRIEDADE, QVariant());
    }
    QVariantList grupos;
    QSet<Grupo *> vistos;
    for (auto it = m_grupo.begin(); it != m_grupo.end(); ++it) {
        Grupo *g = it.value().get();
        if (vistos.contains(g)) {
            continue;
        }
        vistos.insert(g);
        QStringList ids;
        for (Window *w : std::as_const(g->membros)) {
            if (!w->isDeleted()) {
                ids << w->internalId().toString();
            }
        }
        QVariantMap gm;
        gm.insert(QStringLiteral("membros"), ids);
        gm.insert(QStringLiteral("atual"), g->atual ? g->atual->internalId().toString() : QString());
        grupos << gm;
    }
    if (grupos.isEmpty()) {
        return;
    }
    qApp->setProperty(ESTADO, grupos);
    QTimer::singleShot(3000, qApp, [] {
        soltarSobras();
    });
    // Enquanto isso, a decoração desenha cada janela sozinha.
    for (auto it = m_grupo.begin(); it != m_grupo.end(); ++it) {
        if (auto *d = it.key()->decoration()) {
            d->update();
        }
    }
}

QAction *Abas::atalho(const char *nome, const QString &texto, QKeySequence tecla,
                      std::function<void()> f)
{
    auto *a = new QAction(this);
    a->setAutoRepeat(false);
    a->setObjectName(QString::fromLatin1(nome));
    a->setText(texto);
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << tecla);
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << tecla);
    connect(a, &QAction::triggered, this, [f] { f(); });
    return a;
}

void Abas::acompanhar(Window *w)
{
    // O título da aba muda com o da janela.
    connect(w, &Window::captionChanged, this, [this, w] {
        if (auto g = m_grupo.value(w)) {
            redesenhar(g.get());
        }
    });
}

void Abas::redesenhar(Grupo *g) const
{
    for (Window *m : g->membros) {
        if (auto *d = m->decoration()) {
            d->update();
        }
    }
}

// A janela ativa mudou: guarda a anterior (para o empilhar) e, se ela é de
// um grupo e não é a que aparece (veio pela barra de tarefas, pelo Alt+Tab),
// ela passa a aparecer no lugar da outra.
void Abas::ativou(Window *w)
{
    if (!candidata(w)) {
        return;
    }
    if (w != m_ativa) {
        m_anterior = m_ativa;
        m_ativa = w;
    }
    if (m_trocando) {
        return;
    }
    if (auto g = m_grupo.value(w); g && g->atual != w) {
        mostrar(g.get(), w);
    }
}

// Mostra a janela w do grupo: ela vai para o lugar (e o tamanho) da que
// aparecia, que é minimizada.
void Abas::mostrar(Grupo *g, Window *w)
{
    Window *antes = g->atual;
    if (antes == w) {
        return;
    }
    m_trocando = true;
    if (antes && !antes->isDeleted()) {
        if (antes->maximizeMode() == MaximizeFull) {
            w->maximize(MaximizeFull);
        } else {
            if (w->maximizeMode() != MaximizeRestore) {
                w->maximize(MaximizeRestore);
            }
            w->moveResize(antes->frameGeometry());
        }
    }
    if (w->isMinimized()) {
        w->setMinimized(false);
    }
    g->atual = w;
    workspace()->activateWindow(w, true);
    if (antes && !antes->isDeleted() && !antes->isMinimized()) {
        antes->setMinimized(true);
    }
    m_trocando = false;
    redesenhar(g);
}

// Ctrl+Meta+E: a janela ativa entra no grupo da anterior (ou as duas formam
// um grupo novo) e fica por cima, no lugar dela.
void Abas::empilhar()
{
    Window *a = m_ativa;
    Window *b = m_anterior;
    if (!candidata(a) || !candidata(b) || a == b || b->isMinimized() || !b->isOnCurrentDesktop()) {
        return;
    }
    if (m_grupo.value(a) && m_grupo.value(a) == m_grupo.value(b)) {
        return;
    }
    if (m_grupo.contains(a)) {
        tirar(a, false);
    }
    auto g = m_grupo.value(b);
    if (!g) {
        g = std::make_shared<Grupo>();
        g->membros << b;
        g->atual = b;
        m_grupo.insert(b, g);
        acompanhar(b);
    }
    g->membros << a;
    m_grupo.insert(a, g);
    acompanhar(a);
    // b continua na tela até a troca: a vai para o lugar dela e b se esconde.
    g->atual = b;
    mostrar(g.get(), a);
}

// Tira w do grupo. Se era a que aparecia, a próxima aparece; com afastar, w
// fica um pouco deslocada, para se ver que saiu.
void Abas::tirar(Window *w, bool afastar)
{
    auto g = m_grupo.take(w);
    if (!g) {
        return;
    }
    disconnect(w, &Window::captionChanged, this, nullptr);
    const int i = g->membros.indexOf(w);
    g->membros.removeAll(w);
    if (!w->isDeleted()) {
        m_trocando = true;
        if (w->isMinimized()) {
            w->setMinimized(false);
        }
        if (afastar) {
            QRectF r = w->frameGeometry();
            r.translate(32, 32);
            w->moveResize(r);
        }
        m_trocando = false;
        if (auto *d = w->decoration()) {
            d->update();
        }
    }
    if (g->atual == w) {
        g->atual = nullptr;
        if (!g->membros.isEmpty()) {
            Window *proxima = g->membros.value(std::min<int>(i, g->membros.size() - 1));
            m_trocando = true;
            if (proxima->isMinimized()) {
                proxima->setMinimized(false);
            }
            if (!w->isDeleted()) {
                proxima->moveResize(w->frameGeometry().translated(afastar ? -32 : 0, afastar ? -32 : 0));
            }
            g->atual = proxima;
            m_trocando = false;
        }
    }
    // Um grupo de uma janela só não é grupo.
    if (g->membros.size() == 1) {
        Window *so = g->membros.first();
        m_grupo.remove(so);
        disconnect(so, &Window::captionChanged, this, nullptr);
        if (so->isMinimized()) {
            m_trocando = true;
            so->setMinimized(false);
            m_trocando = false;
        }
        if (auto *d = so->decoration()) {
            d->update();
        }
        return;
    }
    redesenhar(g.get());
}

void Abas::separarAtiva()
{
    if (m_ativa && m_grupo.contains(m_ativa)) {
        tirar(m_ativa, true);
    }
}

void Abas::vizinha(int passo)
{
    Window *w = m_ativa;
    auto g = w ? m_grupo.value(w) : nullptr;
    if (!g || g->membros.size() < 2) {
        return;
    }
    const int n = g->membros.size();
    const int i = (g->membros.indexOf(g->atual) + passo + n) % n;
    mostrar(g.get(), g->membros.at(i));
}

Window *Abas::janelaDe(const KDecoration3::Decoration *d) const
{
    for (auto it = m_grupo.begin(); it != m_grupo.end(); ++it) {
        if (it.key()->decoration() == d) {
            return it.key();
        }
    }
    return nullptr;
}

QList<Lume::Aba> Abas::abas(const KDecoration3::Decoration *d) const
{
    QList<Lume::Aba> r;
    Window *w = janelaDe(d);
    auto g = w ? m_grupo.value(w) : nullptr;
    if (!g) {
        return r;
    }
    for (Window *m : g->membros) {
        Lume::Aba a;
        a.titulo = m->caption();
        a.ativa = (m == g->atual);
        a.daJanela = (m == w);
        r << a;
    }
    return r;
}

void Abas::ativar(const KDecoration3::Decoration *d, int indice)
{
    Window *w = janelaDe(d);
    auto g = w ? m_grupo.value(w) : nullptr;
    if (g && indice >= 0 && indice < g->membros.size()) {
        mostrar(g.get(), g->membros.at(indice));
    }
}

void Abas::separar(const KDecoration3::Decoration *d, int indice)
{
    Window *w = janelaDe(d);
    auto g = w ? m_grupo.value(w) : nullptr;
    if (g && indice >= 0 && indice < g->membros.size()) {
        tirar(g->membros.at(indice), true);
    }
}

class AbasFabrica : public EffectPluginFactory
{
public:
    bool isSupported() const override
    {
        return Abas::supported();
    }
    bool enabledByDefault() const override
    {
        return true;
    }
    Effect *createEffect() const override
    {
        return new Abas();
    }
    // Como no lume_tinta: a resposta que o moc daria ao qobject_cast.
    void *qt_metacast(const char *nome) override
    {
        if (nome && std::strcmp(nome, KPluginFactory_iid) == 0) {
            return static_cast<KPluginFactory *>(this);
        }
        return EffectPluginFactory::qt_metacast(nome);
    }
};

} // namespace KWin

#include "metadados.inc"
QT_MOC_EXPORT_PLUGIN_V2(KWin::AbasFabrica, AbasFabrica, qt_pluginMetaDataV2_AbasFabrica)
