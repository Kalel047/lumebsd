/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * O splash do LumeBSD (enquanto o KDE carrega): o farol e o nome sobre o
 * papel, e seis quadrados que se preenchem a cada etapa, sem animação, como
 * uma tela de e-ink que atualiza. O lume tema instala uma cópia com as cores
 * do Papel noite no tema escuro.
 */
import QtQuick

Rectangle {
    id: root
    color: "#efeadf"            // papel
    property color tinta: "#1c1b19"
    property string logo: "images/lumebsd-logo.svg"
    property int stage

    Image {
        id: marca
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -root.height * 0.04
        source: root.logo
        sourceSize.height: Math.round(root.height * 0.12)
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    Row {
        anchors.top: marca.bottom
        anchors.topMargin: Math.round(root.height * 0.06)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Math.round(root.height * 0.012)
        Repeater {
            model: 6
            Rectangle {
                width: Math.round(root.height * 0.012)
                height: width
                color: index < root.stage ? root.tinta : "transparent"
                border.color: root.tinta
                border.width: Math.max(1, Math.round(root.height / 900))
            }
        }
    }
}
