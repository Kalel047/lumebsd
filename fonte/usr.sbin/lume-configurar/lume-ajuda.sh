#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause
#
# lume-ajuda - abre a Ajuda do LumeBSD (páginas em /usr/share/lume/ajuda)
# no navegador. "lume ajuda windows" vai direto ao assunto.
#

D=/usr/share/lume/ajuda

case "${1:-}" in
"")				t=index ;;
programas|loja|kit|kits)	t=programas ;;
trocando|equivalentes|matlab|excel|office)	t=trocando ;;
tinta|eink|e-ink|tela|animacao|animação)	t=tela ;;
windows|exe|wine|linux|mac|macos|dmg|compat|compatibilidade|web)	t=compatibilidade ;;
wifi|internet|rede|som|video|vídeo|impressora)	t=internet ;;
atualizar|atualizacoes|atualizações|tempo|backup)	t=atualizacoes ;;
problemas|erro|socorro)		t=problemas ;;
*)	echo "uso: lume ajuda [programas | trocando | windows | mac | tinta | wifi | atualizar | problemas]" >&2
	exit 1 ;;
esac
f=$D/$t.html
[ -f "$f" ] || { echo "lume ajuda: $f não encontrado" >&2; exit 1; }

if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
	echo "  A Ajuda do LumeBSD abre no desktop (menu > Ajuda do LumeBSD)."
	echo "  Os arquivos ficam em $D; os comandos têm manual: man lume"
	exit 0
fi
for b in xdg-open firefox chrome; do
	command -v $b >/dev/null 2>&1 && exec $b "$f"
done
echo "lume ajuda: nenhum navegador encontrado; abra $f" >&2
exit 1
