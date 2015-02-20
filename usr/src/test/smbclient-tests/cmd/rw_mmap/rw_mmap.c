#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* Test if file read/write is coherent with mmap, perform 2 tests:
 * modify file through mmap, and check the result through file read.
 * modify file through file write, and check the result through mmap.
 */

int main(int argc, char *argv[]) {

    char *filename = NULL;
    char *file_addr;
    char *p;
    size_t filesize;
    size_t blksize;
    long cnt = 1;
    long i, j;
    int fid;
    char c;
    char *buf;
    char sbuf[16];

    if (argc != 5) {
        fprintf(stderr, "\tusage:\n\trw_mmap -n <size>[b|k|m|g] -f <filename>\n");
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
        return errno;
    }

    /* extend file */
    filesize = cnt;
    if (ftruncate(fid, filesize) == -1) {
        fprintf(stderr, "ftrunc %s error=%d\n", filename, errno);
        return errno;
    }

    /* map file */
    file_addr = mmap(NULL, filesize,
            PROT_READ | PROT_WRITE, MAP_SHARED, fid, 0);
    if (file_addr == MAP_FAILED) {
        fprintf(stderr, "mmap %s error=%d\n", filename, errno);
        return errno;
    }

    blksize = 4096;
    buf = malloc(blksize);

    /* verify fread and mmap see the same data */
    p = file_addr + 2013; /* not aligned to 4KB, on purpose */
    lseek(fid, 2013, SEEK_SET);
    while (p < file_addr + filesize) {
        blksize = read(fid, buf, blksize);
        if (memcmp(buf, p, blksize) != 0) {
            fprintf(stderr, "memcmp failed 1\n");
            return -1;
        }
        p += blksize;
    }

    /* modify file through mmap, verify fread can see the change */
    blksize = 4096;
    p = file_addr + 2013; /* not aligned to 4KB */
    lseek(fid, 2013, SEEK_SET);
    c = 'a';
    while (p < file_addr + filesize) {
        if (p + blksize > file_addr + filesize)
            blksize = file_addr + filesize - p;
        memset(p, c++, blksize);
        blksize = read(fid, buf, blksize);
        if (memcmp(buf, p, blksize) != 0) {
            fprintf(stderr, "memcmp failed 2\n");
            return -2;
        }
        p += blksize;
    }

    /* modify file through fwrite, verify mmap can see the change */
    blksize = 4096;
    p = file_addr + 2013; /* not aligned to 4KB */
    lseek(fid, 2013, SEEK_SET);
    c = 'Z';
    while (p < file_addr + filesize) {
        if (p + blksize > file_addr + filesize)
            blksize = file_addr + filesize - p;
        memset(buf, c--, blksize);
        blksize = write(fid, buf, blksize);
        if (memcmp(buf, p, blksize) != 0) {
            fprintf(stderr, "memcmp failed 3\n");
            return -3;
        }
        p += blksize;
    }

    /* sync pages to file */
    if (msync(file_addr, filesize, MS_SYNC) == -1) {
        fprintf(stderr, "msync %s error=%d\n", filename, errno);
        return errno;
    }

    /* unmap file */
    if (munmap(file_addr, filesize) == -1) {
        fprintf(stderr, "munmap %s error=%d\n", filename, errno);
        return errno;
    }

    /* close file */
    if (close(fid) == -1) {
        fprintf(stderr, "close %s error=%d\n", filename, errno);
        return errno;
    }

    return 0;
}
