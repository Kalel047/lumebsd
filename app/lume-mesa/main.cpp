// SPDX-License-Identifier: BSD-2-Clause
//
// lume-mesa: a mesa dos Replicants (como a do BeOS). Entra com a sessão,
// roda um processo por replicante (o app no modo --replicante ID) e o traz
// de volta se cair; vê a mesa mudar (um replicante novo, um que saiu) e
// acompanha. Os replicantes e a mesa estão em ../comum/replicante.h.
//
//   lume-mesa                         roda a mesa (uma por sessão)
//   lume-mesa --listar                o que está na mesa
//   lume-mesa --por APP DADOS...      põe um replicante (escreve o id)
//   lume-mesa --tirar ID              tira

#include "../comum/replicante.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <memory>

namespace {

// O SIGTERM (o fim da sessão, um kill) fecha a mesa direito: pelo laço do
// Qt (um par de sockets), e não no meio do que ela estiver fazendo.
int sinais[2] = {-1, -1};

void aoSinal(int)
{
	const char c = 1;
	if (write(sinais[1], &c, 1) < 0) {
	}
}

struct Processo {
	QProcess *p = nullptr;
	QList<qint64> quedas; // quando caiu (ms): mais de 5 num minuto, desiste
};

class Mesa
{
public:
	Mesa()
	{
		olho_.addPath(QFileInfo(arquivoMesa()).absolutePath());
		if (QFileInfo::exists(arquivoMesa()))
			olho_.addPath(arquivoMesa());
		auto mudou = [this] { espera_.start(); };
		QObject::connect(&olho_, &QFileSystemWatcher::fileChanged, qApp, mudou);
		QObject::connect(&olho_, &QFileSystemWatcher::directoryChanged, qApp, mudou);
		espera_.setSingleShot(true);
		espera_.setInterval(250);
		QObject::connect(&espera_, &QTimer::timeout, qApp, [this] { acompanhar(); });
		acompanhar();
	}

	~Mesa()
	{
		// O fim da sessão: os replicantes saem com a mesa (e voltam com ela).
		for (Processo &x : rodando_)
			if (x.p) {
				x.p->disconnect();
				x.p->terminate();
				x.p->waitForFinished(2000);
			}
	}

private:
	void acompanhar()
	{
		// (O QSettings grava num arquivo novo: o olho precisa dele de novo.)
		if (QFileInfo::exists(arquivoMesa()) && !olho_.files().contains(arquivoMesa()))
			olho_.addPath(arquivoMesa());
		const std::vector<Replicante> mesa = lerMesa();
		QSet<QString> ids;
		int fila = 0;
		for (const Replicante &r : mesa) {
			ids.insert(r.id);
			if (!rodando_.contains(r.id) || !rodando_[r.id].p)
				if (!desistiu_.contains(r.id))
					rodar(r, 700 * fila++);
		}
		for (auto it = rodando_.begin(); it != rodando_.end();) {
			if (!ids.contains(it.key())) {
				if (QProcess *p = it->p) {
					// (O "Tirar da mesa" já fecha o replicante; um tirado
					// pelo lume-mesa --tirar recebe o SIGTERM.)
					p->disconnect();
					QObject::connect(p, &QProcess::finished, p, &QObject::deleteLater);
					p->terminate();
					QTimer::singleShot(3000, p, [p] { p->kill(); });
				}
				desistiu_.remove(it.key());
				it = rodando_.erase(it);
			} else {
				++it;
			}
		}
	}

