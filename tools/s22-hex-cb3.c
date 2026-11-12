// SPDX-License-Identifier: MIT
/*
 * Reserve FastRPC SDSP context banks 1 and 2 (0x0541, 0x0542) so hexagonrpcd
 * opens context bank 3 (0x0543 = downstream shared-cb for SLPI SENSORS_PD).
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int fd0, fd1;

	if (argc < 2) {
		fprintf(stderr, "usage: %s /usr/bin/hexagonrpcd [args...]\n", argv[0]);
		return 1;
	}

	fd0 = open("/dev/fastrpc-sdsp", O_RDWR);
	fd1 = open("/dev/fastrpc-sdsp", O_RDWR);
	if (fd0 < 0 || fd1 < 0)
		perror("s22-hex-cb3: open /dev/fastrpc-sdsp");

	execv(argv[1], &argv[1]);
	perror("s22-hex-cb3: execv");
	return 1;
}
