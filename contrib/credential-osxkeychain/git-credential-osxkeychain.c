/* Copyright 2011 Jay Soffian. All rights reserved.
 * FreeBSD License.
 *
 * A git credential helper that interfaces with the Mac OS X keychain
 * via the Security framework.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <Security/Security.h>

static void die(const char *err, ...)
{
	char msg[4096];
	va_list params;
	va_start(params, err);
	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, "%s\n", msg);
	va_end(params);
	exit(1);
}

void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (!ret)
		die("Out of memory");
	return ret;
}

void *xstrdup(const char *s1)
{
	void *ret = strdup(s1);
	if (!ret)
		die("Out of memory");
	return ret;
}

void emit_user_pass(char *username, char *password)
{
	if (username)
		printf("username=%s\n", username);
	if (password)
		printf("password=%s\n", password);
}

typedef enum { USERNAME, PASSWORD } prompt_type;

void prompt(FILE *file, const char *what, const char *desc)
{
	if (desc)
		fprintf(file, "%s for '%s': ", what, desc);
	else
		fprintf(file, "%s: ", what);
}

char *prompt_tty(prompt_type what, char *description)
{
	struct termios old;
	struct termios new;
	char buf[128];
	int buf_len;
	int fd = open("/dev/tty", O_RDWR|O_NOCTTY);
	FILE *tty = fdopen(fd, "w+");
	if (what == USERNAME) {
		prompt(tty, "Username", description);
	}
	else {
		prompt(tty, "Password", description);
		tcgetattr(fd, &old);
		memcpy(&new, &old, sizeof(struct termios));
		new.c_lflag &= ~ECHO;
		tcsetattr(fd, TCSADRAIN, &new);
	}
	if (!fgets(buf, sizeof(buf), tty)) {
		fprintf(tty, "\n");
		fclose(tty);
		return NULL;
	}
	if (what == PASSWORD) {
		tcsetattr(fd, TCSADRAIN, &old);
		fprintf(tty, "\n");
	}
	fclose(tty);
	buf_len = strlen(buf);
	if (buf[buf_len-1] == '\n')
		buf[buf_len-1] = '\0';
	return xstrdup(buf);
}

char *username_from_keychain_item(SecKeychainItemRef item)
{
	OSStatus status;
	SecKeychainAttributeList list;
	SecKeychainAttribute attr;
	list.count = 1;
	list.attr = &attr;
	attr.tag = kSecAccountItemAttr;
	char *username;

	status = SecKeychainItemCopyContent(item, NULL, &list, NULL, NULL);
	if (status != noErr)
		return NULL;
	username = xmalloc(attr.length + 1);
	strncpy(username, attr.data, attr.length);
	username[attr.length] = '\0';
	SecKeychainItemFreeContent(&list, NULL);
	return username;
}

int find_internet_password(SecProtocolType protocol,
			   char *hostname,
			   char *username)
{
	void *password_buf;
	UInt32 password_len;
	OSStatus status;
	char *password;
	int free_username = 0;
	SecKeychainItemRef item;

	status = SecKeychainFindInternetPassword(
			NULL,
			strlen(hostname), hostname,
			0, NULL,
			username ? strlen(username) : 0, username,
			0, NULL,
			0,
			protocol,
			kSecAuthenticationTypeDefault,
			&password_len, &password_buf,
			&item);
	if (status != noErr)
		return -1;

	password = xmalloc(password_len + 1);
	strncpy(password, password_buf, password_len);
	password[password_len] = '\0';
	SecKeychainItemFreeContent(NULL, password_buf);
	if (!username) {
		username = username_from_keychain_item(item);
		free_username = 1;
	}
	emit_user_pass(username, password);
	if (free_username)
		free(username);
	free(password);
	return 0;
}

void delete_internet_password(SecProtocolType protocol,
			      char *hostname,
			      char *username)
{
	OSStatus status;
	SecKeychainItemRef item;

	status = SecKeychainFindInternetPassword(
			NULL,
			strlen(hostname), hostname,
			0, NULL,
			username ? strlen(username) : 0, username,
			0, NULL,
			0,
			protocol,
			kSecAuthenticationTypeDefault,
			0, NULL,
			&item);
	if (status != noErr)
		return;
	SecKeychainItemDelete(item);
}

void add_internet_password(SecProtocolType protocol,
			   char *hostname,
			   char *username,
			   char *password,
			   char *comment)
{
	const char *label_format = "%s (%s)";
	char *label;
	OSStatus status;
	SecKeychainItemRef item;
	SecKeychainAttributeList list;
	SecKeychainAttribute attr;
	list.count = 1;
	list.attr = &attr;
	status = SecKeychainAddInternetPassword(
			NULL,
			strlen(hostname), hostname,
			0, NULL,
			strlen(username), username,
			0, NULL,
			0,
			protocol,
			kSecAuthenticationTypeDefault,
			strlen(password), password,
			&item);
	if (status != noErr)
		return;

	/* set the comment */
	attr.tag = kSecCommentItemAttr;
	attr.data = comment;
	attr.length = strlen(comment);
	SecKeychainItemModifyContent(item, &list, 0, NULL);

	/* override the label */
	label = xmalloc(strlen(hostname) + strlen(username) +
			strlen(label_format));
	sprintf(label, label_format, hostname, username);
	attr.tag = kSecLabelItemAttr;
	attr.data = label;
	attr.length = strlen(label);
	SecKeychainItemModifyContent(item, &list, 0, NULL);
}

