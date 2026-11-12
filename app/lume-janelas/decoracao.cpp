// SPDX-License-Identifier: BSD-2-Clause
//
// A decoração de janelas "Papel" do LumeBSD (org.lumebsd.papel).
//
// A barra de título é uma aba, como no BeOS: só da largura do título, com o
// fechar à esquerda e o minimizar e o maximizar à direita, sobre um fio de
// tinta que contorna a janela. Sem sombra, sem cantos redondos, sem
// gradiente (o tema Papel, estética e-ink). As cores são as do esquema do
// KDE (o Papel ou o Papel noite): o papel e a tinta da janela.
//
// Janelas empilhadas (o controlador lume_abas, abas.h) ganham uma aba cada,
// lado a lado; a da janela que aparece fica ligada ao corpo dela, as outras
// um tom abaixo. Clique numa aba mostra aquela janela; clique do meio a tira
// do grupo.
//
// Sem moc: a classe não declara sinais nem slots novos; o plugin é um
// KPluginFactory com a resposta ao qobject_cast escrita à mão e os
// metadados gerados pelo montar-efeitos.py.

#include "abas.h"

#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/Decoration>
#include <KDecoration3/DecorationButton>
#include <KDecoration3/DecorationButtonGroup>
#include <KDecoration3/DecorationSettings>
#include <KPluginFactory>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtPlugin>

#include <cmath>
#include <cstring>
#include <memory>

namespace Lume
{

using KDecoration3::DecorationButtonType;

// --- os botões --------------------------------------------------------------

// Desenhados com dois traços de tinta, como a lanterna do farol: um quadro
// (maximizar), um traço (minimizar), um x (fechar).
class Botao : public KDecoration3::DecorationButton
{
public:
    Botao(DecorationButtonType tipo, KDecoration3::Decoration *d, QObject *pai)
        : KDecoration3::DecorationButton(tipo, d, pai)
    {
    }

    void paint(QPainter *p, const QRectF &) override
    {
        auto *d = decoration();
        if (!d || !isVisible()) {
            return;
        }
        const QPalette pal = d->window()->palette();
        const bool ativa = d->window()->isActive();
        QColor tinta = pal.color(QPalette::WindowText);
        if (!ativa) {
            tinta.setAlphaF(0.55);
        }
        const QRectF r = geometry();
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        if (isHovered() || isPressed()) {
            QColor fundo = tinta;
            fundo.setAlphaF(isPressed() ? 0.25 : 0.12);
            p->fillRect(r, fundo);
        }
        const qreal s = std::round(r.height() * 0.30);
        const QRectF c(r.center().x() - s, r.center().y() - s, 2 * s, 2 * s);
        QPen caneta(tinta, std::max<qreal>(1.25, r.height() / 14.0));
        caneta.setCapStyle(Qt::SquareCap);
        p->setPen(caneta);
        p->setBrush(Qt::NoBrush);
        switch (type()) {
        case DecorationButtonType::Close:
            p->drawLine(c.topLeft(), c.bottomRight());
            p->drawLine(c.topRight(), c.bottomLeft());
            break;
        case DecorationButtonType::Maximize:
            if (isChecked()) {
                p->drawRect(c.adjusted(s * 0.5, 0, 0, -s * 0.5));
                p->drawRect(c.adjusted(0, s * 0.5, -s * 0.5, 0));
            } else {
                p->drawRect(c);
            }
            break;
        case DecorationButtonType::Minimize:
            p->drawLine(QPointF(c.left(), c.bottom()), c.bottomRight());
            break;
        default:
            break;
        }
        p->restore();
    }
};

// --- a decoração -------------------------------------------------------------

class Papel : public KDecoration3::Decoration
{
public:
    Papel(QObject *pai, const QVariantList &args)
        : KDecoration3::Decoration(pai, args)
    {
    }

