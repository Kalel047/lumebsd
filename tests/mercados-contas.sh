#!/bin/sh
# Confere as contas dos Mercados do LumeBSD (lume-mercados) sem rede: o
# título de renda fixa contra a conta feita à mão, as relações que vêm com
# o sistema, as cotações, a correlação e o beta de duas séries inventadas
# (B anda sempre o dobro de A: correlação 1, beta 0,5), o Black-Scholes, e a
# leitura dos documentos da CVM e da SEC com arquivos de exemplo.
#   sh tests/mercados-contas.sh [CAMINHO/DO/lume-mercados] [PASTA-DO-APP]
M=${1:-/usr/bin/lume-mercados}
[ -n "$2" ] && export LUME_MERCADOS_PASTA=$2
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
falhas=0
confere() {	# confere "descrição" "texto esperado" comando...
	d=$1 e=$2
	shift 2
	if "$@" 2>&1 | grep -qF -- "$e"; then
		echo "ok    $d"
	else
		echo "FALHA $d (esperava: $e)"
		"$@" 2>&1 | sed 's/^/      /'
		falhas=$((falhas + 1))
	fi
}

# Face 1.000, cupom de 10% semestral, 10 anos, a 12% ao ano.
confere "título: preço" "preço               885,30" "$M" --titulo 1000 10 2 10 12
confere "título: duração" "duração (Macaulay)  6,3092" "$M" --titulo 1000 10 2 10 12
confere "título: duração modificada" "duração modificada  5,9521" "$M" --titulo 1000 10 2 10 12
confere "título: a taxa de volta do preço" "taxa pelo preço     12,0000%" "$M" --titulo 1000 10 2 10 12
confere "título ao par" "preço               1.000,00" "$M" --titulo 1000 8 1 5 8

confere "relações: cliente da Vale" "Clientes      5401.T     Nippon Steel" "$M" --relacoes VALE3
confere "relações: concorrente da Vale" "Concorrentes  RIO        Rio Tinto" "$M" --relacoes VALE3
confere "relações: fornecedor da Apple" "Fornecedores  TSM        TSMC" "$M" --relacoes AAPL
confere "relações: a controladora da Ambev" "Controlador   BUD        AB InBev" "$M" --relacoes ABEV3

# Duas séries de 40 dias, no formato da fonte das cotações.
serie() {	# serie SIMBOLO MULTIPLICADOR
	awk -v s="$1" -v m="$2" 'BEGIN {
		p = 100; t = 1700000000
		for (k = 0; k < 40; k++) {
			r = (k % 3 == 0 ? 0.010 : k % 3 == 1 ? -0.004 : 0.007) * m
			if (k) p *= exp(r)
			ts = ts (k ? "," : "") t + k * 86400; cs = cs (k ? "," : "") sprintf("%.8f", p)
		}
		printf "{\"symbol\":\"%s\",\"response\":[{\"meta\":{\"currency\":\"BRL\",\"symbol\":\"%s\",", s, s
		printf "\"regularMarketPrice\":%.8f,\"longName\":\"Empresa %s\"},\"timestamp\":[%s],", p, s, ts
		printf "\"indicators\":{\"quote\":[{\"close\":[%s]}]}}]}", cs
	}'
}
dados() {	# dados PERIODO: grava a resposta para AAA e BBB
	u="https://query1.finance.yahoo.com/v7/finance/spark?symbols=AAA,BBB&range=$1&interval=1d"
	printf '{"spark":{"result":[%s,%s]}}' "$(serie AAA 1)" "$(serie BBB 2)" > "$T/$("$M" --chave "$u")"
}
dados 1y
dados 5d
export LUME_MERCADOS_DADOS="$T"
confere "correlação de duas séries proporcionais" "correlação  1,0000" "$M" --correlacao AAA BBB
confere "beta de A contra B (o dobro)" "beta        0,5000" "$M" --correlacao AAA BBB
confere "cotação lida da fonte" "Empresa AAA" "$M" --cotacao AAA BBB
confere "ativo que não existe" "não achei" "$M" --cotacao AAA BBB ZZZ

# Black-Scholes: S = K = 100, um ano, 20% de volatilidade, juros contínuos de
# 5% (5,1271% ao ano efetivos): 10,4506 é o valor dos livros.
confere "Black-Scholes: compra" "preço  10,4506" "$M" --opcao compra 100 100 365 20 5,127110
confere "Black-Scholes: a volatilidade de volta" "volatilidade implícita de volta  20,00%" "$M" --opcao compra 100 100 365 20 5,127110
confere "Black-Scholes: venda (paridade)" "preço  5,5735" "$M" --opcao venda 100 100 365 20 5,127110

