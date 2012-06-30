#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* using mmap, make a file and padding it with random chars. */

int main(int argc, char *argv[]) {

    char *filename = NULL;
    char *file_addr;
    char *p, *q;
    off_t offset;
    size_t filesize;
    size_t blksize;
    long numblks;
    long cnt = 1;
    int mret = 0;
    long i, j;
    int fid;
    char sbuf[16];

    /* parse arguments */
    if (argc != 5) {
        fprintf(stderr, "\tusage:\n\tmkfile_mmap -n <size>[b|k|m|g] -f <filename>\n");
        return -7;
    }
    for (i = 1; i < argc;) {
        switch (argv[i][1]) {
            case 'n':/* file size */
                i++;
                j = strlen(argv[i]) - 1;
                switch (argv[i][j]) {
                    case 'b':
                        break;
                    case 'k':
                        cnt *= 1024;
                        break;
                    case 'm':
                        cnt *= (1024 * 1024);
                        break;
                    case 'g':
                        cnt *= (1024 * 1024 * 1024);
                        break;
                    default:
                        j++;
                }
                strncpy(sbuf, argv[i], j < 16 ? j : 15);
                sbuf[j] = 0;
                cnt *= atoi(sbuf);
                i++;
                break;
            case 'f':/* target file */
                i++;
                filename = argv[i];
                i++;
        }
    }

    /* open test file */
    fid = open(filename, O_RDWR | O_CREAT | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
    if (fid == -1) {
        fprintf(stderr, "open %s error=%d\n", filename, errno);
        mret = errno;
        goto exit3;
    }

    /* extend file */
    filesize = cnt;
    if (ftruncate(fid, filesize) == -1) {
        fprintf(stderr, "ftrunc %s error=%d\n", filename, errno);
        mret = errno;
        goto exit2;
    }

#define K 1024

    blksize = 64 * K * K;
    numblks = (filesize + blksize - 1) / blksize;
    for (i = 0; i < numblks && mret == 0; i++) {

        offset = i*blksize;
        if (offset + blksize > filesize)
            blksize = filesize - offset;

        /* map file */
        file_addr = mmap(NULL, blksize,
                PROT_READ | PROT_WRITE, MAP_SHARED, fid, offset);
        if (file_addr == MAP_FAILED) {
            fprintf(stderr, "mmap %s error=%d\n", filename, errno);
            mret = errno;
            break;
        }

	/* tag each block (to aid debug) */
	p = file_addr;
	q = file_addr + blksize - K;
	memset(p, ' ', K);
	snprintf(p, K, "\nblk=%d\n\n", i);
	p += K;

        /* fill something into mapped addr */
	while (p < q) {
		memset(p, ' ', K);
		snprintf(p, K, "\noff=0x%x\n\n",
		    (i * blksize) + (p - file_addr));
		p += K;
	}

        /* sync mapped pages to file */
        if (msync(file_addr, blksize, MS_SYNC) == -1) {
            fprintf(stderr, "msync %s error=%d\n", filename, errno);
            mret = errno;
        }

        /* unmap file */
        if (munmap(file_addr, blksize) == -1) {
            fprintf(stderr, "munmap %s error=%d\n", filename, errno);
            mret = errno;
        }
    }

    /* close file */
exit2:
    close(fid);
exit3:
    return mret;
}