    bool init() override
    {
        auto *w = window();
        // Grupos vazios (sem os botões das configurações do KDE): o
        // desenho da aba é do BeOS, os botões são estes três.
        m_esquerda = new KDecoration3::DecorationButtonGroup(this);
        m_direita = new KDecoration3::DecorationButtonGroup(this);
        // O BeOS: fechar à esquerda; minimizar e maximizar à direita da aba.
        m_esquerda->addButton(new Botao(DecorationButtonType::Close, this, m_esquerda));
        m_direita->addButton(new Botao(DecorationButtonType::Minimize, this, m_direita));
        m_direita->addButton(new Botao(DecorationButtonType::Maximize, this, m_direita));
        const auto recalc = [this] { medir(); };
        connect(w, &KDecoration3::DecoratedWindow::captionChanged, this, recalc);
        connect(w, &KDecoration3::DecoratedWindow::widthChanged, this, recalc);
        connect(w, &KDecoration3::DecoratedWindow::maximizedChanged, this, recalc);
        connect(w, &KDecoration3::DecoratedWindow::activeChanged, this, [this] { update(); });
        connect(w, &KDecoration3::DecoratedWindow::paletteChanged, this, [this] { update(); });
        connect(settings().get(), &KDecoration3::DecorationSettings::fontChanged, this, recalc);
        medir();
        return true;
    }

    void paint(QPainter *p, const QRectF &) override;

protected:
    void mousePressEvent(QMouseEvent *e) override;

private:
    void medir();
    struct Pedaco {
        QRectF r;
        Aba aba;
    };
    QList<Pedaco> pedacos() const;
    qreal altura() const { return m_altura; }

    KDecoration3::DecorationButtonGroup *m_esquerda = nullptr;
    KDecoration3::DecorationButtonGroup *m_direita = nullptr;
    qreal m_altura = 26;
    qreal m_borda = 1;
};

// A altura da barra (a fonte do título com folga), as bordas e a posição dos
// botões dentro da aba desta janela.
void Papel::medir()
{
    const QFontMetricsF fm(settings()->font());
    m_altura = std::round(fm.height() * 1.7);
    const bool max = window()->isMaximized();
    m_borda = max ? 0 : 1;
    setBorders(QMarginsF(m_borda, m_altura, m_borda, m_borda));
    // Bordas de redimensionar mais largas que o fio (só para o mouse).
    setResizeOnlyBorders(max ? QMarginsF() : QMarginsF(4, 0, 4, 4));
    setTitleBar(QRectF(0, 0, window()->width() + 2 * m_borda, m_altura));
    const qreal lado = m_altura;
    for (auto *b : m_esquerda->buttons()) {
        b->setGeometry(QRectF(0, 0, lado, lado));
    }
    for (auto *b : m_direita->buttons()) {
        b->setGeometry(QRectF(0, 0, lado, lado));
    }
    // A aba desta janela: onde os botões ficam.
    QRectF minha;
    for (const Pedaco &pd : pedacos()) {
        if (pd.aba.daJanela) {
            minha = pd.r;
        }
    }
    m_esquerda->setPos(minha.topLeft());
    m_direita->setPos(QPointF(minha.right() - 2 * lado, 0));
    update();
}

// As abas: sozinha, uma do tamanho do título; num grupo, uma por janela,
// repartindo a largura se não couber.
QList<Papel::Pedaco> Papel::pedacos() const
{
    QList<Aba> abas;
    if (ControleAbas *c = controle()) {
        abas = c->abas(this);
    }
    if (abas.isEmpty()) {
        Aba a;
        a.titulo = window()->caption();
        a.ativa = true;
        a.daJanela = true;
        abas << a;
    }
    const QFontMetricsF fm(settings()->font());
    const qreal lado = m_altura;
    const qreal total = window()->width() + 2 * m_borda;
    QList<qreal> larguras;
    qreal soma = 0;
    for (const Aba &a : abas) {
        // título + o fechar + minimizar e maximizar (só na da janela) + folga
        qreal l = fm.horizontalAdvance(a.titulo) + lado * (a.daJanela ? 3 : 1) + lado * 0.6;
        l = std::clamp<qreal>(l, lado * 5, std::max<qreal>(lado * 5, total * 0.6));
        larguras << l;
        soma += l;
    }
    const qreal fator = soma > total ? total / soma : 1.0;
    QList<Pedaco> r;
    qreal x = 0;
    for (int i = 0; i < abas.size(); i++) {
        const qreal l = std::floor(larguras[i] * fator);
        r << Pedaco{QRectF(x, 0, l, m_altura), abas[i]};
        x += l;
    }
    return r;
}

void Papel::paint(QPainter *p, const QRectF &)
{
    auto *w = window();
    const QPalette pal = w->palette();
    const bool ativa = w->isActive();
    const QColor papel = pal.color(QPalette::Window);
    QColor tinta = pal.color(QPalette::WindowText);
    QColor tintaFraca = tinta;
    tintaFraca.setAlphaF(0.55);
    // Um tom abaixo do papel: as abas das janelas que não aparecem.
    const QColor papelFundo = papel.lightness() > 128 ? papel.darker(108) : papel.lighter(135);
    const qreal fio = 1;

    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);
    const QRectF tudo = rect();