# A SEC: a lista de códigos, os fatos XBRL, as entregas e o relatório anual.
grava() {	# grava URL < conteúdo
	cat > "$T/$("$M" --chave "$1")"
}
grava https://www.sec.gov/files/company_tickers.json <<'J'
{"0":{"cik_str":1045810,"ticker":"NVDA","title":"NVIDIA CORP"}}
J
grava https://data.sec.gov/api/xbrl/companyfacts/CIK0001045810.json <<'J'
{"facts":{"us-gaap":{"Revenues":{"units":{"USD":[
{"start":"2024-01-29","end":"2025-01-26","val":130497000000,"form":"10-K","filed":"2025-02-26"},
{"start":"2025-01-27","end":"2026-01-25","val":187142000000,"form":"10-K","filed":"2026-02-25"},
{"start":"2025-07-28","end":"2025-10-26","val":57006000000,"form":"10-Q","filed":"2025-11-19"}]}},
"NetIncomeLoss":{"units":{"USD":[{"start":"2025-01-27","end":"2026-01-25","val":104876000000,"form":"10-K","filed":"2026-02-25"}]}},
"Assets":{"units":{"USD":[{"end":"2026-01-25","val":161148000000,"form":"10-K","filed":"2026-02-25"}]}}}}}
J
grava https://data.sec.gov/submissions/CIK0001045810.json <<'J'
{"filings":{"recent":{"accessionNumber":["0001045810-26-000010","0001045810-26-000003"],"filingDate":["2026-05-28","2026-02-25"],
"form":["10-Q","10-K"],"primaryDocument":["q1.htm","nvda-20260125.htm"]}}}
J
grava https://www.sec.gov/Archives/edgar/data/1045810/000104581026000003/nvda-20260125.htm <<'J'
<html><body><p>Sales to one direct customer, Customer&#160;A, represented 12% of total
revenue for fiscal year 2026, and sales to Customer B represented 11%.</p>
<p>Our revenue grew 65% in fiscal year 2026.</p></body></html>
J
confere "SEC: a receita do ano fiscal" "2026      187.142" "$M" --balanco NVDA
confere "SEC: o 10-Q não entra no ano" "2025      130.497" "$M" --balanco NVDA
confere "SEC: clientes do relatório anual" "[12% 11%] Sales to one direct customer, Customer A, represented 12% of total revenue" "$M" --clientes NVDA
confere "SEC: só as frases sobre clientes" "1 trechos sobre clientes" "$M" --clientes NVDA

