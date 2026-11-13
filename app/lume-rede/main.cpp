// SPDX-License-Identifier: BSD-2-Clause
//
// lume-rede - o ícone de rede do LumeBSD, na bandeja: o cabo e o Wi-Fi, as
// redes por perto, conectar com a senha, esquecer uma rede. No lugar do
// networkmgr (que é Python).
//
//   lume-rede            o ícone na bandeja (o autostart da sessão)
//   lume-rede --estado   imprime o estado e as redes, e sai (para testes)
//   lume-rede --conectar REDE
//                        conecta sem janela (a senha em LUME_REDE_SENHA),
//                        imprime o resultado e sai (para testes)
//
// O Wi-Fi é do wpa_supplicant: o lume-rede fala com ele pelo socket de
// controle (/var/run/wpa_supplicant/wlan0, o mesmo protocolo do wpa_cli),
// que o grupo wheel pode usar; não precisa de root. A senha vira a chave
// (PBKDF2, como o wpa_passphrase) aqui mesmo e só a chave vai para o
// wpa_supplicant. Se a placa ainda não está preparada (sem o socket), o
// menu oferece "Preparar o Wi-Fi", que roda o "lume wifi preparar" pelo
// pkexec.
//
// Sem moc: só sinais do próprio Qt ligados a lambdas, para compilar com o
// clang de fora do FreeBSD contra os cabeçalhos do pacote qt6-base.

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QIcon>
#include <QInputDialog>
#include <QLibraryInfo>
#include <QLineEdit>
#include <QList>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTranslator>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// LUME_REDE_SOCKET troca o socket (os testes, com um wpa_supplicant falso, numa
// máquina sem placa Wi-Fi).
static const char *
socket_wpa()
{
	const char *s = getenv("LUME_REDE_SOCKET");
	return s ? s : "/var/run/wpa_supplicant/wlan0";
}
static const char *LUME_WIFI = "/usr/sbin/lume-wifi";

// --- O wpa_supplicant ------------------------------------------------------

// Uma conversa pelo socket de controle: um datagrama por comando, uma
// resposta por datagrama. O cliente precisa de um endereço próprio (um
// socket em /tmp), para o wpa_supplicant ter para onde responder.
class Wpa {
public:
	~Wpa() { fechar(); }

	bool aberto() const { return fd_ >= 0; }

	bool abrir()
	{
		if (fd_ >= 0)
			return true;
		if (access(socket_wpa(), F_OK) != 0)
			return false;
		fd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
		if (fd_ < 0)
			return false;
		struct sockaddr_un local = {};
		local.sun_family = AF_UNIX;
		snprintf(local.sun_path, sizeof(local.sun_path), "/tmp/lume-rede-%d-%d",
		    (int)getuid(), (int)getpid());
		unlink(local.sun_path);
		local_ = local.sun_path;
		struct sockaddr_un dest = {};
		dest.sun_family = AF_UNIX;
		strlcpy(dest.sun_path, socket_wpa(), sizeof(dest.sun_path));
		if (bind(fd_, (struct sockaddr *)&local, SUN_LEN(&local)) != 0 ||
		    ::connect(fd_, (struct sockaddr *)&dest, SUN_LEN(&dest)) != 0) {
			fechar();
			return false;
		}
		// Confere: sem permissão (fora do grupo wheel), o connect passa e o
		// envio falha.
		if (pedir("PING").trimmed() != "PONG") {
			fechar();
			return false;
		}
		return true;
	}

	void fechar()
	{
		if (fd_ >= 0)
			close(fd_);
		fd_ = -1;
		if (!local_.isEmpty())
			unlink(local_.constData());
		local_.clear();
	}

	// Manda um comando e espera a resposta (até 3 s). Linhas que começam
	// com "<" são eventos (só chegam com ATTACH, que não usamos): pula.
	QByteArray pedir(const QByteArray &cmd)
	{
		if (fd_ < 0)
			return {};
		if (send(fd_, cmd.constData(), cmd.size(), 0) < 0) {
			fechar();
			return {};
		}
		char buf[16384];
		for (;;) {
			struct pollfd p = {fd_, POLLIN, 0};
			if (poll(&p, 1, 3000) <= 0)
				return {};
			ssize_t n = recv(fd_, buf, sizeof(buf) - 1, 0);
			if (n <= 0)
				return {};
			if (buf[0] == '<')
				continue;
			return QByteArray(buf, (int)n);
		}
	}

