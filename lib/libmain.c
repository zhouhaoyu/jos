// Called from entry.S to get us going.
// entry.S already took care of defining envs, pages, uvpd, and uvpt.

#include <inc/lib.h>

extern void umain(int argc, char **argv);

// Lab 4 挑战 6：实现共享内存的 fork
// 对 thisenv 的 hack
const volatile struct Env **_thisenv_addr;
const char *binaryname = "<unknown>";

void
libmain(int argc, char **argv)
{
	const volatile struct Env *_thisenv;
	_thisenv_addr = &_thisenv;

	// set thisenv to point at our Env structure in envs[].
	// LAB 3: Your code here.
	thisenv = envs + ENVX(sys_getenvid());


	// Lab 4 挑战 7：批量系统调用
	// 记录缓存页号，用于让批量系统调用避开缓存页
	batch_begin_pgnum = ROUNDDOWN((uint32_t)&in_batch_state, PGSIZE) / PGSIZE,
		batch_end_pgnum = ROUNDUP((uint32_t)(batch_argu + 4), PGSIZE) / PGSIZE;

	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// call user main routine
	umain(argc, argv);

	// exit gracefully
	exit();
}

