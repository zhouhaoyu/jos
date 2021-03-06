
// 最终项目：换页
#include <inc/lib.h>
#define PAGEFILE_PGCOUNT 2048
#define PAGEFILE_SIZE (PAGEFILE_PGCOUNT + 1) * PGSIZE
#undef dbg_cprintf
#define dbg_cprintf(...) cprintf(__VA_ARGS__)

static char buf[PGSIZE];
extern struct Dev *devtab[4];
extern bool in_urgency;

static void
write_page(int fd, const char *buf, size_t nbytes)
{
	assert(nbytes == PGSIZE);
	write(fd, buf, PGSIZE / 2);
	write(fd, buf + PGSIZE / 2, PGSIZE / 2);
}

static void
dump_page(void *va)
{
	char *p = ROUNDDOWN(va, PGSIZE);
	int i;
	for (i = 0; i < PGSIZE; i++)
		cprintf("%d ", p[i]);
	cputchar('\n');
}

int
swap_page_to_disk(void *va)
{
	int fd, i;

	va = ROUNDDOWN(va, PGSIZE);
	if ((uint32_t)va == (uint32_t)ROUNDDOWN(&fsipcbuf, PGSIZE) ||
		(uint32_t)va == (uint32_t)ROUNDDOWN(&devfile, PGSIZE) ||
		(uint32_t)va == (uint32_t)ROUNDDOWN(&devtab, PGSIZE) ||
		(uint32_t)va == (uint32_t)ROUNDDOWN(&buf, PGSIZE) ||
		(uint32_t)va >= 0xD0000000) // FDTABLE
		return -E_INVAL;

	in_urgency = true;
	fd = open("/pagefile", O_CREAT | O_RDWR);
	dbg_cprintf("\033[1;46m[PageSwap]\033[35;41mSwapping page [va = 0x%08x] to disk...\033[0m\n", va);

	if (fd > 0)
	{
		struct Stat statbuf;
		fstat(fd, &statbuf);
		if (statbuf.st_size != PAGEFILE_SIZE)
		{
			dbg_cprintf("\033[1;46m[PageSwap]\033[35;42mFirstTime\033[0m\n");


			// 重新创建文件
			ftruncate(fd, PAGEFILE_SIZE);
			seek(fd, 0);

			// 空闲位图
			i = PAGEFILE_PGCOUNT / 8;
			memset(buf, -1, i);
			memset(buf + i, 0, PGSIZE - i);

			write_page(fd, buf, PGSIZE);

			//// 写入余下的页面
			//memset(buf, 0, PGSIZE);
			//for (i = 0; i < PAGEFILE_PGCOUNT; i++)
			//	write_page(fd, buf, PGSIZE);

			dbg_cprintf("\033[1;46m[PageSwap]\033[35;42mPagefileCreated\033[0m\n");
		}

		// 交换入磁盘
		seek(fd, 0);

		// 先读出空闲位图
		read(fd, buf, PGSIZE);
		for (i = 0; i < PAGEFILE_PGCOUNT; i += 8)
			if (buf[i / 8])
			{
				char j, c;
				for (j = 0, c = 1; j < 8; j++, c <<= 1)
					if (buf[i / 8] & c)
					{
						pte_t pte = uvpt[PGNUM(va)];
						dbg_cprintf("\033[1;46m[PageSwap]\033[30;44mWritingPage\033[0m\n");

						// 标记为非空闲
						buf[i / 8] &= ~c;
						seek(fd, 0);
						write_page(fd, buf, PGSIZE);

						// 找到了！
						cprintf("Swap to file pa = %x va = %x offset = %d * PGSIZE\n", PTE_ADDR(pte), va, (1 + i + j));
						seek(fd, (1 + i + j) * PGSIZE);
						write_page(fd, va, PGSIZE);


						if (PTE_ADDR(uvpt[PGNUM(va)]) == 0x9f000)
							cprintf("Swap unmap 0x9f000 here and va = %x\n", va);
						sys_page_unmap(0, va);

						// 保存偏移量入PTE
						sys_set_pte_pafield(va, (i + j) * PGSIZE, 
							PTE_INDISK | (pte & PTE_SYSCALL & ~PTE_P));
						close(fd);
						in_urgency = false;
						return 0;
					}
			}

		// 没地方了……
		close(fd);
		in_urgency = false;
		return -E_NO_DISK;
	}

	in_urgency = false;
	return fd;
}

int
swap_back_page(void *va)
{
	int fd, i;
	in_urgency = false;

	fd = open("/pagefile", O_RDWR);
	va = ROUNDDOWN(va, PGSIZE);
	dbg_cprintf("\033[1;46m[PageSwap]\033[35;43mSwapping back page [va = 0x%08x] from disk...\033[0m\n", va);

	if (fd > 0)
	{
		struct Stat statbuf;
		pte_t pte = uvpt[PGNUM(va)];

		fstat(fd, &statbuf);
		if (statbuf.st_size != PAGEFILE_SIZE)
		{
			in_urgency = false;
			return -E_FAULT;
		}

		// 交换入磁盘
		seek(fd, 0);

		// 读出磁盘内页序号
		i = PTE_ADDR(pte) / PGSIZE;

		// 先读出空闲位图，看看是不是非空闲
		read(fd, buf, PGSIZE);
		if (buf[i / 8] & (1 << i % 8))
		{
			close(fd);
			in_urgency = false;
			return -E_FAULT;
		}

		// 读出对应页写回内存！
		dbg_cprintf("\033[1;46m[PageSwap]\033[30;44mReadingPage\033[0m\n");
		if (sys_page_alloc(0, va, (pte | PTE_P) & PTE_SYSCALL & ~PTE_INDISK) < 0)
		{
			close(fd);
			in_urgency = false;
			return -E_NO_MEM;
		}
		in_urgency = false;

		cprintf("Swap from file pa = %x va = %x offset = %d * PGSIZE\n", PTE_ADDR(uvpt[PGNUM(va)]), va, (1 + i));
		seek(fd, (i + 1) * PGSIZE);
		read(fd, va, PGSIZE);

		// 标记为空闲
		dbg_cprintf("\033[1;46m[PageSwap]\033[30;44mTrying to mark free with pte=%x\033[0m\n", pte);
		buf[i / 8] |= 1 << i % 8;
		seek(fd, 0);
		write_page(fd, buf, PGSIZE);

		// 好了
		close(fd);
		in_urgency = false;
		return 0;
	}
	in_urgency = false;
	return fd;
}