	bool ok(const QByteArray &cmd) { return pedir(cmd).trimmed() == "OK"; }

private:
	int fd_ = -1;
	QByteArray local_;
};

// O wpa_supplicant escreve o nome da rede com escapes (\\, \", \xNN); de
// volta aos bytes.
static QByteArray
desescapar(const QByteArray &s)
{
	QByteArray r;
	for (int i = 0; i < s.size(); i++) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			char c = s[i + 1];
			if (c == 'x' && i + 3 < s.size()) {
				bool ok;
				int v = s.mid(i + 2, 2).toInt(&ok, 16);
				if (ok) {
					r.append((char)v);
					i += 3;
					continue;
				}
			}
			r.append(c == 'n' ? '\n' : c == 't' ? '\t' : c);
			i++;
			continue;
		}
		r.append(s[i]);
	}
	return r;
}

// "chave=valor" por linha (STATUS, SIGNAL_POLL).
static QMap<QByteArray, QByteArray>
campos(const QByteArray &txt)
{
	QMap<QByteArray, QByteArray> m;
	for (const QByteArray &l : txt.split('\n')) {
		int i = l.indexOf('=');
		if (i > 0)
			m.insert(l.left(i), l.mid(i + 1));
	}
	return m;
}

struct Rede {
	QByteArray ssid;     // os bytes
	QString nome;        // para mostrar
	int sinal = -100;    // dBm
	QByteArray flags;    // [WPA2-PSK-CCMP][ESS]...
	bool guardada = false;
	int id = -1;         // na lista do wpa_supplicant, se guardada
	bool psk() const { return flags.contains("PSK"); }
	bool sae() const { return flags.contains("SAE"); }
	bool aberta() const { return !flags.contains("WPA") && !flags.contains("RSN") && !flags.contains("WEP"); }
	bool suportada() const { return aberta() || psk() || sae(); }
};

// dBm -> 0..4 (as barras).
static int
barras(int dbm)
{
	if (dbm >= -55) return 4;
	if (dbm >= -65) return 3;
	if (dbm >= -75) return 2;
	if (dbm >= -85) return 1;
	return 0;
}

// A chave de uma rede WPA a partir da senha: PBKDF2-HMAC-SHA1(senha, nome da
// rede, 4096, 32), em hexadecimal (é o que o wpa_passphrase faz).
static QByteArray
chave_psk(const QByteArray &senha, const QByteArray &ssid)
{
	unsigned char k[32];
	if (PKCS5_PBKDF2_HMAC_SHA1(senha.constData(), senha.size(),
	    (const unsigned char *)ssid.constData(), ssid.size(), 4096, sizeof(k), k) != 1)
		return {};
	return QByteArray((const char *)k, sizeof(k)).toHex();
}

// Uma string entre aspas para o wpa_supplicant (sae_password): ele não tem
// escapes; recusa aspas e quebras de linha.
static bool
cabe_entre_aspas(const QByteArray &s)
{
	return !s.contains('"') && !s.contains('\n') && !s.contains('\r');
}

// --- O cabo ----------------------------------------------------------------

struct Cabo {
	QString interface;
	QString ip;
};

// Uma placa de rede com fio (ethernet) ligada e com endereço IPv4.
static Cabo
cabo()
{
	Cabo c;
	struct ifaddrs *lista;
	if (getifaddrs(&lista) != 0)
		return c;
	QMap<QString, bool> ethernet;
	for (struct ifaddrs *i = lista; i; i = i->ifa_next) {
		if (i->ifa_addr && i->ifa_addr->sa_family == AF_LINK) {
			auto *dl = (struct sockaddr_dl *)i->ifa_addr;
			ethernet.insert(i->ifa_name, dl->sdl_type == IFT_ETHER &&
			    (i->ifa_flags & IFF_UP) && (i->ifa_flags & IFF_RUNNING));
		}
	}
	for (struct ifaddrs *i = lista; i; i = i->ifa_next) {
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET)
			continue;
		QString nome = i->ifa_name;
		if (nome.startsWith("wlan") || !ethernet.value(nome))
			continue;
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, ip, sizeof(ip));
		c.interface = nome;
		c.ip = ip;
		break;
	}
	freeifaddrs(lista);
	return c;
}

// Há uma placa Wi-Fi com driver?
static bool
tem_placa_wifi()
{
	if (getenv("LUME_REDE_SOCKET"))
		return true;
	char buf[256];
	size_t n = sizeof(buf);
	if (sysctlbyname("net.wlan.devices", buf, &n, nullptr, 0) != 0)
		return false;
	return n > 1 && buf[0] != '\0';
}

