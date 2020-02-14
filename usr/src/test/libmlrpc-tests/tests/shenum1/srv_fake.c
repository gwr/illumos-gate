

#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <time.h>
#include <thread.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libmlrpc/libmlrpc.h>
#include "srv_fake.h"

int
smb_getnetbiosname(char *buf, size_t buflen)
{
	strlcpy(buf, "gwr153ns5", buflen);
	return (0);
}

int
smb_config_get_comment(char *cbuf, int bufsz)
{
	strlcpy(cbuf, "test server", bufsz);
	return (0);
}

void
smb_config_get_version(smb_version_t *version)
{
	static smb_version_t myvers = {
		.sv_size = 0,
		.sv_major = 6,
		.sv_minor = 1,
		.sv_build_number = 7007,
		.sv_platform_id = 0 };
	*version = myvers;
}

char *
strsubst(char *s, char orgchar, char newchar)
{
	char *p = s;

	if (p == 0)
		return (0);

	while (*p) {
		if (*p == orgchar)
			*p = newchar;
		++p;
	}

	return (s);
}

smb_share_t fake_shares[] = {
	{
		.shr_name = "c$",
		.shr_path = "/var/smb/c",
		.shr_cmnt = "Default Share",
		.shr_flags = 0
	}, {
		.shr_name = "testca",
		.shr_path = "/export/test",
		.shr_cmnt = "",
		.shr_flags = 0
	}, {
		.shr_name = "ipc$",
		.shr_path = "/var/smb/pipe",
		.shr_cmnt = "Remote IPC",
		.shr_flags = 0,
		.shr_type = 3
	}, {
		.shr_name = "test",
		.shr_path = "/export/test",
		.shr_cmnt = "",
		.shr_flags = 0
	}
};

int
smb_shr_count(void)
{
	return (sizeof (fake_shares) / sizeof (fake_shares[0]));
}

void
smb_shr_iterinit(smb_shriter_t *si)
{
	bzero(si, sizeof (*si));
}

smb_share_t *
smb_shr_iterate(smb_shriter_t *si)
{
	int idx = si->si_idx;

	si->si_share = fake_shares[idx];
	si->si_idx = idx + 1;
	si->si_first = (idx == 0);

	return (&si->si_share);

}

char
smb_shr_drive_letter(const char *path)
{
	return ('\0');
}
