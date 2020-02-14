
#include <stdio.h>
#include <strings.h>
#include <sys/errno.h>

void
hexdump(const uchar_t *buf, int len)
{
	int idx;
	char ascii[24];
	char *pa = ascii;

	memset(ascii, '\0', sizeof (ascii));

	idx = 0;
	while (len--) {
		if ((idx & 15) == 0) {
			printf("%04X: ", idx);
			pa = ascii;
		}
		if (*buf > ' ' && *buf <= '~')
			*pa++ = *buf;
		else
			*pa++ = '.';
		printf("%02x ", *buf++);

		idx++;
		if ((idx & 3) == 0) {
			*pa++ = ' ';
			putchar(' ');
		}
		if ((idx & 15) == 0) {
			*pa = '\0';
			printf("%s\n", ascii);
		}
	}

	if ((idx & 15) != 0) {
		*pa = '\0';
		/* column align the last ascii row */
		while ((idx & 15) != 0) {
			if ((idx & 3) == 0)
				putchar(' ');
			printf("   ");
			idx++;
		}
		printf("%s\n", ascii);
	}
}

int
compare(uchar_t *p1, uchar_t *p2, size_t len)
{
	int off, row;
	int diffs = 0;

	for (off = 0; off < len; off++) {
		if (p1[off] != p2[off]) {
			printf("differs at off %d\n", off);
			row = off & ~15;
			hexdump(p1+row, 16);
			hexdump(p2+row, 16);
			off = row + 16;
			diffs++;
		}
	}

	return (diffs);
}