// --- O ícone ---------------------------------------------------------------

class Bandeja {
public:
	Bandeja();
	void estado_no_terminal();
	void conectar_no_terminal(const QString &nome);

private:
	void atualizar();
	void procurar();
	void ler_redes();
	void montar_menu();
	void conectar(const Rede &r);
	void esquecer(int id, const QString &nome);
	void preparar();
	void conferir_conexao(int id, const QString &nome, bool nova, int tentativas);
	QIcon icone() const;

	Wpa wpa_;
	QSystemTrayIcon tray_;
	QMenu menu_;
	QTimer relogio_;
	QList<Rede> redes_;
	QMap<QByteArray, QByteArray> status_;
	int sinal_ = -100;
	Cabo cabo_;
	bool placa_ = false;
	bool conectando_ = false;
	// No modo de teste: a senha vem pronta e o resultado vai para o terminal.
	bool terminal_ = false;
	QByteArray senha_pronta_;
	void avisar(const QString &texto, bool ok);
};

void
Bandeja::avisar(const QString &texto, bool ok)
{
	if (terminal_) {
		printf("%s\n", texto.toUtf8().constData());
		fflush(stdout);
		qApp->exit(ok ? 0 : 1);
		return;
	}
	tray_.showMessage("Wi-Fi", texto, ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning,
	    ok ? 4000 : 6000);
}

Bandeja::Bandeja()
{
	tray_.setContextMenu(&menu_);
	QObject::connect(&tray_, &QSystemTrayIcon::activated, &menu_,
	    [this](QSystemTrayIcon::ActivationReason r) {
		if (r == QSystemTrayIcon::Trigger) {
			procurar();
			montar_menu();
			menu_.popup(QCursor::pos());
		}
	});
	QObject::connect(&menu_, &QMenu::aboutToShow, &menu_, [this] { montar_menu(); });
	QObject::connect(&relogio_, &QTimer::timeout, &menu_, [this] { atualizar(); });
	relogio_.start(5000);
	atualizar();
	procurar();
	QTimer::singleShot(4000, &menu_, [this] { ler_redes(); montar_menu(); });
	tray_.show();
}

QIcon
Bandeja::icone() const
{
	QByteArray st = status_.value("wpa_state");
	if (st == "COMPLETED" && status_.contains("ip_address"))
		return QIcon::fromTheme(QString("network-wireless-%1").arg(barras(sinal_) * 25),
		    QIcon::fromTheme("network-wireless"));
	if (st == "COMPLETED" || st == "ASSOCIATING" || st == "4WAY_HANDSHAKE" ||
	    st == "GROUP_HANDSHAKE" || st == "AUTHENTICATING" || st == "ASSOCIATED")
		return QIcon::fromTheme("network-wireless-acquiring", QIcon::fromTheme("network-wireless"));
	if (!cabo_.ip.isEmpty())
		return QIcon::fromTheme("network-wired-activated", QIcon::fromTheme("network-wired"));
	if (placa_)
		return QIcon::fromTheme("network-wireless-disconnected", QIcon::fromTheme("network-wireless"));
	return QIcon::fromTheme("network-wired-disconnected", QIcon::fromTheme("network-wired"));
}

void
Bandeja::atualizar()
{
	placa_ = tem_placa_wifi();
	cabo_ = cabo();
	status_.clear();
	sinal_ = -100;
	if (placa_ && wpa_.abrir()) {
		status_ = campos(wpa_.pedir("STATUS"));
		if (status_.value("wpa_state") == "COMPLETED") {
			auto s = campos(wpa_.pedir("SIGNAL_POLL"));
			sinal_ = s.value("RSSI", "-100").toInt();
		}
	}
	tray_.setIcon(icone());
	QString dica;
	if (status_.value("wpa_state") == "COMPLETED") {
		dica = QString("Wi-Fi: %1").arg(QString::fromUtf8(desescapar(status_.value("ssid"))));
		if (status_.contains("ip_address"))
			dica += QString(" (%1)").arg(QString::fromLatin1(status_.value("ip_address")));
		else
			dica += " (esperando o endereço)";
	} else if (!cabo_.ip.isEmpty()) {
		dica = QString("Cabo: conectado (%1)").arg(cabo_.ip);
	} else if (placa_) {
		dica = "Wi-Fi: desconectado";
	} else {
		dica = "Sem rede";
	}
	if (!cabo_.ip.isEmpty() && status_.value("wpa_state") == "COMPLETED")
		dica += QString("\nCabo: conectado (%1)").arg(cabo_.ip);
	tray_.setToolTip(dica);
}

