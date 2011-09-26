#!/usr/bin/python
# Copyright 2011 Jay Soffian. All rights reserved.
# FreeBSD License.
"""
A git credential helper that interfaces with the Mac OS X keychain via
/usr/bin/security.
"""

import os
import re
import sys
import termios
from getpass import _raw_input
from optparse import OptionParser
from subprocess import Popen, PIPE

USERNAME = 'USERNAME'
PASSWORD = 'PASSWORD'
PROMPTS = dict(USERNAME='Username', PASSWORD='Password')

def prompt_tty(what, desc):
    """Prompt on TTY for username or password with optional description"""
    prompt = '%s%s: ' % (PROMPTS[what], " for '%s'" % desc if desc else '')
    # Borrowed mostly from getpass.py
    fd = os.open('/dev/tty', os.O_RDWR|os.O_NOCTTY)
    tty = os.fdopen(fd, 'w+', 1)
    if what == USERNAME:
        return _raw_input(prompt, tty, tty)
    old = termios.tcgetattr(fd) # a copy to save
    new = old[:]
    new[3] &= ~termios.ECHO  # 3 == 'lflags'
    try:
        termios.tcsetattr(fd, termios.TCSADRAIN, new)
        return _raw_input(prompt, tty, tty)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        tty.write('\n')

def emit_user_pass(username, password):
    if username:
        print 'username=' + username
    if password:
        print 'password=' + password

def make_security_args(command, protocol, hostname, username):
    args = ['/usr/bin/security', command]
    # tlfd is 'dflt' backwards - obvious /usr/bin/security bug
    # but allows us to ignore matching saved web forms.
    args.extend(['-t', 'tlfd'])
    args.extend(['-r', protocol])
    if hostname:
        args.extend(['-s', hostname])
    if username:
        args.extend(['-a', username])
    return args

def find_internet_password(protocol, hostname, username):
    args = make_security_args('find-internet-password',
                              protocol, hostname, username)
    args.append('-g') # asks for password on stderr
    p = Popen(args, stdin=PIPE, stdout=PIPE, stderr=PIPE)
    # grok stdout for username
    out, err = p.communicate()
    if p.returncode != 0:
        return
    for line in out.splitlines(): # pylint:disable-msg=E1103
        m = re.search(r'^\s+"acct"<blob>=[^"]*"(.*)"$', line)
        if m:
            username = m.group(1)
            break
    # grok stderr for password
    m = re.search(r'^password:[^"]*"(.*)"$', err)
    if not m:
        return
    emit_user_pass(username, m.group(1))
    return True

def delete_internet_password(protocol, hostname, username):
    args = make_security_args('delete-internet-password',
                              protocol, hostname, username)
    p = Popen(args, stdin=PIPE, stdout=PIPE, stderr=PIPE)
    p.communicate()

def add_internet_password(protocol, hostname, username, password):
    # We do this over a pipe so that we can provide the password more
    # securely than as an argument which would show up in ps output.
    # Unfortunately this is possibly less robust since the security man
    # page does not document how to quote arguments. Emprically it seems
    # that using the double-quote, escaping \ and " works properly.
    username = username.replace('\\', '\\\\').replace('"', '\\"')
    password = password.replace('\\', '\\\\').replace('"', '\\"')
    command = ' '.join([
        'add-internet-password', '-U',
        '-r', protocol,
        '-s', hostname,
        '-a "%s"' % username,
        '-w "%s"' % password,
        '-j default',
        '-l "%s (%s)"' % (hostname, username),
    ]) + '\n'
    args = ['/usr/bin/security', '-i']
    p = Popen(args, stdin=PIPE, stdout=PIPE, stderr=PIPE)
    p.communicate(command)

def main():
    p = OptionParser()
    p.add_option('--description')
    p.add_option('--reject', action='store_true')
    p.add_option('--unique', dest='token', help='REQUIRED OPTION')
    p.add_option('--username')
    opts, _ = p.parse_args()

    if not opts.token:
        p.error('--unique option required')
    if not ':' in opts.token:
        print >> sys.stderr, "Invalid token: '%s'" % opts.token
        return 1
    protocol, hostname = opts.token.split(':', 1)
    if protocol not in ('http', 'https'):
        print >> sys.stderr, "Unsupported protocol: '%s'" % protocol
        return 1
    if protocol == 'https':
        protocol = 'htps'

    # "GitHub for Mac" compatibility
    if hostname == 'github.com':
        hostname = 'github.com/mac'

    # if this is a rejection delete the existing creds
    if opts.reject:
        delete_internet_password(protocol, hostname, opts.username)
        return 0

    # otherwise look for creds
    if find_internet_password(protocol, hostname, opts.username):
        return 0

    # creds not found, so prompt the user then store the creds
    username = opts.username
    if username is None:
        username = prompt_tty(USERNAME, opts.description)
    password = prompt_tty(PASSWORD, opts.description)
    add_internet_password(protocol, hostname, username, password)
    emit_user_pass(username, password)
    return 0

if __name__ == '__main__':
    sys.exit(main())
