/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * wpa-falso: um wpa_supplicant de mentira, para testar o lume-rede numa
 * máquina virtual sem placa Wi-Fi. Responde pelo mesmo socket de controle
 * (datagramas, o protocolo do wpa_cli) com quatro redes por perto e conecta
 * só se a chave estiver certa. Cada comando vai para o log (sem a chave).
 *
 *   wpa-falso SOCKET LOG
 *
 * As redes: "Casa do Kalel" (WPA2, senha farol-2026), "Café da Esquina"
 * (WPA2, com acento no nome), "Biblioteca" (aberta) e "Escritório" (802.1X).
 */
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <openssl/evp.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct rede {
	int usada;
	char ssid[64];   /* bytes */
	char psk[65];    /* hexadecimal */
	char key_mgmt[32];
};
static struct rede redes[16];
static int atual = -1;      /* conectada */
static FILE *log_;

static const char *CASA = "Casa do Kalel";

static void
hex_para_bytes(const char *h, char *out, size_t max)
{
	size_t n = 0;
	while (h[0] && h[1] && n + 1 < max) {
		unsigned v;
		sscanf(h, "%2x", &v);
		out[n++] = (char)v;
		h += 2;
	}
	out[n] = '\0';
}

static void
psk_certa(const char *ssid, const char *senha, char *hex)
{
	unsigned char k[32];
	PKCS5_PBKDF2_HMAC_SHA1(senha, strlen(senha), (const unsigned char *)ssid,
	    strlen(ssid), 4096, 32, k);
	for (int i = 0; i < 32; i++)
		sprintf(hex + 2 * i, "%02x", k[i]);
}

static void
responder(const char *cmd, char *r, size_t max)
{
	int id;
	char campo[64], valor[256];

	if (strcmp(cmd, "PING") == 0)
		snprintf(r, max, "PONG\n");
	else if (strcmp(cmd, "SCAN") == 0)
		snprintf(r, max, "OK\n");
	else if (strcmp(cmd, "SCAN_RESULTS") == 0)
		snprintf(r, max,
		    "bssid / frequency / signal level / flags / ssid\n"
		    "02:00:00:00:00:01\t2437\t-48\t[WPA2-PSK-CCMP][ESS]\tCasa do Kalel\n"
		    "02:00:00:00:00:02\t2412\t-71\t[WPA2-PSK-CCMP][ESS]\tCaf\\xc3\\xa9 da Esquina\n"
		    "02:00:00:00:00:03\t5180\t-80\t[ESS]\tBiblioteca\n"
		    "02:00:00:00:00:04\t5200\t-62\t[WPA2-EAP-CCMP][ESS]\tEscrit\\xc3\\xb3rio\n"
		    "02:00:00:00:00:05\t2437\t-77\t[WPA2-PSK-CCMP][ESS]\tCasa do Kalel\n");
	else if (strcmp(cmd, "STATUS") == 0) {
		if (atual >= 0)
			snprintf(r, max, "wpa_state=COMPLETED\nid=%d\nssid=%s\nip_address=192.168.0.42\n",
			    atual, redes[atual].ssid);
		else
			snprintf(r, max, "wpa_state=SCANNING\n");
	} else if (strcmp(cmd, "SIGNAL_POLL") == 0)
		snprintf(r, max, "RSSI=-48\nLINKSPEED=144\n");
	else if (strcmp(cmd, "LIST_NETWORKS") == 0) {
		size_t n = snprintf(r, max, "network id / ssid / bssid / flags\n");
		for (int i = 0; i < 16; i++)
			if (redes[i].usada)
				n += snprintf(r + n, max - n, "%d\t%s\tany\t%s\n", i, redes[i].ssid,
				    i == atual ? "[CURRENT]" : "");
	} else if (strcmp(cmd, "ADD_NETWORK") == 0) {
		for (id = 0; id < 16 && redes[id].usada; id++)
			;
		redes[id].usada = 1;
		snprintf(r, max, "%d\n", id);
	} else if (sscanf(cmd, "SET_NETWORK %d %63s %255s", &id, campo, valor) == 3 &&
	    id >= 0 && id < 16 && redes[id].usada) {
		if (strcmp(campo, "ssid") == 0)
			hex_para_bytes(valor, redes[id].ssid, sizeof(redes[id].ssid));
		else if (strcmp(campo, "psk") == 0)
			strlcpy(redes[id].psk, valor, sizeof(redes[id].psk));
		else if (strcmp(campo, "key_mgmt") == 0)
			strlcpy(redes[id].key_mgmt, valor, sizeof(redes[id].key_mgmt));
		snprintf(r, max, "OK\n");
	} else if (sscanf(cmd, "SELECT_NETWORK %d", &id) == 1 && id >= 0 && id < 16 && redes[id].usada) {
		char certa[65] = "";
		if (strcmp(redes[id].ssid, CASA) == 0)
			psk_certa(CASA, "farol-2026", certa);
		/* Conecta: a Casa com a chave certa, ou uma rede aberta. */
		if ((certa[0] && strcmp(redes[id].psk, certa) == 0) ||
		    strcmp(redes[id].key_mgmt, "NONE") == 0)
			atual = id;
		else
			atual = -1;
		fprintf(log_, "conectado=%s\n", atual >= 0 ? redes[atual].ssid : "-");
		snprintf(r, max, "OK\n");
	} else if (sscanf(cmd, "REMOVE_NETWORK %d", &id) == 1 && id >= 0 && id < 16) {
		memset(&redes[id], 0, sizeof(redes[id]));
		if (atual == id)
			atual = -1;
		snprintf(r, max, "OK\n");
	} else if (strcmp(cmd, "SAVE_CONFIG") == 0 || strcmp(cmd, "RECONNECT") == 0)
		snprintf(r, max, "OK\n");
	else if (strcmp(cmd, "DISCONNECT") == 0) {
		atual = -1;
		snprintf(r, max, "OK\n");
	} else
		snprintf(r, max, "FAIL\n");
}

int
main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "uso: wpa-falso SOCKET LOG\n");
		return 2;
	}
	log_ = fopen(argv[2], "a");
	setvbuf(log_, NULL, _IOLBF, 0);
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	struct sockaddr_un a = {0};
	a.sun_family = AF_UNIX;
	strlcpy(a.sun_path, argv[1], sizeof(a.sun_path));
	unlink(argv[1]);
	if (bind(fd, (struct sockaddr *)&a, SUN_LEN(&a)) != 0) {
		perror("bind");
		return 1;
	}
	/* Como o wpa_supplicant com ctrl_interface_group=wheel. */
	struct group *g = getgrnam("wheel");
	if (g)
		chown(argv[1], 0, g->gr_gid);
	chmod(argv[1], 0770);
	for (;;) {
		char cmd[1024], r[4096];
		struct sockaddr_un de;
		socklen_t dl = sizeof(de);
		ssize_t n = recvfrom(fd, cmd, sizeof(cmd) - 1, 0, (struct sockaddr *)&de, &dl);
		if (n <= 0)
			continue;
		cmd[n] = '\0';
		/* A chave não vai para o log, só se veio. */
		if (strstr(cmd, " psk "))
			fprintf(log_, "%.*s psk (%zu caracteres)\n", (int)(strstr(cmd, " psk ") - cmd), cmd,
			    strlen(strstr(cmd, " psk ") + 5));
		else
			fprintf(log_, "%s\n", cmd);
		responder(cmd, r, sizeof(r));
		sendto(fd, r, strlen(r), 0, (struct sockaddr *)&de, dl);
	}
}
