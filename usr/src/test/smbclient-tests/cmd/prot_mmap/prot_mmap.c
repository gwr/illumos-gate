#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* use mmap to copy data from src file to des file,
 * with given flags and modes.
 * the src & des file should exist and have the same size. */

int main(int argc, char *argv[]) {

    struct stat sb;
    char *src_addr, *des_addr;
    char *src_file = NULL, *des_file = NULL;
    off_t offset;
    size_t filesize;
    size_t blksize;
    long numblks;
    int src_fid, des_fid;
    int mret = 0;
    long i, j;
    int flags0 = 0, mflags0 = 0, prot0 = 0; /* flags for src file */
    int flags1 = 0, mflags1 = 0, prot1 = 0; /* flags for des file */

    /* parse arguments */
    if (argc != 10) {
        fprintf(stderr,
                "\tusage:\n\t"
                "prot_mmap -o <r|w> <r|w> "
                "-m <r|w|s|p> <r|w|s|p>"
                " -f <srcfile> <desfile>\n");
        return -7;
    }
    for (i = 1; i < argc;) {
        switch (argv[i][1]) {
            case 'o':/* options for open() */
                i++;
                for (j = 0; argv[i][j]; j++) {
                    if (argv[i][j] == 'r')
                        flags0 |= O_RDONLY;
                    else if (argv[i][j] == 'w')
                        flags0 |= O_WRONLY;
                }
                if ((flags0 & (O_RDONLY | O_WRONLY)) == (O_RDONLY | O_WRONLY))
                    flags0 = O_RDWR;
                i++;
                for (j = 0; argv[i][j]; j++) {
                    if (argv[i][j] == 'r')
                        flags1 |= O_RDONLY;
                    else if (argv[i][j] == 'w')
                        flags1 |= O_WRONLY;
                }
                if ((flags1 & (O_RDONLY | O_WRONLY)) == (O_RDONLY | O_WRONLY))
                    flags1 = O_RDWR;
                i++;
                break;
            case 'm':/* options for mmap() */
                i++;
                for (j = 0; argv[i][j]; j++) {
                    if (argv[i][j] == 'r')
                        prot0 |= PROT_READ;
                    else if (argv[i][j] == 'w')
                        prot0 |= PROT_WRITE;
                    else if (argv[i][j] == 's')
                        mflags0 |= MAP_SHARED;
                    else if (argv[i][j] == 'p')
                        mflags0 |= MAP_PRIVATE;
                }
                i++;
                for (j = 0; argv[i][j]; j++) {
                    if (argv[i][j] == 'r')
                        prot1 |= PROT_READ;
                    else if (argv[i][j] == 'w')
                        prot1 |= PROT_WRITE;
                    else if (argv[i][j] == 's')
                        mflags1 |= MAP_SHARED;
                    else if (argv[i][j] == 'p')
                        mflags1 |= MAP_PRIVATE;
                }
                i++;
                break;
            case 'f':/* src file and des file */
                i++;
                src_file = argv[i];
                i++;
                des_file = argv[i];
                i++;
        }
    }


    /* source file */
    src_fid = open(src_file, flags0);
    if (src_fid == -1) {
        fprintf(stderr, "open %s error=%d\n", src_file, errno);
        return errno;
    }
    /* destination file */
    des_fid = open(des_file, flags1);
    if (des_fid == -1) {
        fprintf(stderr, "open %s error=%d\n", des_file, errno);
        mret = errno;
        goto exit3;
    }

    /* get file size */
    if (fstat(src_fid, &sb) == -1) {
        fprintf(stderr, "fstat %s error=%d\n", src_file, errno);
        mret = errno;
        goto exit2;
    }
    filesize = sb.st_size;
    if (fstat(des_fid, &sb) == -1) {
        fprintf(stderr, "fstat %s error=%d\n", des_file, errno);
        mret = errno;
        goto exit2;
    }
    if (filesize != sb.st_size) {
        mret = -8;
        goto exit2;
    }

    /* copy data */
    blksize = 64 * 1024 * 1024;
    numblks = (filesize + blksize - 1) / blksize;
    for (i = 0; i < numblks && mret == 0; i++) {

        offset = (i % numblks) * blksize;
        if (offset + blksize > filesize)
            blksize = filesize - offset;

        /* map file */
        src_addr = mmap(NULL, blksize, prot0, mflags0, src_fid, offset);
        if (src_addr == MAP_FAILED) {
            fprintf(stderr, "mmap %s error=%d\n", src_file, errno);
            mret = errno;
            break;
        }
        des_addr = mmap(NULL, blksize, prot1, mflags1, des_fid, offset);
        if (des_addr == MAP_FAILED) {
            fprintf(stderr, "mmap %s error=%d\n", des_file, errno);
            mret = errno;
            goto exit1;
        }

        /* cp data from src addr to des addr */
        memcpy(des_addr, src_addr, blksize);
        /* sync mapped pages to file */
        if (msync(des_addr, blksize, MS_SYNC) == -1) {
            fprintf(stderr, "msync %s error=%d\n", des_file, errno);
            mret = errno;
        }

        /* unmap file */
        if (munmap(des_addr, blksize) == -1) {
            fprintf(stderr, "munmap %s error=%d\n", des_file, errno);
            mret = errno;
        }
exit1:
        if (munmap(src_addr, blksize) == -1) {
            fprintf(stderr, "munmap %s error=%d\n", src_file, errno);
            mret = errno;
        }
    }

    /* close file */
exit2:
    close(des_fid);
exit3:
    close(src_fid);

    return mret;
}
