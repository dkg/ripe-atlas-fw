/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 */

#include "libbb.h"
#include <syslog.h>


static char* new_password( int algo, char *pass)
{
	char salt[sizeof("$N$XXXXXXXX")]; /* "$N$XXXXXXXX" or "XX" */
	char *ret = NULL; /* failure so far */

	crypt_make_salt(salt, 1, 0); /* des */
	if (algo) { /* MD5 */
		strcpy(salt, "$1$");
		crypt_make_salt(salt + 3, 4, 0);
	}
	/* pw_encrypt returns malloced str */
	ret = pw_encrypt(pass, salt, 1);
	/* whee, success! */

	return ret;
}

int passwd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int passwd_main(int argc UNUSED_PARAM, char **argv)
{
	enum {
		OPT_algo = 0x1, /* -a - password algorithm */
		OPT_lock = 0x2, /* -l - lock account */
		OPT_unlock = 0x4, /* -u - unlock account */
		OPT_delete = 0x8, /* -d - delete password */
		OPT_lud = 0xe,
		STATE_ALGO_md5 = 0x10,
		//STATE_ALGO_des = 0x20, not needed yet
	};
	unsigned opt;
	int rc;
	const char *opt_a = "";
	const char *filename;
	char *myname;
	char *name;
	char *newp;
	char *pass;
	struct passwd *pw;
	uid_t myuid;
	struct rlimit rlimit_fsize;
#if ENABLE_FEATURE_SHADOWPASSWDS
	/* Using _r function to avoid pulling in static buffers */
	struct spwd spw;
	char buffer[256];
#endif

	logmode = LOGMODE_BOTH;
	openlog(applet_name, LOG_NOWAIT, LOG_AUTH);
	opt = getopt32(argv, "a:lud", &opt_a);
	//argc -= optind;
	argv += optind;

	if (strcasecmp(opt_a, "des") != 0) /* -a */
		opt |= STATE_ALGO_md5;
	//else
	//	opt |= STATE_ALGO_des;
	myuid = getuid();
	/* -l, -u, -d require root priv and username argument */
	if ((opt & OPT_lud) && (myuid || !argv[0]))
		bb_show_usage();

	/* Will complain and die if username not found */
	myname = xstrdup(bb_getpwuid(NULL, -1, myuid));

	if( argc<3 ) {
		bb_error_msg_and_die("You should supply a name ans a password");
	}

	name = argv[0];
	pass = argv[1];
	pw = getpwnam(name);
	if (!pw)
		bb_error_msg_and_die("unknown user %s", name);
	if (myuid && pw->pw_uid != myuid) {
		/* LOGMODE_BOTH */
		bb_error_msg_and_die("%s can't change password for %s", myname, name);
	}

	newp = new_password( opt & STATE_ALGO_md5, pass);

#if ENABLE_FEATURE_SHADOWPASSWDS
	{
		/* getspnam_r may return 0 yet set result to NULL.
		 * At least glibc 2.4 does this. Be extra paranoid here. */
		struct spwd *result = NULL;
		if (getspnam_r(pw->pw_name, &spw, buffer, sizeof(buffer), &result)
		 || !result || strcmp(result->sp_namp, pw->pw_name) != 0) {
			/* LOGMODE_BOTH */
			bb_error_msg("no record of %s in %s, using %s",
					name, bb_path_shadow_file,
					bb_path_passwd_file);
		} else {
			pw->pw_passwd = result->sp_pwdp;
		}
	}
#endif

	rlimit_fsize.rlim_cur = rlimit_fsize.rlim_max = 512L * 30000;
	setrlimit(RLIMIT_FSIZE, &rlimit_fsize);
	bb_signals(0
		+ (1 << SIGHUP)
		+ (1 << SIGINT)
		+ (1 << SIGQUIT)
		, SIG_IGN);
	umask(077);
	xsetuid(0);

#if ENABLE_FEATURE_SHADOWPASSWDS
	filename = bb_path_shadow_file;
	rc = update_passwd(bb_path_shadow_file, name, newp );
	if (rc == 0) /* no lines updated, no errors detected */
#endif
	{
		filename = bb_path_passwd_file;
		rc = update_passwd(bb_path_passwd_file, name, newp);
	}
	/* LOGMODE_BOTH */
	if (rc < 0)
		bb_error_msg_and_die("cannot update password file %s",
				filename);
	bb_info_msg("Password for %s changed by %s", name, myname);

	//if (ENABLE_FEATURE_CLEAN_UP) free(newp);

	if (!newp) {
		bb_error_msg_and_die("password for %s is already %slocked",
			name, (opt & OPT_unlock) ? "un" : "");
	}
	if (ENABLE_FEATURE_CLEAN_UP) free(myname);
	return 0;
}
