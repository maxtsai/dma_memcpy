#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

int main(void)
{
	char *src, *dst;
	int fd;
	size_t length;
	ssize_t s;
	off_t offset = 0;
	size_t need_len = 1920*1080*4;
	int i;

	fd = open("/dev/dmacpy", O_RDWR);
	if (fd == -1)
		handle_error("open");

	srand(time(NULL));

	src = mmap(NULL, need_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
	if (src == MAP_FAILED)
		handle_error("mmap");

	i = rand();

	memset(src, i, need_len);

	dst = mmap(NULL, need_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
	if (dst == MAP_FAILED)
		handle_error("mmap");

	i = rand();
	memset(dst, i, need_len);

	printf("src ptr %p, dst ptr %p\n", src, dst);
	for (i = 0; i < need_len; i+= (need_len/10))
		printf("[%d] [0x%x : 0x%x]\n", i, src[i], dst[i]);


	munmap(src, need_len);
	munmap(dst, need_len);

	close(fd);
	exit(EXIT_SUCCESS);
}
