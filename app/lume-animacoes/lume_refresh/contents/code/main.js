/*
    SPDX-License-Identifier: GPL-2.0-or-later

    lume_refresh: abrir e fechar janelas como numa tela de e-ink.

    Uma tela de e-ink troca a imagem com um "refresh": a área pisca escura e
    a imagem nova surge de uma vez. Ao abrir, a janela aparece escura (o
    filtro e-ink a desenha na cor da tinta) e o conteúdo surge rápido, no fim;
    ao fechar, escurece e some. Curto (cerca de 200 ms) e sem movimento: nada
    desliza nem cresce, como no papel.
*/

const ignorar = [
    "ksmserver ksmserver",
    "ksmserver-logout-greeter ksmserver-logout-greeter",
    "ksplashqml ksplashqml",
    "spectacle spectacle",
    "spectacle org.kde.spectacle",
];

class Refresh {
    constructor() {
        effect.configChanged.connect(this.lerConfig.bind(this));
        effects.windowAdded.connect(this.abriu.bind(this));
        effects.windowClosed.connect(this.fechou.bind(this));
        effects.windowDataChanged.connect(this.dadosMudaram.bind(this));
        this.lerConfig();
    }

    lerConfig() {
        // Tempos fixos, sem animationTime(): o tema Papel deixa as
        // animações do KDE em zero (AnimationDurationFactor=0), e estas
        // continuam.
        this.abrir = Number(effect.readConfig("Abrir", 220));
        this.fechar = Number(effect.readConfig("Fechar", 160));
    }

    static anima(w) {
        if (ignorar.indexOf(w.windowClass) != -1) {
            return false;
        }
        if (w.popupWindow || !w.managed || !w.visible || w.outline) {
            return false;
        }
        if (w.deleted && effect.isGrabbed(w, Effect.WindowClosedGrabRole)) {
            return false;
        }
        if (!w.deleted && effect.isGrabbed(w, Effect.WindowAddedGrabRole)) {
            return false;
        }
        return w.normalWindow || w.dialog;
    }

    abriu(w) {
        if (effects.hasActiveFullScreenEffect || !Refresh.anima(w)) {
            return;
        }
        if (w.animFechar) {
            cancel(w.animFechar);
            delete w.animFechar;
        }
        w.animAbrir = animate({
            window: w,
            duration: this.abrir,
            animations: [{
                // Escura quase o tempo todo e o conteúdo surge no fim: o
                // "refresh" do e-ink.
                type: Effect.Brightness,
                curve: QEasingCurve.InExpo,
                from: 0.0,
                to: 1.0
            }, {
                // Sem transparência visível: só não aparece antes do quadro
                // escuro.
                type: Effect.Opacity,
                curve: QEasingCurve.OutExpo,
                duration: 40,
                from: 0.0,
                to: 1.0
            }]
        });
    }

    fechou(w) {
        if (effects.hasActiveFullScreenEffect || w.skipsCloseAnimation || !Refresh.anima(w)) {
            return;
        }
        if (w.animAbrir) {
            cancel(w.animAbrir);
            delete w.animAbrir;
        }
        w.animFechar = animate({
            window: w,
            duration: this.fechar,
            animations: [{
                type: Effect.Brightness,
                curve: QEasingCurve.OutExpo,
                to: 0.0
            }, {
                type: Effect.Opacity,
                curve: QEasingCurve.InExpo,
                to: 0.0
            }]
        });
    }

    dadosMudaram(w, papel) {
        if (papel == Effect.WindowAddedGrabRole && effect.isGrabbed(w, Effect.WindowAddedGrabRole) && w.animAbrir) {
            cancel(w.animAbrir);
            delete w.animAbrir;
        } else if (papel == Effect.WindowClosedGrabRole && effect.isGrabbed(w, Effect.WindowClosedGrabRole) && w.animFechar) {
            cancel(w.animFechar);
            delete w.animFechar;
        }
    }
}

new Refresh();