    // O contorno da janela: um fio de tinta (sem borda quando maximizada).
    if (m_borda > 0) {
        p->setPen(QPen(ativa ? tinta : tintaFraca, fio));
        p->setBrush(Qt::NoBrush);
        p->drawRect(QRectF(tudo.left(), m_altura - fio, tudo.width() - fio, tudo.height() - m_altura));
    } else {
        p->fillRect(QRectF(0, m_altura - fio, tudo.width(), fio), ativa ? tinta : tintaFraca);
    }

    // As abas.
    QFont fonte = settings()->font();
    const auto lista = pedacos();
    for (const Pedaco &pd : lista) {
        const bool frente = pd.aba.daJanela;  // a desta janela fica ligada ao corpo
        const QRectF r = pd.r.adjusted(0, 0, -fio, 0);
        p->fillRect(r, frente ? papel : papelFundo);
        p->setPen(QPen(ativa ? tinta : tintaFraca, fio));
        // Três lados; a de frente não tem o fio de baixo (continua no corpo).
        p->drawLine(r.bottomLeft(), r.topLeft());
        p->drawLine(r.topLeft(), r.topRight());
        p->drawLine(r.topRight(), r.bottomRight());
        if (!frente) {
            p->drawLine(QPointF(r.left(), m_altura - fio), QPointF(r.right(), m_altura - fio));
        } else {
            p->fillRect(QRectF(r.left() + fio, m_altura - fio, r.width() - fio, fio), papel);
        }
        // O título: na de frente, entre o fechar e os botões da direita.
        QRectF texto = r.adjusted(m_altura * 0.3, 0, -m_altura * 0.3, 0);
        if (frente) {
            texto = r.adjusted(m_altura * 1.05, 0, -m_altura * 2.05, 0);
        }
        fonte.setBold(frente && ativa);
        p->setFont(fonte);
        p->setPen(frente && ativa ? tinta : tintaFraca);
        const QString t = QFontMetricsF(fonte).elidedText(pd.aba.titulo, Qt::ElideRight, texto.width());
        p->drawText(texto, Qt::AlignVCenter | Qt::AlignLeft, t);
    }
    p->restore();

    m_esquerda->paint(p, tudo);
    m_direita->paint(p, tudo);
}

void Papel::mousePressEvent(QMouseEvent *e)
{
    // Num grupo: clique numa aba de outra janela mostra aquela janela; o
    // clique do meio tira a janela do grupo. O resto (arrastar, o duplo
    // clique, os botões) é com a classe base.
    if (ControleAbas *c = controle()) {
        const auto lista = pedacos();
        if (lista.size() > 1) {
            for (int i = 0; i < lista.size(); i++) {
                if (!lista[i].r.contains(e->position())) {
                    continue;
                }
                if (e->button() == Qt::MiddleButton) {
                    c->separar(this, i);
                    e->accept();
                    return;
                }
                if (e->button() == Qt::LeftButton && !lista[i].aba.daJanela) {
                    c->ativar(this, i);
                    e->accept();
                    return;
                }
            }
        }
    }
    KDecoration3::Decoration::mousePressEvent(e);
}

// --- o plugin ----------------------------------------------------------------

class PapelFabrica : public KPluginFactory
{
public:
    PapelFabrica()
    {
        registerPlugin<Papel>();
    }
    void *qt_metacast(const char *nome) override
    {
        if (nome && std::strcmp(nome, KPluginFactory_iid) == 0) {
            return static_cast<KPluginFactory *>(this);
        }
        return KPluginFactory::qt_metacast(nome);
    }
};

} // namespace Lume

#include "metadados.inc"
QT_MOC_EXPORT_PLUGIN_V2(Lume::PapelFabrica, PapelFabrica, qt_pluginMetaDataV2_PapelFabrica)