# A CVM: o cadastro (códigos) e o Formulário de Referência, em zip como lá.
ano=$(date +%Y)
mkdir -p "$T/z"
printf 'CNPJ_Companhia;Data_Referencia;Versao;ID_Documento;Nome_Companhia;Valor_Mobiliario;Sigla_Classe_Acao_Preferencial;Classe_Acao_Preferencial;Codigo_Negociacao;Composicao_BDR_Unit;Mercado;Sigla_Entidade_Administradora;Entidade_Administradora;Data_Inicio_Negociacao;Data_Fim_Negociacao;Segmento;Data_Inicio_Listagem;Data_Fim_Listagem\n11.111.111/0001-11;%s-01-01;1;1;EMPRESA MAE S.A.;Ações Ordinárias;;;MAEE3;;Bolsa;B3;B3;2020-01-01;;Novo Mercado;2020-01-01;\n22.222.222/0001-22;%s-01-01;1;2;FILHA QUIMICA S.A.;Ações Ordinárias;;;FILH3;;Bolsa;B3;B3;2020-01-01;;Novo Mercado;2020-01-01;\n' "$ano" "$ano" > "$T/z/fca_cia_aberta_valor_mobiliario_$ano.csv"
printf 'CNPJ_Companhia;Data_Referencia;Versao;ID_Documento;Nome_Empresarial;Setor_Atividade;Descricao_Atividade;Especie_Controle_Acionario;Pagina_Web;Situacao_Emissor;Data_Registro_CVM\n11.111.111/0001-11;%s-01-01;1;1;EMPRESA MAE S.A.;Petróleo;Óleo;Privado;;Fase Operacional;2000-01-01\n22.222.222/0001-22;%s-01-01;1;2;FILHA QUIMICA S.A.;Química;Resinas;Privado;;Fase Operacional;2000-01-01\n' "$ano" "$ano" > "$T/z/fca_cia_aberta_geral_$ano.csv"
# A CVM grava em Latin-1.
latin1() {
	for f in "$@"; do
		iconv -f UTF-8 -t ISO-8859-1 "$f" > "$f.l1" && mv "$f.l1" "$f"
	done
}
latin1 "$T"/z/fca_*.csv
(cd "$T/z" && bsdtar -a -cf ../fca.zip fca_*.csv)
printf 'CNPJ_Companhia;Data_Referencia;Versao;ID_Documento;Nome_Companhia;Parte_Relacionada;Tipo_Pessoa;Documento_Parte_Relacionada;Relacao_Emissor;Data_Transacao;Objeto_Contrato;Montante_Envolvido;Saldo_Existente;Montante_Interesse_Parte_Relacionada;Garantia_Seguro;Duracao_Transacao;Emprestimo_Divida;Rescisao;Natureza_Razao_Operacao;Taxa_Juros;Posicao_Contratual_Emissor;Especificacao_Posicao_Contratual_Emissor\n11.111.111/0001-11;%s-12-31;2;9;EMPRESA MAE S.A.;Filha Química S.A.;;;Coligada;2025-01-01;"Venda de nafta;\nmatéria-prima";30000000000.00;12.000.000.000,00;;;;;;;;Credor;\n11.111.111/0001-11;%s-12-31;2;9;EMPRESA MAE S.A.;Filha Química S.A.;;;Coligada;2025-06-01;Gás;12500000000.00;500.000.000,00;;;;;;;;Credor;\n11.111.111/0001-11;%s-12-31;1;8;EMPRESA MAE S.A.;Versão Velha Ltda;;;Antiga;2020-01-01;x;99.00;0,00;;;;;;;;Devedor;\n' "$ano" "$ano" "$ano" > "$T/z/fre_cia_aberta_transacao_parte_relacionada_$ano.csv"
printf 'CNPJ_Companhia;Data_Referencia;Versao;ID_Documento;Nome_Companhia;ID_Acionista;Acionista;Tipo_Pessoa_Acionista;CPF_CNPJ_Acionista;ID_Acionista_Relacionado;Acionista_Relacionado;Tipo_Pessoa_Acionista_Relacionado;CPF_CNPJ_Acionista_Relacionado;Quantidade_Acao_Ordinaria_Circulacao;Percentual_Acao_Ordinaria_Circulacao;Quantidade_Acao_Preferencial_Circulacao;Percentual_Acao_Preferencial_Circulacao;Quantidade_Total_Acoes_Circulacao;Percentual_Total_Acoes_Circulacao;Nacionalidade;Sigla_UF;Residente_Exterior;Representante_Legal;Tipo_Pessoa_Representante_Legal;CPF_CNPJ_Representante_legal;Data_Composicao_Capital_Social;Data_Ultima_Alteracao;Acionista_Controlador;Participante_Acordo_Acionistas\n11.111.111/0001-11;%s-12-31;2;9;EMPRESA MAE S.A.;1;União Federal;PJ;00.394.460/0409-50;;;;;1;50.5;0;0;1;50.500000;Brasil;;N;;;;;;S;N\n' "$ano" > "$T/z/fre_cia_aberta_posicao_acionaria_$ano.csv"
printf 'CNPJ_Companhia;Data_Referencia;Versao;ID_Documento;Nome_Companhia;ID_Sociedade;Razao_Social;CNPJ;Tipo_Sociedade;Descricao_Atividades;Pais_Sede;UF_Sede;Municipio_Sede;Participacao_Emissor;Possui_Registro_CVM;Codigo_CVM;Razao_Aquisicao_Manutencao;Data_Valor_Mercado;Data_Valor_Contabil;Valor_Mercado;Valor_Contabil\n11.111.111/0001-11;%s-12-31;2;9;EMPRESA MAE S.A.;1;Filha Química S.A.;22222222000122;;Resinas;;;;36.10000000;;;;;;;\n' "$ano" > "$T/z/fre_cia_aberta_participacao_sociedade_$ano.csv"
latin1 "$T"/z/fre_*.csv
(cd "$T/z" && bsdtar -a -cf ../fre.zip fre_*.csv)
cp "$T/fca.zip" "$T/$("$M" --chave "https://dados.cvm.gov.br/dados/CIA_ABERTA/DOC/FCA/DADOS/fca_cia_aberta_$ano.zip")"
cp "$T/fre.zip" "$T/$("$M" --chave "https://dados.cvm.gov.br/dados/CIA_ABERTA/DOC/FRE/DADOS/fre_cia_aberta_$ano.zip")"
confere "CVM: soma os contratos com a parte relacionada" "FILH3     2 contratos     R\$ 42,50 bi  saldo R\$ 12,50 bi" "$M" --relacoes-cvm MAEE3
confere "CVM: só a última versão do formulário" "Contratos com partes relacionadas:" "$M" --relacoes-cvm MAEE3
confere "CVM: o acionista, em Latin-1" "União Federal" "$M" --relacoes-cvm MAEE3
confere "CVM: o controlador" "50,50%  controlador" "$M" --relacoes-cvm MAEE3
confere "CVM: a participação, com o código achado pelo CNPJ" "Filha Química S.A.                           FILH3     36,10%" "$M" --relacoes-cvm MAEE3
if "$M" --relacoes-cvm MAEE3 2>&1 | grep -q "Versão Velha"; then
	echo "FALHA CVM: entrou uma versão antiga do formulário"
	falhas=$((falhas + 1))
else
	echo "ok    CVM: a versão antiga fica de fora"
fi

[ $falhas -eq 0 ] && echo "tudo certo" || echo "$falhas falha(s)"
exit $falhas
