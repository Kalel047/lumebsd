#!/bin/sh
# Confere as contas das Finanças do LumeBSD (lume-financas) contra valores
# conhecidos: os livros de matemática financeira, a Calculadora do Cidadão
# do Banco Central (o IPCA de 2020 fechou em 4,52%) e uma regressão
# conferida no numpy/scipy. Sem rede: o IPCA vem de uma pasta de teste.
#   sh tests/financas-contas.sh [CAMINHO/DO/lume-financas]
F=${1:-/usr/bin/lume-financas}
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

confere "Price: 100 mil a 1% em 12 meses" "primeira parcela  R\$ 8.884,88" "$F" --price 100000 1 12
confere "Price: juros totais" "total de juros    R\$ 6.618,55" "$F" --price 100000 1 12
confere "SAC: primeira parcela" "primeira parcela  R\$ 11.200,00" "$F" --sac 120000 1 12
confere "SAC: juros totais" "total de juros    R\$ 7.800,00" "$F" --sac 120000 1 12
confere "VPL a 10%" "VPL                R\$ 78,82" "$F" --projeto 10 -1000 500 400 300 100
confere "TIR" "TIR                14,49%" "$F" --projeto 10 -1000 500 400 300 100
confere "TIRM" "TIRM               12,11%" "$F" --projeto 10 -1000 500 400 300 100
confere "payback simples" "payback simples    2,33" "$F" --projeto 10 -1000 500 400 300 100
confere "duas TIRs" "TIR                10,00%, 20,00%" "$F" --projeto 10 -1000 2300 -1320
confere "fluxo que termina negativo não se paga" "payback simples    —" "$F" --projeto 10 -1000 3000 -2500
confere "1% ao mês ao ano" "= 12,6825% ao ano" "$F" --taxa 1 mes ano
confere "números em português" "= 12,6825% ao ano" "$F" --taxa 1,0 mes ano

cat > "$T/433.json" <<'J'
[{"data":"01/01/2020","valor":"0.21"},{"data":"01/02/2020","valor":"0.25"},{"data":"01/03/2020","valor":"0.07"},{"data":"01/04/2020","valor":"-0.31"},{"data":"01/05/2020","valor":"-0.38"},{"data":"01/06/2020","valor":"0.26"},{"data":"01/07/2020","valor":"0.36"},{"data":"01/08/2020","valor":"0.24"},{"data":"01/09/2020","valor":"0.64"},{"data":"01/10/2020","valor":"0.86"},{"data":"01/11/2020","valor":"0.89"},{"data":"01/12/2020","valor":"1.35"}]
J
confere "IPCA de 2020" "variação           4,52%" env LUME_FINANCAS_SGS="$T" "$F" --corrigir ipca 1000 01/2020 12/2020
confere "IPCA ainda não divulgado" "ainda não foi divulgado" env LUME_FINANCAS_SGS="$T" "$F" --corrigir ipca 1000 01/2020 02/2021

# y = 2 + 3x1 − x2 (exato, menos a última linha): os coeficientes e um
# ajuste perfeito; a linha com um buraco fica de fora.
printf 'y;x1;x2\n4;1;1\n6;2;2\n12;3;-1\n13;4;1\n18,5;5;-1,5\n9;;2\n' > "$T/d.csv"
confere "regressão: constante" "const               2,0000" "$F" --regressao "$T/d.csv" y x1 x2
confere "regressão: x1" "x1                  3,0000" "$F" --regressao "$T/d.csv" y x1 x2
confere "regressão: linha incompleta" "1 linha incompleta ficou de fora" "$F" --regressao "$T/d.csv" y x1 x2

[ $falhas -eq 0 ] && echo "tudo certo" || echo "$falhas falha(s)"
exit $falhas
