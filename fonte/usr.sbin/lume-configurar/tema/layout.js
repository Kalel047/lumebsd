// Layout inicial do LumeBSD (tema Papel): um painel fino embaixo, opaco,
// com o menu, as janelas abertas, a bandeja e o relógio com a data.
var papel = "file:///usr/local/share/wallpapers/LumePapel/contents/images/3840x2160.png";

var panel = new Panel;
panel.location = "bottom";
panel.height = Math.round(gridUnit * 2.2);
panel.hiding = "none";
panel.floating = false;

var menu = panel.addWidget("org.kde.plasma.kickoff");
menu.currentConfigGroup = ["General"];
menu.writeConfig("icon", "start-here-kde-symbolic");

panel.addWidget("org.kde.plasma.icontasks");
panel.addWidget("org.kde.plasma.marginsseparator");
panel.addWidget("org.kde.plasma.systemtray");

var relogio = panel.addWidget("org.kde.plasma.digitalclock");
relogio.currentConfigGroup = ["Appearance"];
relogio.writeConfig("showDate", true);
relogio.writeConfig("dateFormat", "longDate");
relogio.writeConfig("use24hFormat", 2);

var telas = desktops();
for (var i = 0; i < telas.length; i++) {
    telas[i].wallpaperPlugin = "org.kde.image";
    telas[i].currentConfigGroup = ["Wallpaper", "org.kde.image", "General"];
    telas[i].writeConfig("Image", papel);
    telas[i].writeConfig("FillMode", 2);
}