int main(int argc, const char **argv)
{
	const char *usage =
		"Usage: git credential-osxkeychain --unique=TOKEN [options]\n"
		"Options:\n"
		"    --description=DESCRIPTION\n"
		"    --username=USERNAME\n"
		"    --reject";
	char *description = NULL, *username = NULL, *unique = NULL;
	char *hostname, *password;
	int i, free_username = 0, reject = 0;
	SecProtocolType protocol = 0;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strncmp(arg, "--description=", 14)) {
			description = (char *) arg + 14;
		}
		else if (!strncmp(arg, "--username=", 11)) {
			username = (char *) arg + 11;
		}
		else if (!strncmp(arg, "--unique=", 9)) {
			unique = (char *) arg + 9;
		}
		else if (!strcmp(arg, "--reject")) {
			reject = 1;
		}
		else if (!strcmp(arg, "--help")) {
			die(usage);
		}
		else
			die("Unrecognized argument `%s'; try --help", arg);
	}

	if (!unique)
		die("Must specify --unique=TOKEN; try --help");

	hostname = strchr(unique, ':');
	if (!hostname)
		die("Invalid token `%s'", unique);
	*hostname++ = '\0';

	/* "GitHub for Mac" compatibility */
	if (!strcmp(hostname, "github.com"))
		hostname = "github.com/mac";

	if (!strcmp(unique, "https")) {
		protocol = kSecProtocolTypeHTTPS;
	} else if (!strcmp(unique, "http")) {
		protocol = kSecProtocolTypeHTTP;
	}
	else
		die("Unrecognized protocol `%s'", unique);

	/* if this is a rejection delete the existing creds */
	if (reject) {
		delete_internet_password(protocol, hostname, username);
		return 0;
	}

	/* otherwise look for a matching keychain item */
	if (!find_internet_password(protocol, hostname, username))
		return 0;

	/* no keychain item found, prompt the user and store the result */
	if (!username) {
		if (!(username = prompt_tty(USERNAME, description)))
			return 0;
		free_username = 1;
	}
	if (!(password = prompt_tty(PASSWORD, description)))
		return 0;

	add_internet_password(protocol, hostname, username, password,
			      description ? description : "default");
	emit_user_pass(username, password);
	if (free_username)
		free(username);
	free(password);
	return 0;
}
