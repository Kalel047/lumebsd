// SPDX-License-Identifier: BSD-2-Clause
//
// O que a decoração "Papel" e o controlador de abas (lume_abas) combinam:
// os dois são plugins carregados no mesmo processo, o KWin. O controlador se
// registra numa propriedade do aplicativo ("lume.abas"); a decoração o acha
// ali a cada desenho. Sem o controlador (o efeito desligado), a decoração
// mostra só a aba da própria janela.
//
// A chave de cada janela é o ponteiro da decoração dela: o controlador
// conhece as janelas do KWin (Window::decoration()), a decoração só a si.

#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QVariant>

namespace KDecoration3
{
class Decoration;
}

namespace Lume
{

struct Aba {
    QString titulo;
    bool ativa = false;     // a janela que aparece (as outras ficam minimizadas)
    bool daJanela = false;  // a aba da própria janela desta decoração
};

class ControleAbas
{
public:
    virtual ~ControleAbas() = default;
    // As abas do grupo desta janela, na ordem; vazio: a janela está sozinha.
    virtual QList<Aba> abas(const KDecoration3::Decoration *d) const = 0;
    // Clique numa aba: mostra aquela janela do grupo.
    virtual void ativar(const KDecoration3::Decoration *d, int indice) = 0;
    // Clique do meio numa aba: tira aquela janela do grupo.
    virtual void separar(const KDecoration3::Decoration *d, int indice) = 0;
};

inline const char *PROPRIEDADE = "lume.abas";

inline ControleAbas *controle()
{
    if (!qApp) {
        return nullptr;
    }
    const QVariant v = qApp->property(PROPRIEDADE);
    return v.isValid() ? reinterpret_cast<ControleAbas *>(v.value<quintptr>()) : nullptr;
}

} // namespace Lume
