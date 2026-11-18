// SPDX-License-Identifier: BSD-2-Clause
//
// telas3.h - o mapa do mundo e o que se move nele: os aviões (ADS-B, de
// redes abertas de receptores) e os navios (AIS).

#pragma once

#include "telas2.h"
#include "ws.h"

// --- O mapa ------------------------------------------------------------------

struct Alvo {
	QString id, nome;
	double lon = 0, lat = 0, rumo = NAO;
};

static double
mercator(double lat)
{
	lat = std::clamp(lat, -85.0, 85.0);
	return std::log(std::tan(M_PI / 4 + lat * M_PI / 360)) * 180 / M_PI;
}

static double
latitudeDe(double y)
{
	return (2 * std::atan(std::exp(y * M_PI / 180)) - M_PI / 2) * 180 / M_PI;
}

// As cidades que servem de referência no mapa.
static const struct {
	const char *nome;
	double lon, lat;
} CIDADES[] = {
	{"São Paulo", -46.63, -23.55}, {"Rio de Janeiro", -43.2, -22.9}, {"Brasília", -47.88, -15.79}, {"Belo Horizonte", -43.94, -19.92},
	{"Curitiba", -49.27, -25.43}, {"Porto Alegre", -51.23, -30.03}, {"Salvador", -38.5, -12.97}, {"Recife", -34.88, -8.05},
	{"Fortaleza", -38.54, -3.73}, {"Manaus", -60.02, -3.1}, {"Belém", -48.5, -1.45}, {"Campinas", -47.06, -22.9},
	{"Santos", -46.33, -23.96}, {"Florianópolis", -48.55, -27.6}, {"Goiânia", -49.25, -16.68}, {"Vitória", -40.31, -20.32},
	{"Buenos Aires", -58.38, -34.6}, {"Santiago", -70.65, -33.45}, {"Lima", -77.04, -12.05}, {"Bogotá", -74.07, 4.71},
	{"Cidade do México", -99.13, 19.43}, {"Miami", -80.19, 25.76}, {"Nova York", -74.0, 40.71}, {"Chicago", -87.63, 41.88},
	{"Los Angeles", -118.24, 34.05}, {"Lisboa", -9.14, 38.72}, {"Madri", -3.7, 40.42}, {"Londres", -0.13, 51.51},
	{"Paris", 2.35, 48.86}, {"Frankfurt", 8.68, 50.11}, {"Roma", 12.5, 41.9}, {"Istambul", 28.98, 41.01},
	{"Dubai", 55.27, 25.2}, {"Joanesburgo", 28.05, -26.2}, {"Mumbai", 72.88, 19.08}, {"Singapura", 103.82, 1.35},
	{"Hong Kong", 114.17, 22.32}, {"Xangai", 121.47, 31.23}, {"Tóquio", 139.69, 35.69}, {"Sydney", 151.21, -33.87},
	{"Helsinque", 24.94, 60.17}, {"Estocolmo", 18.07, 59.33}, {"Tallinn", 24.75, 59.44}, {"São Petersburgo", 30.32, 59.94},
	{"Turku", 22.27, 60.45}, {"Riga", 24.1, 56.95},
};

// As costas e as fronteiras (Natural Earth, domínio público), em tinta sobre
// o papel; arrasta com o mouse, aproxima com a roda.
class Mapa : public QWidget {
public:
	Mapa()
	{
		setMinimumHeight(430);
		setMouseTracking(true);
		setCursor(Qt::OpenHandCursor);
		QFile f(arquivoDoSistema("lume-mercados-mapa.txt"));
		if (f.open(QIODevice::ReadOnly))
			while (!f.atEnd()) {
				const QByteArray l = f.readLine().trimmed();
				if (l.isEmpty() || l[0] == '#')
					continue;
				Traco t;
				t.fronteira = l[0] == 'f';
				for (const QByteArray &par : l.mid(2).split(' ')) {
					const int v = par.indexOf(',');
					if (v > 0)
						t.pontos << QPointF(par.left(v).toDouble(), mercator(par.mid(v + 1).toDouble()));
				}
				t.caixa = t.pontos.boundingRect();
				tracos_ << t;
			}
		quieto_.setSingleShot(true);
		quieto_.setInterval(500);
		QObject::connect(&quieto_, &QTimer::timeout, this, [this] {
			if (moveu)
				moveu();
		});
	}