void
Bandeja::procurar()
{
	if (placa_ && wpa_.abrir())
		wpa_.pedir("SCAN");
}

// SCAN_RESULTS: "bssid / frequency / signal level / flags / ssid", uma por
// linha, com tab; a mesma rede aparece uma vez por ponto de acesso: fica o
// de sinal mais forte.
void
Bandeja::ler_redes()
{
	redes_.clear();
	if (!placa_ || !wpa_.abrir())
		return;
	QMap<QByteArray, int> guardadas;
	for (const QByteArray &l : wpa_.pedir("LIST_NETWORKS").split('\n').mid(1)) {
		QList<QByteArray> c = l.split('\t');
		if (c.size() >= 2)
			guardadas.insert(desescapar(c[1]), c[0].toInt());
	}
	QMap<QByteArray, Rede> por_nome;
	for (const QByteArray &l : wpa_.pedir("SCAN_RESULTS").split('\n').mid(1)) {
		QList<QByteArray> c = l.split('\t');
		if (c.size() < 5 || c[4].isEmpty())
			continue;  // rede oculta: sem nome
		Rede r;
		r.ssid = desescapar(c[4]);
		r.nome = QString::fromUtf8(r.ssid);
		r.sinal = c[2].toInt();
		r.flags = c[3];
		if (guardadas.contains(r.ssid)) {
			r.guardada = true;
			r.id = guardadas.value(r.ssid);
		}
		if (!por_nome.contains(r.ssid) || por_nome[r.ssid].sinal < r.sinal)
			por_nome.insert(r.ssid, r);
	}
	redes_ = por_nome.values();
	std::sort(redes_.begin(), redes_.end(), [](const Rede &a, const Rede &b) {
		return a.sinal > b.sinal;
	});
}

void
Bandeja::montar_menu()
{
	menu_.clear();
	QByteArray atual = status_.value("wpa_state") == "COMPLETED" ? desescapar(status_.value("ssid")) : QByteArray();

	// O estado, no alto.
	if (!cabo_.ip.isEmpty())
		menu_.addAction(QIcon::fromTheme("network-wired"),
		    QString("Cabo: conectado (%1)").arg(cabo_.ip))->setEnabled(false);
	if (!placa_) {
		if (cabo_.ip.isEmpty())
			menu_.addAction("Sem rede: nenhum cabo nem placa Wi-Fi")->setEnabled(false);
		menu_.addAction("Nenhuma placa Wi-Fi com driver (veja lume hardware)")->setEnabled(false);
		menu_.addSeparator();
		menu_.addAction("Fechar o ícone", [] { qApp->quit(); });
		return;
	}
	if (!wpa_.abrir()) {
		menu_.addAction("Wi-Fi: a placa ainda não está pronta")->setEnabled(false);
		menu_.addAction(QIcon::fromTheme("network-wireless"), "Preparar o Wi-Fi…", [this] { preparar(); });
		menu_.addSeparator();
		menu_.addAction("Fechar o ícone", [] { qApp->quit(); });
		return;
	}
	if (!atual.isEmpty()) {
		QString t = QString("Wi-Fi: %1").arg(QString::fromUtf8(atual));
		t += status_.contains("ip_address") ? QString(" (%1)").arg(QString::fromLatin1(status_.value("ip_address")))
		                                    : QString(" (esperando o endereço)");
		menu_.addAction(icone(), t)->setEnabled(false);
	} else {
		menu_.addAction(conectando_ ? "Wi-Fi: conectando…" : "Wi-Fi: desconectado")->setEnabled(false);
	}
	menu_.addSeparator();

	// As redes por perto.
	if (redes_.isEmpty())
		menu_.addAction("Procurando redes…")->setEnabled(false);
	for (const Rede &r : redes_) {
		QString nome = r.nome;
		if (!r.aberta())
			nome += "  🔒";
		QString icone = QString("network-wireless-%1%2").arg(barras(r.sinal) * 20)
		    .arg(r.aberta() ? "" : "-locked");
		QAction *a = menu_.addAction(QIcon::fromTheme(icone, QIcon::fromTheme("network-wireless")), nome,
		    [this, r] { conectar(r); });
		a->setCheckable(true);
		a->setChecked(r.ssid == atual);
		if (!r.suportada())
			a->setToolTip("Rede corporativa ou WEP: ainda não suportada");
	}
	menu_.addSeparator();
	menu_.addAction(QIcon::fromTheme("view-refresh"), "Procurar redes de novo", [this] {
		procurar();
		QTimer::singleShot(4000, &menu_, [this] { ler_redes(); });
	});

	// As redes guardadas: para esquecer.
	QMenu *guardadas = menu_.addMenu("Redes guardadas");
	bool alguma = false;
	for (const QByteArray &l : wpa_.pedir("LIST_NETWORKS").split('\n').mid(1)) {
		QList<QByteArray> c = l.split('\t');
		if (c.size() < 2)
			continue;
		int id = c[0].toInt();
		QString nome = QString::fromUtf8(desescapar(c[1]));
		guardadas->addAction(QString("Esquecer %1").arg(nome), [this, id, nome] { esquecer(id, nome); });
		alguma = true;
	}
	guardadas->setEnabled(alguma);

	if (!atual.isEmpty())
		menu_.addAction("Desconectar", [this] { wpa_.ok("DISCONNECT"); atualizar(); });
	else
		menu_.addAction("Reconectar", [this] { wpa_.ok("RECONNECT"); atualizar(); });
	menu_.addSeparator();
	menu_.addAction("Fechar o ícone", [] { qApp->quit(); });
}

