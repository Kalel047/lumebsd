--
-- SPDX-License-Identifier: BSD-2-Clause
--
-- LumeBSD: the lighthouse ("farol") emblem, in color.
-- Amber light, white tower, blue sea.
--

return {
	brand = {
		ascii = {
			image = {
			    "\027[33;1m  _                          \027[37m____   _____ _____  ",
			    "\027[33;1m | |                        \027[37m|  _ \\ / ____|  __ \\ ",
			    "\027[33;1m | |    _   _ _ __ ___   ___\027[37m| |_) | (___ | |  | |",
			    "\027[33;1m | |   | | | | '_ ` _ \\ / _ \\\027[37m  _ < \\___ \\| |  | |",
			    "\027[33;1m | |___| |_| | | | | | |  __/\027[37m |_) |____) | |__| |",
			    "\027[33;1m |______\\__,_|_| |_| |_|\\___|\027[37m____/|_____/|_____/ \027[m",
			},
			requires_color = true,
		},
	},
	logo = {
		ascii = {
			image = {
			    "\027[33;1m     \\    |    /\027[m",
			    "\027[33;1m   ---  (( ))  ---\027[m",
			    "\027[33;1m     /   \027[37m_|_\027[33;1m   \\\027[m",
			    "\027[37;1m        /___\\\027[m",
			    "\027[37;1m        |[\027[33m_\027[37m]|\027[m",
			    "\027[37;1m        |\027[31m===\027[37m|\027[m",
			    "\027[37;1m        |   |\027[m",
			    "\027[37;1m        |\027[31m===\027[37m|\027[m",
			    "\027[37;1m       /|   |\\\027[m",
			    "\027[37;1m        |\027[31m===\027[37m|\027[m",
			    "\027[37;1m       _|___|_\027[m",
			    "\027[34;1m  ~~~~\027[37m/_______\\\027[34;1m~~~~\027[m",
			    "\027[34m   ~~   ~~~~   ~~\027[m",
			},
			requires_color = true,
			shift = {x = 3, y = 0},
		},
		-- Com a tela em modo gráfico, o farol desenhado, com a luz acesa.
		fb = {
			image = "/boot/images/lume-logo-rev.png",
			-- the same 13 rows as the ASCII lighthouse
			width = 13,
		},
	},
}