	std::function<void()> moveu;
	std::function<void(const QString &)> escolheu;

	void irPara(double lon, double lat, double grausDeLargura)
	{
		cx_ = lon;
		cy_ = mercator(lat);
		z_ = std::max(1.0, width() / grausDeLargura);
		update();
		quieto_.start();
	}
	double lon() const { return cx_; }
	double lat() const { return latitudeDe(cy_); }
	// A caixa que se vê: oeste, sul, leste, norte.
	void limites(double *o, double *s, double *l, double *n) const
	{
		*o = cx_ - width() / 2.0 / z_;
		*l = cx_ + width() / 2.0 / z_;
		*s = latitudeDe(cy_ - height() / 2.0 / z_);
		*n = latitudeDe(cy_ + height() / 2.0 / z_);
	}
	// Do centro ao canto, em quilômetros.
	double raioKm() const
	{
		double o, s, l, n;
		limites(&o, &s, &l, &n);
		const double dy = (n - s) / 2 * 111.2, dx = (l - o) / 2 * 111.2 * std::cos(lat() * M_PI / 180);
		return std::sqrt(dx * dx + dy * dy);
	}
	void definir(const QVector<Alvo> &a)
	{
		alvos_ = a;
		update();
	}
	void destacar(const QString &id)
	{
		destaque_ = id;
		update();
	}
	bool temMapa() const { return !tracos_.isEmpty(); }

protected:
	QPointF tela(double lon, double lat) const
	{
		return QPointF(width() / 2.0 + (lon - cx_) * z_, height() / 2.0 - (mercator(lat) - cy_) * z_);
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QColor tinta = palette().color(QPalette::WindowText), papel = palette().color(QPalette::Base);
		const QColor segunda = papel.lightness() < 128 ? QColor(0x7c, 0x77, 0x6d) : QColor(0x9b, 0x96, 0x8b);
		p.fillRect(rect(), papel);
		const QRectF vista(cx_ - width() / 2.0 / z_, cy_ - height() / 2.0 / z_, width() / z_, height() / z_);
		p.save();
		p.translate(width() / 2.0, height() / 2.0);
		p.scale(z_, -z_);
		p.translate(-cx_, -cy_);
		for (const Traco &t : tracos_) {
			if (!t.caixa.intersects(vista) && !(t.caixa.width() == 0 || t.caixa.height() == 0))
				continue;
			QPen pen(t.fronteira ? segunda : tinta, t.fronteira ? 0.8 : 1.1, t.fronteira ? Qt::DotLine : Qt::SolidLine);
			pen.setCosmetic(true);
			p.setPen(pen);
			p.drawPolyline(t.pontos);
		}
		p.restore();
		QFont serifa("IBM Plex Serif");
		serifa.setPixelSize(12);
		serifa.setItalic(true);
		p.setFont(serifa);
		for (const auto &c : CIDADES) {
			const QPointF t = tela(c.lon, c.lat);
			if (!rect().contains(t.toPoint()))
				continue;
			p.setPen(QPen(segunda, 1));
			p.drawLine(t + QPointF(-3, 0), t + QPointF(3, 0));
			p.drawLine(t + QPointF(0, -3), t + QPointF(0, 3));
			if (z_ > 4)
				p.drawText(t + QPointF(6, -5), QString::fromUtf8(c.nome));
		}
		QFont mono("IBM Plex Mono");
		mono.setPixelSize(10);
		p.setFont(mono);
		const bool nomes = alvos_.size() <= 120;
		for (const Alvo &a : alvos_) {
			const QPointF c = tela(a.lon, a.lat);
			if (!rect().adjusted(-20, -20, 20, 20).contains(c.toPoint()))
				continue;
			const bool d = a.id == destaque_;
			p.save();
			p.translate(c);
			if (std::isfinite(a.rumo)) {
				p.rotate(a.rumo);
				const double t = d ? 9 : 6;
				QPolygonF seta;
				seta << QPointF(0, -t) << QPointF(t * 0.6, t * 0.8) << QPointF(0, t * 0.35) << QPointF(-t * 0.6, t * 0.8);
				p.setPen(QPen(tinta, 1));
				p.setBrush(d ? tinta : papel);
				p.drawPolygon(seta);
			} else {
				p.setPen(QPen(tinta, 1));
				p.setBrush(d ? tinta : papel);
				p.drawEllipse(QPointF(0, 0), d ? 5 : 3, d ? 5 : 3);
			}
			p.restore();
			if ((nomes || d) && !a.nome.isEmpty()) {
				if (d) {
					const QRectF r(c.x() + 10, c.y() - 8, QFontMetrics(mono).horizontalAdvance(a.nome) + 8, 15);
					p.fillRect(r, tinta);
					p.setPen(papel);
					p.drawText(r, Qt::AlignCenter, a.nome);
				} else {
					p.setPen(segunda);
					p.drawText(c + QPointF(8, 4), a.nome);
				}
			}
		}
		p.setPen(QPen(tinta, 1));
		p.setBrush(Qt::NoBrush);
		p.drawRect(rect().adjusted(0, 0, -1, -1));
		if (tracos_.isEmpty()) {
			p.setPen(segunda);
			p.drawText(rect(), Qt::AlignCenter, "Sem o desenho do mapa (lume-mercados-mapa.txt).");
		}
	}
	void mousePressEvent(QMouseEvent *e) override
	{
		pega_ = apertou_ = e->position();
		setCursor(Qt::ClosedHandCursor);
	}
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (!(e->buttons() & Qt::LeftButton))
			return;
		cx_ -= (e->position().x() - pega_.x()) / z_;
		cy_ += (e->position().y() - pega_.y()) / z_;
		cy_ = std::clamp(cy_, -150.0, 150.0);
		pega_ = e->position();
		update();
		quieto_.start();
	}
	void mouseReleaseEvent(QMouseEvent *e) override
	{
		setCursor(Qt::OpenHandCursor);
		if ((e->position() - apertou_).manhattanLength() > 4)
			return;
		// Um clique parado: o alvo mais perto.
		double melhor = 14;
		QString id;
		for (const Alvo &a : alvos_) {
			const double d = QLineF(tela(a.lon, a.lat), e->position()).length();
			if (d < melhor) {
				melhor = d;
				id = a.id;
			}
		}
		if (!id.isEmpty()) {
			destacar(id);
			if (escolheu)
				escolheu(id);
		}
	}
	void wheelEvent(QWheelEvent *e) override
	{
		const double f = std::pow(1.0015, e->angleDelta().y());
		const QPointF m = e->position();
		const double lonM = cx_ + (m.x() - width() / 2.0) / z_, yM = cy_ - (m.y() - height() / 2.0) / z_;
		z_ = std::clamp(z_ * f, 1.5, 4000.0);
		cx_ = lonM - (m.x() - width() / 2.0) / z_;
		cy_ = yM + (m.y() - height() / 2.0) / z_;
		update();
		quieto_.start();
		e->accept();
	}

