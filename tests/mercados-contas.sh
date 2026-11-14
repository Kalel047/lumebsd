#!/bin/sh
# Confere as contas dos Mercados do LumeBSD (lume-mercados) sem rede: o
# título de renda fixa contra a conta feita à mão, as relações que vêm com
# o sistema, e as cotações, a correlação e o beta de duas séries inventadas
# (B anda sempre o dobro de A: correlação 1, beta 0,5).
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

[ $falhas -eq 0 ] && echo "tudo certo" || echo "$falhas falha(s)"
exit $falhas
