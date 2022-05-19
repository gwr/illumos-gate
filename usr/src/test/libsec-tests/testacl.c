
/*
 * Try this:
 testacl "sid:S-1-5-21-1813420391-1960978090-3893453006-1000:rwxpd-aARWc--s:fd-----:allow"
*/

#include <sys/types.h>
#include <sys/acl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <aclutils.h>

int
main(int argc, char **argv)
{
	acl_t	*aclp = NULL;	/* acl info */
	char	*str;
	int i, err;
	int flags = ACL_COMPACT_FMT | ACL_SID_FMT;

	for (i = 1; i < argc; i++) {
		err = acl_fromtext(argv[i], &aclp);
		if (err != 0) {
			fprintf(stderr, "acl_fromtext(%s): %s\n",
				argv[i], acl_strerror(err));
			return (1);
		}
		str = acl_totext(aclp, flags);
		printf("acl_totext: %s\n", str);
		acl_free(aclp);
		aclp = NULL;
	}

	return (0);
}