private:
	struct Traco {
		QPolygonF pontos;
		QRectF caixa;
		bool fronteira = false;
	};
	QVector<Traco> tracos_;
	QVector<Alvo> alvos_;
	QString destaque_;
	double cx_ = -50, cy_ = mercator(-15), z_ = 12;
	QPointF pega_, apertou_;
	QTimer quieto_;
};

// --- VOO: os aviões ----------------------------------------------------------------

struct Aeronave {
	QString hex, voo, matricula, tipo, pais;
	double lat = NAO, lon = NAO, altPes = NAO, velNos = NAO, rumo = NAO;
	bool solo = false;
};

static const struct {
	const char *nome;
	double lon, lat, largura;
} LUGARES[] = {
	{"São Paulo", -46.6, -23.5, 5},	      {"Rio de Janeiro", -43.2, -22.9, 4},  {"Brasília", -47.9, -15.8, 5},
	{"Brasil", -52, -14, 50},	      {"Nordeste", -38.5, -8.5, 16},	    {"Sul do Brasil", -51, -27.5, 12},
	{"Atlântico Norte", -35, 48, 70},     {"Nova York", -73.9, 40.7, 5},	    {"Estados Unidos", -97, 39, 60},
	{"Londres", -0.3, 51.5, 5},	      {"Europa", 10, 50, 50},		    {"Lisboa", -9.1, 38.8, 5},
	{"Dubai", 55.3, 25.2, 6},	      {"Tóquio", 139.7, 35.6, 6},	    {"Buenos Aires", -58.5, -34.7, 5},
	{"O mundo", 0, 20, 330},
};

