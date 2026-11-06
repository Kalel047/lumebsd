/*
    SPDX-License-Identifier: GPL-2.0-or-later

    lume_esmaecer: abrir e fechar janelas esmaecendo em tons de cinza.

    A janela surge do papel: começa transparente e sem cor e chega à cor
    (ou à tinta, com o filtro e-ink) em cerca de 160 ms; ao fechar, o
    inverso. Sem movimento.
*/

const ignorar = [
    "ksmserver ksmserver",
    "ksmserver-logout-greeter ksmserver-logout-greeter",
    "ksplashqml ksplashqml",
    "spectacle spectacle",
    "spectacle org.kde.spectacle",
];

class Esmaecer {
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
        this.abrir = Number(effect.readConfig("Abrir", 160));
        this.fechar = Number(effect.readConfig("Fechar", 140));
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
        if (effects.hasActiveFullScreenEffect || !Esmaecer.anima(w)) {
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
                type: Effect.Opacity,
                curve: QEasingCurve.OutCubic,
                from: 0.0,
                to: 1.0
            }, {
                type: Effect.Saturation,
                curve: QEasingCurve.InCubic,
                from: 0.0,
                to: 1.0
            }]
        });
    }

    fechou(w) {
        if (effects.hasActiveFullScreenEffect || w.skipsCloseAnimation || !Esmaecer.anima(w)) {
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
                type: Effect.Opacity,
                curve: QEasingCurve.InCubic,
                to: 0.0
            }, {
                type: Effect.Saturation,
                curve: QEasingCurve.OutCubic,
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

new Esmaecer();
