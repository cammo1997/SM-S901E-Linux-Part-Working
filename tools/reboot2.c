/* reboot2 <arg>: reboot(LINUX_REBOOT_CMD_RESTART2, arg). No libc. */
static long sys(long n, long a, long b, long c, long d) {
	register long x8 asm("x8") = n, x0 asm("x0") = a, x1 asm("x1") = b, x2 asm("x2") = c, x3 asm("x3") = d;
	asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}
void _start_c(long *sp) {
	char **argv = (char **)(sp + 1);
	const char *arg = (sp[0] > 1) ? argv[1] : "recovery";
	sys(81, 0, 0, 0, 0);                                   /* sync */
	sys(142, 0xfee1dead, 672274793, 0xA1B2C3D4, (long)arg); /* reboot RESTART2 */
	sys(93, 1, 0, 0, 0);                                   /* exit(1) if it returned */
}
asm(".global _start\n_start:\n mov x0, sp\n bl _start_c\n");