class TelaVoos : public Tela {
public:
	TelaVoos() : Tela("VOO", "Aviões", "Os aviões no ar agora, pelo sinal que cada um transmite (ADS-B) e redes abertas de receptores.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		lugar_ = new QComboBox;
		for (const auto &l : LUGARES)
			lugar_->addItem(QString::fromUtf8(l.nome));
		busca_ = campo("", "voo ou matrícula: TAM3456, PR-XMA");
		busca_->setMinimumWidth(260);
		auto *bAchar = botao("Achar");
		h->addWidget(rotulo("VER"));
		h->addWidget(lugar_);
		h->addWidget(busca_);
		h->addWidget(bAchar);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		mapa_ = new Mapa;
		v->addWidget(mapa_);
		t_ = tabela({"voo", "matrícula", "modelo", "altitude (pés)", "velocidade (nós)", "rumo"}, 300);
		t_->verticalHeader()->setVisible(false);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_);
		v->addWidget(rotulo("Fontes: adsb.lol (dados abertos, ODbL) para até 460 km do centro do mapa; de mais longe, a OpenSky "
		    "Network, que limita as consultas de quem não tem conta. Aviões militares e alguns particulares não aparecem. "
		    "Arraste o mapa e use a roda do mouse.", "detalhe"));
		v->addStretch(1);
		mapa_->moveu = [this] { atualizar(); };
		mapa_->escolheu = [this](const QString &id) {
			for (int l = 0; l < t_->rowCount(); l++)
				if (t_->item(l, 0)->data(Qt::UserRole).toString() == id)
					t_->selectRow(l);
		};
		connect(t_, &QTableWidget::itemSelectionChanged, this, [this] {
			if (t_->currentRow() >= 0 && t_->item(t_->currentRow(), 0))
				mapa_->destacar(t_->item(t_->currentRow(), 0)->data(Qt::UserRole).toString());
		});
		connect(lugar_, &QComboBox::currentIndexChanged, this, [this](int k) {
			mapa_->irPara(LUGARES[k].lon, LUGARES[k].lat, LUGARES[k].largura);
		});
		connect(bAchar, &QPushButton::clicked, this, [this] { achar(); });
		connect(busca_, &QLineEdit::returnPressed, this, [this] { achar(); });
		auto *relogio = new QTimer(this);
		connect(relogio, &QTimer::timeout, this, [this] {
			if (isVisible() && ++tiques_ % (longe_ ? 8 : 1) == 0)
				atualizar();
		});
		relogio->start(15000);
	}
	void abrir(const QStringList &a) override
	{
		if (!comecou_) {
			comecou_ = true;
			QTimer::singleShot(0, this, [this] { mapa_->irPara(LUGARES[0].lon, LUGARES[0].lat, LUGARES[0].largura); });
		}
		if (!a.isEmpty()) {
			busca_->setText(a.first());
			achar();
		}
	}

