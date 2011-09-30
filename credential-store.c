#include "cache.h"
#include "credential.h"
#include "string-list.h"
#include "parse-options.h"
#include "quote.h"

static struct lock_file credential_lock;

static int parse_credential_file(const char *fn,
				 struct credential *c,
				 int (*match_cb)(const char *username,
						 const char *password,
						 struct credential *c),
				 int (*other_cb)(const char *buf))
{
	FILE *fh;
	struct strbuf buf = STRBUF_INIT;
	const char **argv = NULL;
	int alloc = 0;
	int retval = 0;

	fh = fopen(fn, "r");
	if (!fh)
		return errno == ENOENT ? 0 : -1;

	while (strbuf_getwholeline(&buf, fh, '\n') != EOF) {
		int nr = 0;
		char *pristine = xstrdup(buf.buf);

		strbuf_trim(&buf);
		if (!sq_dequote_to_argv(buf.buf, &argv, &nr, &alloc) &&
		    nr == 3 &&
		    !strcmp(c->unique, argv[0]) &&
		    (!c->username || !strcmp(c->username, argv[1]))) {
			if (match_cb(argv[1], argv[2], c) < 0) {
				retval = -1;
				break;
			}
		}
		else if (other_cb) {
			if (other_cb(pristine) < 0) {
				retval = -1;
				break;
			}
		}
		free(pristine);
	}

	free(argv);
	strbuf_release(&buf);
	fclose(fh);
	return retval;
}


static int copy_credential(const char *username, const char *password,
			   struct credential *c)
{
	if (!c->username)
		c->username = xstrdup(username);
	free(c->password);
	c->password = xstrdup(password);
	return 0;
}

static int lookup_credential(const char *fn, struct credential *c)
{
	if (!c->unique)
		return 0;
	parse_credential_file(fn, c, copy_credential, NULL);
	return c->username && c->password;
}

static int skip_match(const char *username, const char *password,
		      struct credential *c)
{
	return 0;
}

static int print_entry(const char *buf)
{
	return write_in_full(credential_lock.fd, buf, strlen(buf));
}

static int rewrite_credential_file(const char *fn, struct credential *c,
				   int replace)
{
	umask(077);
	if (hold_lock_file_for_update(&credential_lock, fn, 0) < 0)
		return -1;
	if (parse_credential_file(fn, c, skip_match, print_entry) < 0) {
		rollback_lock_file(&credential_lock);
		return -1;
	}
	if (replace) {
		struct strbuf buf = STRBUF_INIT;
		int r;

		sq_quote_buf(&buf, c->unique);
		strbuf_addch(&buf, ' ');
		sq_quote_buf(&buf, c->username);
		strbuf_addch(&buf, ' ');
		sq_quote_buf(&buf, c->password);
		strbuf_addch(&buf, '\n');

		r = write_in_full(credential_lock.fd, buf.buf, buf.len);
		strbuf_release(&buf);
		if (r < 0) {
			rollback_lock_file(&credential_lock);
			return -1;
		}
	}
	return commit_lock_file(&credential_lock);
}

static void store_credential(const char *fn, struct credential *c)
{
	if (!c->unique || !c->username || !c->password)
		return;
	rewrite_credential_file(fn, c, 1);
}

static void remove_credential(const char *fn, struct credential *c)
{
	if (!c->unique)
		return;
	rewrite_credential_file(fn, c, 0);
}

int main(int argc, const char **argv)
{
	const char * const usage[] = {
		"git credential-store [options]",
		NULL
	};
	struct credential c = { NULL };
	struct string_list chain = STRING_LIST_INIT_NODUP;
	char *store = NULL;
	int reject = 0;
	struct option options[] = {
		OPT_STRING_LIST(0, "store", &store, "file",
				"fetch and store credentials in <file>"),
		OPT_STRING_LIST(0, "chain", &chain, "helper",
				"use <helper> to get non-cached credentials"),
		OPT_BOOLEAN(0, "reject", &reject,
			    "reject a stored credential"),
		OPT_STRING(0, "username", &c.username, "name",
			   "an existing username"),
		OPT_STRING(0, "description", &c.description, "desc",
			   "human-readable description of the credential"),
		OPT_STRING(0, "unique", &c.unique, "token",
			   "a unique context for the credential"),
		OPT_END()
	};

	argc = parse_options(argc, argv, NULL, options, usage, 0);
	if (argc)
		usage_with_options(usage, options);

	if (!store)
		store = expand_user_path("~/.git-credentials");
	if (!store)
		die("unable to set up default store; use --store");

	if (reject)
		remove_credential(store, &c);
	else {
		if (!lookup_credential(store, &c)) {
			credential_fill(&c, &chain);
			store_credential(store, &c);
		}
		printf("username=%s\n", c.username);
		printf("password=%s\n", c.password);
	}
	return 0;
}