	// Um de cada vez (atraso em ms): um replicante novo escolhe o lugar
	// vendo os que já estão na mesa.
	void rodar(const Replicante &r, int atraso = 0)
	{
		auto *p = new QProcess(qApp);
		p->setProcessChannelMode(QProcess::ForwardedChannels);
		rodando_[r.id].p = p;
		const QString id = r.id;
		QObject::connect(p, &QProcess::finished, qApp, [this, p, id](int codigo, QProcess::ExitStatus st) {
			p->deleteLater();
			if (!rodando_.contains(id))
				return;
			Processo &x = rodando_[id];
			x.p = nullptr;
			Replicante r;
			if (!acharNaMesa(id, r)) {
				rodando_.remove(id);
				return;
			}
			// Saiu sem tirar da mesa: caiu. Volta, se não cair demais.
			const qint64 agora = QDateTime::currentMSecsSinceEpoch();
			x.quedas.append(agora);
			while (!x.quedas.isEmpty() && agora - x.quedas.first() > 60000)
				x.quedas.removeFirst();
			if (x.quedas.size() > 5) {
				std::fprintf(stderr, "lume-mesa: o replicante %s (%s) caiu demais (saída %d%s); fica parado\n",
				             qPrintable(id), qPrintable(r.app), codigo, st == QProcess::CrashExit ? ", quebrou" : "");
				desistiu_.insert(id);
				return;
			}
			QTimer::singleShot(1000 * int(x.quedas.size()), qApp, [this, id] {
				Replicante r;
				if (acharNaMesa(id, r) && rodando_.contains(id) && !rodando_[id].p)
					rodar(r);
			});
		});
		const QString app = r.app;
		QTimer::singleShot(atraso, p, [p, app, id] { p->start(app, {"--replicante", id}); });
	}

	QFileSystemWatcher olho_;
	QTimer espera_;
	QHash<QString, Processo> rodando_;
	QSet<QString> desistiu_;
};

int uso()
{
	std::fprintf(stderr,
	             "uso: lume-mesa\n"
	             "     lume-mesa --listar\n"
	             "     lume-mesa --por APP DADOS...\n"
	             "     lume-mesa --tirar ID\n");
	return 2;
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	app.setApplicationName("lume-mesa");
	const QStringList a = app.arguments().mid(1);
	if (!a.isEmpty()) {
		if (a[0] == "--listar" && a.size() == 1) {
			for (const Replicante &r : lerMesa()) {
				const QRect g = r.geo;
				std::printf("%s %s %s%s\n", qPrintable(r.id), qPrintable(r.app), qPrintable(r.dados.join(' ')),
				            g.isEmpty() ? "" : qPrintable(QString(" @%1,%2 %3x%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height())));
			}
			return 0;
		}
		if (a[0] == "--por" && a.size() >= 3) {
			if (!appDaMesa(a[1])) {
				std::fprintf(stderr, "lume-mesa: %s não é um app do LumeBSD\n", qPrintable(a[1]));
				return 1;
			}
			std::printf("%s\n", qPrintable(porNaMesa(a[1], a.mid(2))));
			return 0;
		}
		if (a[0] == "--tirar" && a.size() == 2) {
			Replicante r;
			if (!acharNaMesa(a[1], r)) {
				std::fprintf(stderr, "lume-mesa: não há o replicante %s\n", qPrintable(a[1]));
				return 1;
			}
			tirarDaMesa(a[1]);
			return 0;
		}
		return uso();
	}
	// Uma mesa por sessão: a segunda sai (a primeira vê a mesa mudar).
	QDir().mkpath(QFileInfo(arquivoMesa()).absolutePath());
	QLockFile uma(arquivoMesa() + ".rodando");
	uma.setStaleLockTime(0);
	if (!uma.tryLock(0))
		return 0;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sinais) == 0) {
		auto *n = new QSocketNotifier(sinais[0], QSocketNotifier::Read, &app);
		QObject::connect(n, &QSocketNotifier::activated, &app, [] { QCoreApplication::quit(); });
		std::signal(SIGTERM, aoSinal);
		std::signal(SIGINT, aoSinal);
		std::signal(SIGHUP, aoSinal);
	}
	auto mesa = std::make_unique<Mesa>();
	const int r = app.exec();
	mesa.reset();
	return r;
}