private:
	static QVector<Aeronave> lerAdsb(const QByteArray &d)
	{
		QVector<Aeronave> r;
		for (const QJsonValue &v : QJsonDocument::fromJson(d).object().value("ac").toArray()) {
			const QJsonObject o = v.toObject();
			if (!o.contains("lat"))
				continue;
			Aeronave a;
			a.hex = o.value("hex").toString();
			a.voo = o.value("flight").toString().trimmed();
			a.matricula = o.value("r").toString();
			a.tipo = o.value("t").toString();
			a.lat = o.value("lat").toDouble();
			a.lon = o.value("lon").toDouble();
			a.solo = o.value("alt_baro").isString();
			a.altPes = a.solo ? 0 : o.value("alt_baro").toDouble(NAO);
			a.velNos = o.value("gs").toDouble(NAO);
			a.rumo = o.value("track").toDouble(NAO);
			r << a;
		}
		return r;
	}
	static QVector<Aeronave> lerOpenSky(const QByteArray &d)
	{
		QVector<Aeronave> r;
		for (const QJsonValue &v : QJsonDocument::fromJson(d).object().value("states").toArray()) {
			const QJsonArray s = v.toArray();
			if (s.size() < 11 || s.at(5).isNull() || s.at(6).isNull())
				continue;
			Aeronave a;
			a.hex = s.at(0).toString();
			a.voo = s.at(1).toString().trimmed();
			a.pais = s.at(2).toString();
			a.lon = s.at(5).toDouble();
			a.lat = s.at(6).toDouble();
			a.solo = s.at(8).toBool();
			a.altPes = s.at(7).isNull() ? NAO : s.at(7).toDouble() / 0.3048;
			a.velNos = s.at(9).isNull() ? NAO : s.at(9).toDouble() * 1.943844;
			a.rumo = s.at(10).isNull() ? NAO : s.at(10).toDouble();
			r << a;
		}
		return r;
	}

	void atualizar()
	{
		const double km = mapa_->raioKm();
		const int vez = ++vez_;
		longe_ = km > 600;
		if (!longe_) {
			const int milhas = std::clamp(int(std::ceil(km / 1.852 / 25) * 25), 25, 250);
			const QString url = QStringLiteral("https://api.adsb.lol/v2/point/%1/%2/%3")
			    .arg(mapa_->lat(), 0, 'f', 1).arg(mapa_->lon(), 0, 'f', 1).arg(milhas);
			buscar(url, 10, this, [this, vez](const QByteArray &d, const QString &aviso) {
				if (vez == vez_)
					mostrar(lerAdsb(d), aviso, "adsb.lol");
			});
		} else {
			double o, s, l, n;
			mapa_->limites(&o, &s, &l, &n);
			const QString url = QStringLiteral("https://opensky-network.org/api/states/all?lamin=%1&lomin=%2&lamax=%3&lomax=%4")
			    .arg(std::floor(s)).arg(std::floor(std::max(o, -180.0))).arg(std::ceil(n)).arg(std::ceil(std::min(l, 180.0)));
			buscar(url, 100, this, [this, vez](const QByteArray &d, const QString &aviso) {
				if (vez == vez_)
					mostrar(lerOpenSky(d), aviso, "OpenSky Network");
			});
		}
	}

	void mostrar(QVector<Aeronave> a, const QString &aviso, const char *fonte)
	{
		std::sort(a.begin(), a.end(), [](const Aeronave &x, const Aeronave &y) { return x.voo < y.voo; });
		const QString antes = t_->currentRow() >= 0 && t_->item(t_->currentRow(), 0)
		    ? t_->item(t_->currentRow(), 0)->data(Qt::UserRole).toString() : procurado_;
		QVector<Alvo> alvos;
		const QSignalBlocker b(t_);
		t_->setRowCount(0);
		int noAr = 0;
		for (const Aeronave &x : a) {
			alvos.push_back({x.hex, x.voo.isEmpty() ? x.matricula : x.voo, x.lon, x.lat, x.rumo});
			noAr += !x.solo;
			if (t_->rowCount() >= 400)
				continue;
			const int l = t_->rowCount();
			t_->insertRow(l);
			auto *i = celula(x.voo.isEmpty() ? QStringLiteral("—") : x.voo);
			i->setData(Qt::UserRole, x.hex);
			t_->setItem(l, 0, i);
			t_->setItem(l, 1, celula(x.matricula.isEmpty() ? x.pais : x.matricula));
			t_->setItem(l, 2, celula(x.tipo));
			t_->setItem(l, 3, celula(x.solo ? QStringLiteral("no solo") : num(x.altPes, 0)));
			t_->setItem(l, 4, celula(num(x.velNos, 0)));
			t_->setItem(l, 5, celula(std::isfinite(x.rumo) ? num(x.rumo, 0) + "°" : QStringLiteral("—")));
			if (x.hex == antes)
				t_->selectRow(l);
		}
		mapa_->definir(alvos);
		mapa_->destacar(antes);
		aviso_->setText(a.isEmpty() && !aviso.isEmpty() ? aviso
		    : QStringLiteral("%1 aeronaves nesta parte do mapa, %2 no ar. Fonte: %3, às %4. %5")
			.arg(a.size()).arg(noAr).arg(QLatin1String(fonte), QTime::currentTime().toString("HH:mm:ss"), aviso));
	}

	void achar()
	{
		const QString q = busca_->text().toUpper().remove(' ');
		if (q.isEmpty())
			return;
		aviso_->setText("Procurando…");
		const bool matricula = q.contains('-');
		buscar(QStringLiteral("https://api.adsb.lol/v2/%1/%2").arg(matricula ? "reg" : "callsign", q), 10, this,
		    [this, q, matricula](const QByteArray &d, const QString &aviso) {
			const QVector<Aeronave> a = lerAdsb(d);
			if (a.isEmpty()) {
				aviso_->setText(aviso.isEmpty() ? QStringLiteral("Não achei %1 no ar agora. O voo vai pelo código da companhia de "
				    "três letras (TAM3456, GLO1587, AZU4012); a matrícula, com o traço (PR-XMA).").arg(q) : aviso);
				return;
			}
			procurado_ = a.first().hex;
			t_->clearSelection();
			t_->setCurrentCell(-1, -1);
			mapa_->irPara(a.first().lon, a.first().lat, 4);
			Q_UNUSED(matricula);
		});
	}

	QComboBox *lugar_;
	QLineEdit *busca_;
	QLabel *aviso_;
	Mapa *mapa_;
	QTableWidget *t_;
	QString procurado_;
	int vez_ = 0, tiques_ = 0;
	bool longe_ = false, comecou_ = false;
};