void
Bandeja::conectar(const Rede &r)
{
	if (!r.suportada()) {
		QMessageBox::information(nullptr, "Rede",
		    QString("“%1” é uma rede corporativa (802.1X) ou WEP, que o ícone de rede "
		            "ainda não sabe configurar.").arg(r.nome));
		return;
	}
	if (r.guardada) {
		wpa_.ok("SELECT_NETWORK " + QByteArray::number(r.id));
		conectando_ = true;
		conferir_conexao(r.id, r.nome, false, 0);
		return;
	}
	QByteArray senha;
	if (!r.aberta()) {
		bool ok = terminal_;
		QString s = terminal_ ? QString::fromUtf8(senha_pronta_) :
		    QInputDialog::getText(nullptr, "Senha do Wi-Fi",
		    QString("Senha de “%1”:").arg(r.nome), QLineEdit::Password, QString(), &ok);
		if (!ok)
			return;
		senha = s.toUtf8();
		if (r.psk() && (senha.size() < 8 || senha.size() > 63)) {
			QMessageBox::warning(nullptr, "Senha do Wi-Fi",
			    "A senha de uma rede WPA tem de 8 a 63 caracteres.");
			return;
		}
	}
	QByteArray id = wpa_.pedir("ADD_NETWORK").trimmed();
	bool ok = !id.isEmpty() && id != "FAIL";
	// O nome em hexadecimal: sem problema com aspas nem acentos.
	ok = ok && wpa_.ok("SET_NETWORK " + id + " ssid " + r.ssid.toHex());
	if (ok && r.aberta()) {
		ok = wpa_.ok("SET_NETWORK " + id + " key_mgmt NONE");
	} else if (ok && r.psk()) {
		// WPA2 (e o modo de transição do WPA3): só a chave vai, não a senha.
		QByteArray k = chave_psk(senha, r.ssid);
		ok = !k.isEmpty() && wpa_.ok("SET_NETWORK " + id + " psk " + k);
		if (ok && r.sae())
			wpa_.ok("SET_NETWORK " + id + " key_mgmt WPA-PSK SAE");
	} else if (ok && r.sae()) {
		// Só WPA3: a senha vai entre aspas (o SAE não usa a chave do PBKDF2).
		if (!cabe_entre_aspas(senha)) {
			QMessageBox::warning(nullptr, "Senha do Wi-Fi",
			    "Senhas com aspas ainda não funcionam em redes só WPA3; use o lume wifi.");
			wpa_.ok("REMOVE_NETWORK " + id);
			return;
		}
		ok = wpa_.ok("SET_NETWORK " + id + " key_mgmt SAE") &&
		     wpa_.ok("SET_NETWORK " + id + " ieee80211w 2") &&
		     wpa_.ok("SET_NETWORK " + id + " sae_password \"" + senha + "\"");
	}
	senha.fill('\0');
	if (!ok || !wpa_.ok("SELECT_NETWORK " + id)) {
		if (!id.isEmpty() && id != "FAIL")
			wpa_.ok("REMOVE_NETWORK " + id);
		QMessageBox::warning(nullptr, "Rede", QString("Não deu para configurar “%1”.").arg(r.nome));
		return;
	}
	conectando_ = true;
	conferir_conexao(id.toInt(), r.nome, true, 0);
}

