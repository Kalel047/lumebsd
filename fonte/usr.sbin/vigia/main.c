/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Um só binário: chamado como "vigiad" é o daemon, como "vigia" é o comando.
 */

#include <stdlib.h>
#include <string.h>

#include "vigia.h"

int
main(int argc, char *argv[])
{
	if (strcmp(getprogname(), "vigiad") == 0)
		return (servidor_main(argc, argv));
	return (cliente_main(argc, argv));
}