// --- NAVIOS ------------------------------------------------------------------------

static QString
tipoDeNavio(int t)
{
	if (t == 30)
		return "pesca";
	if (t == 31 || t == 32 || t == 52)
		return "rebocador";
	if (t == 36 || t == 37)
		return "vela ou recreio";
	if (t == 50)
		return "prático";
	if (t == 51)
		return "busca e salvamento";
	if (t >= 40 && t < 50)
		return "alta velocidade";
	if (t >= 60 && t < 70)
		return "passageiros";
	if (t >= 70 && t < 80)
		return "carga";
	if (t >= 80 && t < 90)
		return "petroleiro";
	return t ? "outro" : "";
}

class TelaNavios : public Tela {
public:
	TelaNavios() : Tela("NAVIOS", "Navios", "Os navios pelo sinal que transmitem (AIS): o mar Báltico com dados abertos, e o mundo com a sua chave.")
	{
		auto *h = new QHBoxLayout;
		h->setSpacing(10);
		auto *b = botao("Atualizar");
		filtro_ = new QComboBox;
		filtro_->addItems({"todos os tipos", "carga", "petroleiro", "passageiros", "pesca", "rebocador"});
		onde_ = new QComboBox;
		onde_->addItems({"mar Báltico (dados abertos)", "onde o mapa está (o mundo, com a sua chave)"});
		h->addWidget(b);
		h->addWidget(onde_);
		h->addWidget(filtro_);
		h->addStretch(1);
		v->addLayout(h);
		aviso_ = rotulo(QString(), "aviso");
		v->addWidget(aviso_);
		mapa_ = new Mapa;
		v->addWidget(mapa_);
		t_ = tabela({"navio", "tipo", "destino", "velocidade (nós)", "rumo", "calado (m)"}, 300);
		t_->verticalHeader()->setVisible(false);
		t_->setSelectionBehavior(QAbstractItemView::SelectRows);
		v->addWidget(t_);
		v->addWidget(rotulo("Báltico: Fintraffic / digitraffic.fi (Finlândia), dados abertos CC BY 4.0. O mundo: aisstream.io, com a "
		    "chave gratuita que você cria lá e põe na tela CONFIG; a cobertura é a dos receptores voluntários em terra, "
		    "boa nas costas e nos portos e fraca em alto-mar.", "detalhe"));
		v->addStretch(1);
		connect(b, &QPushButton::clicked, this, [this] { atualizar(); });
		connect(filtro_, &QComboBox::currentIndexChanged, this, [this] { mostrar(); });
		connect(onde_, &QComboBox::currentIndexChanged, this, [this](int k) {
			vistos_.clear();
			if (k == 0) {
				fio_->fechar();
				mapa_->irPara(22.5, 60.5, 16);
				atualizar();
			} else
				mundo();
		});
		fio_ = new FioWs(this);
		fio_->abriu = [this] { assinar(); };
		fio_->mensagem = [this](const QByteArray &m) { receber(m); };
		fio_->fechou = [this](const QString &motivo) {
			if (onde_->currentIndex() == 1) {
				erro_ = QStringLiteral("A conexão com o serviço de AIS fechou: %1 Confira a chave na tela CONFIG.").arg(motivo);
				mostrar();
			}
		};
		mapa_->moveu = [this] {
			if (onde_->currentIndex() == 1 && fio_->aberto())
				assinar();
		};
		auto *relogio = new QTimer(this);
		connect(relogio, &QTimer::timeout, this, [this] {
			if (onde_->currentIndex() == 1 && isVisible() && mudou_) {
				mudou_ = false;
				mostrar();
			}
		});
		relogio->start(3000);
		mapa_->escolheu = [this](const QString &id) {
			for (int l = 0; l < t_->rowCount(); l++)
				if (t_->item(l, 0)->data(Qt::UserRole).toString() == id)
					t_->selectRow(l);
		};
		connect(t_, &QTableWidget::itemSelectionChanged, this, [this] {
			if (t_->currentRow() >= 0 && t_->item(t_->currentRow(), 0))
				mapa_->destacar(t_->item(t_->currentRow(), 0)->data(Qt::UserRole).toString());
		});
	}
	void abrir(const QStringList &) override
	{
		if (!comecou_) {
			comecou_ = true;
			QTimer::singleShot(0, this, [this] { mapa_->irPara(22.5, 60.5, 16); });
		}
		atualizar();
	}

private:
	struct Navio {
		QString nome, destino;
		int tipo = 0;
		double lon = 0, lat = 0, vel = NAO, rumo = NAO, calado = NAO;
	};

