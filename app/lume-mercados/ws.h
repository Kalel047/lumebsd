// SPDX-License-Identifier: BSD-2-Clause
//
// ws.h - um cliente de WebSocket (RFC 6455) do tamanho do que os Mercados
// precisam: abrir, mandar texto, receber mensagens, responder ao ping. O
// pacote qt6-base não traz o QtWebSockets; isto fica em cima do QSslSocket.

#pragma once

#include "base.h"

class FioWs : public QObject {
public:
	explicit FioWs(QObject *pai) : QObject(pai) {}

	std::function<void()> abriu;
	std::function<void(const QByteArray &)> mensagem;
	std::function<void(const QString &)> fechou;

	void abrir(const QUrl &url)
	{
		fechar();
		url_ = url;
		aberto_ = false;
		buf_.clear();
		s_ = new QSslSocket(this);
		const bool seguro = url.scheme() == "wss";
		auto cumprimentar = [this] {
			QByteArray chave(16, 0);
			for (char &c : chave)
				c = char(QRandomGenerator::global()->bounded(256));
			QString caminho = url_.path(QUrl::FullyEncoded);
			if (caminho.isEmpty())
				caminho = "/";
			if (url_.hasQuery())
				caminho += "?" + url_.query(QUrl::FullyEncoded);
			s_->write("GET " + caminho.toLatin1() + " HTTP/1.1\r\nHost: " + url_.host().toLatin1() +
			    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + chave.toBase64() +
			    "\r\nSec-WebSocket-Version: 13\r\nUser-Agent: lume-mercados/1.0 (LumeBSD)\r\n\r\n");
		};
		if (seguro)
			connect(s_, &QSslSocket::encrypted, this, cumprimentar);
		else
			connect(s_, &QSslSocket::connected, this, cumprimentar);
		connect(s_, &QSslSocket::readyRead, this, [this] { ler(); });
		connect(s_, &QSslSocket::disconnected, this, [this] { acabou("A conexão caiu."); });
		connect(s_, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { acabou(s_ ? s_->errorString() : QString()); });
		if (seguro)
			s_->connectToHostEncrypted(url.host(), quint16(url.port(443)));
		else
			s_->connectToHost(url.host(), quint16(url.port(80)));
	}
	bool aberto() const { return aberto_; }
	void enviar(const QString &texto) { quadro(1, texto.toUtf8()); }
	void fechar()
	{
		if (!s_)
			return;
		QSslSocket *s = s_;
		s_ = nullptr;
		aberto_ = false;
		s->disconnect(this);
		s->abort();
		s->deleteLater();
	}

private:
	void acabou(const QString &motivo)
	{
		if (!s_)
			return;
		fechar();
		if (fechou)
			fechou(motivo);
	}
	// Do cliente para o servidor, todo quadro vai mascarado.
	void quadro(int tipo, const QByteArray &dados)
	{
		if (!s_ || !aberto_)
			return;
		QByteArray q;
		q += char(0x80 | tipo);
		const qsizetype n = dados.size();
		if (n < 126)
			q += char(0x80 | n);
		else if (n < 65536) {
			q += char(0x80 | 126);
			q += char(n >> 8);
			q += char(n & 0xff);
		} else {
			q += char(0x80 | 127);
			for (int k = 7; k >= 0; k--)
				q += char((quint64(n) >> (8 * k)) & 0xff);
		}
		char m[4];
		for (char &c : m)
			c = char(QRandomGenerator::global()->bounded(256));
		q.append(m, 4);
		for (qsizetype k = 0; k < n; k++)
			q += char(dados[k] ^ m[k % 4]);
		s_->write(q);
	}
	void ler()
	{
		if (!s_)
			return;
		buf_ += s_->readAll();
		if (!aberto_) {
			const int fim = buf_.indexOf("\r\n\r\n");
			if (fim < 0)
				return;
			const QByteArray cab = buf_.left(fim);
			buf_.remove(0, fim + 4);
			if (!cab.startsWith("HTTP/1.1 101")) {
				acabou(QStringLiteral("O servidor recusou a conexão (%1).").arg(QString::fromLatin1(cab.left(cab.indexOf('\r')))));
				return;
			}
			aberto_ = true;
			if (abriu)
				abriu();
		}
		while (s_ && buf_.size() >= 2) {
			const quint8 b0 = quint8(buf_[0]), b1 = quint8(buf_[1]);
			quint64 n = b1 & 0x7f;
			int pos = 2;
			if (n == 126) {
				if (buf_.size() < 4)
					return;
				n = (quint8(buf_[2]) << 8) | quint8(buf_[3]);
				pos = 4;
			} else if (n == 127) {
				if (buf_.size() < 10)
					return;
				n = 0;
				for (int k = 2; k < 10; k++)
					n = (n << 8) | quint8(buf_[k]);
				pos = 10;
			}
			const bool mascarado = b1 & 0x80;
			if (quint64(buf_.size()) < pos + (mascarado ? 4 : 0) + n)
				return;
			QByteArray dados = buf_.mid(pos + (mascarado ? 4 : 0), qsizetype(n));
			if (mascarado)
				for (qsizetype k = 0; k < dados.size(); k++)
					dados[k] = char(dados[k] ^ buf_[pos + int(k % 4)]);
			buf_.remove(0, pos + (mascarado ? 4 : 0) + qsizetype(n));
			const int tipo = b0 & 0x0f;
			if (tipo == 8) {
				acabou(dados.size() > 2 ? QString::fromUtf8(dados.mid(2)) : QStringLiteral("O servidor encerrou."));
				return;
			}
			if (tipo == 9) {
				quadro(10, dados);
				continue;
			}
			if (tipo == 10)
				continue;
			// Uma mensagem pode vir em pedaços (continuação, tipo 0).
			parcial_ += dados;
			if (b0 & 0x80) {
				const QByteArray inteira = parcial_;
				parcial_.clear();
				if (mensagem)
					mensagem(inteira);
			}
		}
	}

	QSslSocket *s_ = nullptr;
	QUrl url_;
	QByteArray buf_, parcial_;
	bool aberto_ = false;
};
