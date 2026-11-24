// SPDX-License-Identifier: BSD-2-Clause
//
// LumeBSD, o escritório: o gráfico vinculado (um Replicant dentro de outro
// app). Um gráfico copiado nos Números leva junto de onde veio (a planilha
// e qual gráfico); colado no Páginas ou no Palco, ele se atualiza quando a
// planilha é gravada, e ao abrir o documento.
//
// O vínculo vai no nome do gráfico no arquivo (lume-vinculo:ARQUIVO#N: o
// name do docPr e do cNvPr no Office, o draw:name no ODF), que o Word, o
// PowerPoint e o LibreOffice guardam sem mexer. Os dados de agora vêm do
// próprio lume-numeros (--copiar-grafico ARQUIVO N), num processo.

#pragma once

#include "grafico.h"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include <functional>
#include <memory>

// Na área de transferência, junto do gráfico: ARQUIVO#N.
inline const char *MIME_VINCULO = "application/x-lume-grafico-vinculo";

struct Vinculo {
	QString arq;
	int n = 1;
	bool ok() const { return !arq.isEmpty(); }
	QString texto() const { return arq + "#" + QString::number(n); }
	// O que vai no nome do gráfico no arquivo.
	QString nome() const { return "lume-vinculo:" + texto(); }
	// Para os resumos (sem a pasta).
	QString curto() const { return QFileInfo(arq).fileName() + "#" + QString::number(n); }
	bool operator==(const Vinculo &o) const { return arq == o.arq && n == o.n; }
};

// De ARQUIVO#N ou do nome no arquivo (lume-vinculo:ARQUIVO#N).
inline Vinculo lerVinculo(QString s)
{
	Vinculo v;
	if (s.startsWith("lume-vinculo:"))
		s = s.mid(13);
	else if (!s.startsWith('/'))
		return v;
	const int k = s.lastIndexOf('#');
	if (k <= 0)
		return v;
	bool ok = false;
	const int n = s.mid(k + 1).toInt(&ok);
	if (!ok || n < 1 || !s.startsWith('/'))
		return v;
	v.arq = s.left(k);
	v.n = n;
	return v;
}

// O lume-numeros (LUME_NUMEROS troca: os testes).
inline QString programaNumeros()
{
	const QString e = qEnvironmentVariable("LUME_NUMEROS");
	return e.isEmpty() ? QString("lume-numeros") : e;
}

inline QStringList argumentosVinculo(const Vinculo &v) { return {"--copiar-grafico", v.arq, QString::number(v.n)}; }

// Os dados de agora, esperando (a linha de comando: --atualizar).
inline bool dadosDoVinculo(const Vinculo &v, DadosGrafico &g)
{
	if (!v.ok() || !QFileInfo::exists(v.arq))
		return false;
	QProcess p;
	p.start(programaNumeros(), argumentosVinculo(v));
	if (!p.waitForFinished(20000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
		return false;
	return lerDados(QString::fromUtf8(p.readAllStandardOutput()).trimmed(), g);
}

// Os dados de agora, sem esperar (a janela): f(ok, dados).
inline void buscarVinculo(const Vinculo &v, QObject *dono, const std::function<void(bool, const DadosGrafico &)> &f)
{
	if (!v.ok() || !QFileInfo::exists(v.arq)) {
		QTimer::singleShot(0, dono, [f] { f(false, DadosGrafico()); });
		return;
	}
	auto *p = new QProcess(dono);
	QObject::connect(p, &QProcess::finished, dono, [p, f](int codigo, QProcess::ExitStatus st) {
		DadosGrafico g;
		const bool ok = st == QProcess::NormalExit && codigo == 0 &&
		                lerDados(QString::fromUtf8(p->readAllStandardOutput()).trimmed(), g);
		p->deleteLater();
		f(ok, g);
	});
	QObject::connect(p, &QProcess::errorOccurred, dono, [p, f](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart)
			return;
		p->deleteLater();
		f(false, DadosGrafico());
	});
	p->start(programaNumeros(), argumentosVinculo(v));
}

// Vigia as planilhas dos gráficos vinculados: mudou (os Números gravam num
// arquivo novo e trocam, por isso a pasta também), chama mudou().
class VigiaVinculos
{
public:
	std::function<void()> mudou;

	explicit VigiaVinculos(QObject *dono) : olho_(new QFileSystemWatcher(dono)), espera_(new QTimer(dono))
	{
		espera_->setSingleShot(true);
		espera_->setInterval(500);
		QObject::connect(espera_, &QTimer::timeout, dono, [this] {
			vigiar(arquivos_);
			if (mudou)
				mudou();
		});
		QObject::connect(olho_, &QFileSystemWatcher::fileChanged, dono, [this] { espera_->start(); });
		QObject::connect(olho_, &QFileSystemWatcher::directoryChanged, dono, [this] {
			for (const QString &a : arquivos_)
				if (QFileInfo::exists(a) && !olho_->files().contains(a)) {
					espera_->start();
					return;
				}
		});
	}

	void vigiar(const QStringList &arquivos)
	{
		arquivos_ = arquivos;
		arquivos_.removeDuplicates();
		if (!olho_->files().isEmpty())
			olho_->removePaths(olho_->files());
		if (!olho_->directories().isEmpty())
			olho_->removePaths(olho_->directories());
		QStringList pastas;
		for (const QString &a : arquivos_) {
			if (QFileInfo::exists(a))
				olho_->addPath(a);
			pastas << QFileInfo(a).absolutePath();
		}
		pastas.removeDuplicates();
		for (const QString &p : pastas)
			if (QFileInfo::exists(p))
				olho_->addPath(p);
	}

private:
	QFileSystemWatcher *olho_;
	QTimer *espera_;
	QStringList arquivos_;
};