	// O mundo: o serviço aisstream.io manda, por WebSocket, as posições dos
	// navios dentro da caixa pedida. A chave é gratuita e é da pessoa.
	void mundo()
	{
		const QString chave = config("chave_ais");
		if (chave.isEmpty()) {
			erro_ = "Para ver os navios do mundo todo é preciso uma chave gratuita do aisstream.io (crie a conta em "
				"aisstream.io e copie a chave para a tela CONFIG). Sem chave, fica o Báltico.";
			mostrar();
			return;
		}
		erro_.clear();
		nota_ = "Conectando ao serviço de AIS…";
		mostrar();
		fio_->abrir(QUrl(qEnvironmentVariable("LUME_MERCADOS_AIS", "wss://stream.aisstream.io/v0/stream")));
	}
	void assinar()
	{
		double o, s, l, n;
		mapa_->limites(&o, &s, &l, &n);
		QJsonObject m;
		m["APIKey"] = config("chave_ais");
		m["BoundingBoxes"] = QJsonArray{QJsonArray{QJsonArray{s, std::max(o, -180.0)}, QJsonArray{n, std::min(l, 180.0)}}};
		m["FilterMessageTypes"] = QJsonArray{"PositionReport", "ShipStaticData"};
		fio_->enviar(QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact)));
		nota_ = "Ao vivo, do aisstream.io: os navios aparecem conforme transmitem.";
		vistos_.clear();
		mudou_ = true;
	}
	void receber(const QByteArray &dados)
	{
		const QJsonObject o = QJsonDocument::fromJson(dados).object();
		if (o.contains("error")) {
			erro_ = QStringLiteral("O serviço de AIS respondeu: %1").arg(o.value("error").toString());
			mudou_ = true;
			return;
		}
		const QJsonObject meta = o.value("MetaData").toObject(), msg = o.value("Message").toObject();
		const qint64 mmsi = qint64(meta.value("MMSI").toDouble());
		if (!mmsi)
			return;
		Navio &n = navios_[mmsi];
		const QString nome = meta.value("ShipName").toString().trimmed();
		if (!nome.isEmpty())
			n.nome = nome;
		if (msg.contains("PositionReport")) {
			const QJsonObject p = msg.value("PositionReport").toObject();
			n.lat = p.value("Latitude").toDouble(meta.value("latitude").toDouble(meta.value("Latitude").toDouble()));
			n.lon = p.value("Longitude").toDouble(meta.value("longitude").toDouble(meta.value("Longitude").toDouble()));
			n.vel = p.value("Sog").toDouble();
			n.rumo = p.value("Cog").toDouble() >= 360 ? NAO : p.value("Cog").toDouble();
			if (!vistos_.contains(mmsi))
				vistos_ << mmsi;
		} else if (msg.contains("ShipStaticData")) {
			const QJsonObject p = msg.value("ShipStaticData").toObject();
			n.tipo = p.value("Type").toInt();
			n.destino = p.value("Destination").toString().trimmed();
			n.calado = p.value("MaximumStaticDraught").toDouble();
		}
		mudou_ = true;
	}

	void atualizar()
	{
		if (onde_->currentIndex() == 1) {
			mundo();
			return;
		}
		aviso_->setText("Buscando…");
		// De dez em dez minutos, para o que fica guardado não se multiplicar.
		const qint64 desde = (QDateTime::currentSecsSinceEpoch() / 600 - 2) * 600 * 1000;
		buscar("https://meri.digitraffic.fi/api/ais/v1/vessels", 6 * 3600, this, [this, desde](const QByteArray &nomes, const QString &) {
			for (const QJsonValue &v : QJsonDocument::fromJson(nomes).array()) {
				const QJsonObject o = v.toObject();
				Navio &n = navios_[qint64(o.value("mmsi").toDouble())];
				n.nome = o.value("name").toString().trimmed();
				n.destino = o.value("destination").toString().trimmed();
				n.tipo = o.value("shipType").toInt();
				n.calado = o.value("draught").toDouble() / 10;
			}
			buscar(QStringLiteral("https://meri.digitraffic.fi/api/ais/v1/locations?from=%1").arg(desde), 120, this,
			    [this](const QByteArray &d, const QString &aviso) {
				vistos_.clear();
				for (const QJsonValue &v : QJsonDocument::fromJson(d).object().value("features").toArray()) {
					const QJsonObject o = v.toObject(), p = o.value("properties").toObject();
					const QJsonArray c = o.value("geometry").toObject().value("coordinates").toArray();
					const qint64 mmsi = qint64(o.value("mmsi").toDouble());
					Navio &n = navios_[mmsi];
					n.lon = c.at(0).toDouble();
					n.lat = c.at(1).toDouble();
					n.vel = p.value("sog").toDouble();
					n.rumo = p.value("cog").toDouble() >= 360 ? NAO : p.value("cog").toDouble();
					vistos_ << mmsi;
				}
				erro_ = vistos_.isEmpty() ? aviso : QString();
				nota_ = aviso;
				mostrar();
			});
		});
	}

	void mostrar()
	{
		const QString filtro = filtro_->currentIndex() ? filtro_->currentText() : QString();
		QVector<Alvo> alvos;
		const QSignalBlocker b(t_);
		t_->setRowCount(0);
		int andando = 0;
		// Por nome; os sem nome (só o número do rádio), no fim.
		QVector<qint64> ordem = vistos_;
		std::sort(ordem.begin(), ordem.end(), [this](qint64 a, qint64 b) {
			const QString &x = navios_[a].nome, &y = navios_[b].nome;
			return x.isEmpty() != y.isEmpty() ? y.isEmpty() : x < y;
		});
		for (qint64 mmsi : ordem) {
			const Navio &n = navios_[mmsi];
			const QString tipo = tipoDeNavio(n.tipo);
			if (!filtro.isEmpty() && tipo != filtro)
				continue;
			const QString id = QString::number(mmsi);
			alvos.push_back({id, n.nome, n.lon, n.lat, n.vel > 0.5 ? n.rumo : NAO});
			andando += n.vel > 0.5;
			const int l = t_->rowCount();
			t_->insertRow(l);
			auto *i = celula(n.nome.isEmpty() ? id : n.nome);
			i->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			i->setData(Qt::UserRole, id);
			t_->setItem(l, 0, i);
			t_->setItem(l, 1, celula(tipo));
			t_->setItem(l, 2, celula(n.destino));
			t_->setItem(l, 3, celula(num(n.vel, 1)));
			t_->setItem(l, 4, celula(std::isfinite(n.rumo) ? num(n.rumo, 0) + "°" : QStringLiteral("—")));
			t_->setItem(l, 5, celula(n.calado > 0 ? num(n.calado, 1) : QStringLiteral("—")));
		}
		mapa_->definir(alvos);
		aviso_->setText(!erro_.isEmpty() ? erro_
		    : QStringLiteral("%1 navios com sinal nos últimos minutos, %2 em movimento. %3").arg(alvos.size()).arg(andando).arg(nota_));
	}

	QComboBox *filtro_, *onde_;
	FioWs *fio_;
	bool mudou_ = false;
	QLabel *aviso_;
	Mapa *mapa_;
	QTableWidget *t_;
	QMap<qint64, Navio> navios_;
	QVector<qint64> vistos_;
	QString erro_, nota_;
	bool comecou_ = false;
};