// Espera o wpa_supplicant completar (até uns 25 s). Rede nova que não
// conectou: a senha deve estar errada; ela sai da lista em vez de ficar
// guardada assim.
void
Bandeja::conferir_conexao(int id, const QString &nome, bool nova, int tentativas)
{
	atualizar();
	auto st = status_.value("wpa_state");
	if (st == "COMPLETED" && status_.value("id").toInt() == id) {
		conectando_ = false;
		if (nova)
			wpa_.ok("SAVE_CONFIG");
		avisar(QString("Conectado a “%1”.").arg(nome), true);
		return;
	}
	if (tentativas >= 25) {
		conectando_ = false;
		if (nova) {
			wpa_.ok("REMOVE_NETWORK " + QByteArray::number(id));
			wpa_.ok("RECONNECT");
		}
		avisar(QString("Não conectou a “%1”. A senha está certa?").arg(nome), false);
		return;
	}
	QTimer::singleShot(1000, &menu_, [=] { conferir_conexao(id, nome, nova, tentativas + 1); });
}

void
Bandeja::esquecer(int id, const QString &nome)
{
	if (QMessageBox::question(nullptr, "Esquecer a rede",
	    QString("Esquecer “%1”? A senha guardada será apagada.").arg(nome)) != QMessageBox::Yes)
		return;
	wpa_.ok("REMOVE_NETWORK " + QByteArray::number(id));
	wpa_.ok("SAVE_CONFIG");
	atualizar();
}

void
Bandeja::preparar()
{
	auto *p = new QProcess(&menu_);
	QObject::connect(p, &QProcess::finished, &menu_, [this, p](int code, QProcess::ExitStatus) {
		p->deleteLater();
		if (code == 0) {
			QTimer::singleShot(3000, &menu_, [this] { atualizar(); procurar(); });
			QTimer::singleShot(7000, &menu_, [this] { ler_redes(); });
		} else if (code != 126 && code != 127) {
			QMessageBox::warning(nullptr, "Wi-Fi", "Não deu para preparar o Wi-Fi:\n" +
			    QString::fromUtf8(p->readAll()));
		}
	});
	p->setProcessChannelMode(QProcess::MergedChannels);
	p->start("pkexec", {LUME_WIFI, "preparar"});
}

void
Bandeja::conectar_no_terminal(const QString &nome)
{
	terminal_ = true;
	senha_pronta_ = qgetenv("LUME_REDE_SENHA");
	atualizar();
	procurar();
	usleep(2000000);
	ler_redes();
	for (const Rede &r : redes_) {
		if (r.nome == nome) {
			conectar(r);
			return;
		}
	}
	avisar(QString("Rede “%1” não encontrada.").arg(nome), false);
}

void
Bandeja::estado_no_terminal()
{
	atualizar();
	procurar();
	usleep(4000000);
	ler_redes();
	printf("cabo: %s\n", cabo_.ip.isEmpty() ? "-" : qPrintable(cabo_.interface + " " + cabo_.ip));
	printf("placa wifi: %s\n", placa_ ? "sim" : "não");
	printf("wpa_supplicant: %s\n", wpa_.aberto() ? "sim" : "não");
	printf("estado: %s\n", status_.value("wpa_state", "-").constData());
	printf("rede: %s\n", desescapar(status_.value("ssid", "-")).constData());
	printf("ip: %s\n", status_.value("ip_address", "-").constData());
	for (const Rede &r : redes_)
		printf("  %-32s %4d dBm %d/4 %s%s\n", r.nome.toUtf8().constData(), r.sinal, barras(r.sinal),
		    r.flags.constData(), r.guardada ? " (guardada)" : "");
}

int
main(int argc, char **argv)
{
	bool estado = argc > 1 && strcmp(argv[1], "--estado") == 0;
	bool conectar = argc > 2 && strcmp(argv[1], "--conectar") == 0;
	// --estado e --conectar: sem tela, no terminal.
	if (estado || conectar)
		setenv("QT_QPA_PLATFORM", "offscreen", 1);
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	app.setApplicationName("lume-rede");
	app.setDesktopFileName("lume-rede");
	QTranslator qt;
	if (qt.load(QLocale(), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
		app.installTranslator(&qt);
	Bandeja b;
	if (estado) {
		b.estado_no_terminal();
		return 0;
	}
	if (conectar)
		QTimer::singleShot(0, &app, [&] { b.conectar_no_terminal(QString::fromUtf8(argv[2])); });
	return app.exec();
}
