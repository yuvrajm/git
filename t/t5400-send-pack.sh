#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='See why rewinding head breaks send-pack

'
. ./test-lib.sh

cnt=64
test_expect_success setup '
	test_tick &&
	mkdir mozart mozart/is &&
	echo "Commit #0" >mozart/is/pink &&
	git update-index --add mozart/is/pink &&
	tree=$(git write-tree) &&
	commit=$(echo "Commit #0" | git commit-tree $tree) &&
	zero=$commit &&
	parent=$zero &&
	i=0 &&
	while test $i -le $cnt
	do
	    i=$(($i+1)) &&
	    test_tick &&
	    echo "Commit #$i" >mozart/is/pink &&
	    git update-index --add mozart/is/pink &&
	    tree=$(git write-tree) &&
	    commit=$(echo "Commit #$i" | git commit-tree $tree -p $parent) &&
	    git update-ref refs/tags/commit$i $commit &&
	    parent=$commit || return 1
	done &&
	git update-ref HEAD "$commit" &&
	git clone ./. victim &&
	( cd victim && git config receive.denyCurrentBranch warn && git log ) &&
	git update-ref HEAD "$zero" &&
	parent=$zero &&
	i=0 &&
	while test $i -le $cnt
	do
	    i=$(($i+1)) &&
	    test_tick &&
	    echo "Rebase #$i" >mozart/is/pink &&
	    git update-index --add mozart/is/pink &&
	    tree=$(git write-tree) &&
	    commit=$(echo "Rebase #$i" | git commit-tree $tree -p $parent) &&
	    git update-ref refs/tags/rebase$i $commit &&
	    parent=$commit || return 1
	done &&
	git update-ref HEAD "$commit" &&
	echo Rebase &&
	git log'

test_expect_success 'pack the source repository' '
	git repack -a -d &&
	git prune
'

test_expect_success 'pack the destination repository' '
    (
	cd victim &&
	git repack -a -d &&
	git prune
    )
'

test_expect_success 'refuse pushing rewound head without --force' '
	pushed_head=$(git rev-parse --verify master) &&
	victim_orig=$(cd victim && git rev-parse --verify master) &&
	test_must_fail git send-pack ./victim master &&
	victim_head=$(cd victim && git rev-parse --verify master) &&
	test "$victim_head" = "$victim_orig" &&
	# this should update
	git send-pack --force ./victim master &&
	victim_head=$(cd victim && git rev-parse --verify master) &&
	test "$victim_head" = "$pushed_head"
'

test_expect_success \
        'push can be used to delete a ref' '
	( cd victim && git branch extra master ) &&
	git send-pack ./victim :extra master &&
	( cd victim &&
	  test_must_fail git rev-parse --verify extra )
'

test_expect_success 'refuse deleting push with denyDeletes' '
	(
	    cd victim &&
	    ( git branch -D extra || : ) &&
	    git config receive.denyDeletes true &&
	    git branch extra master
	) &&
	test_must_fail git send-pack ./victim :extra master
'

test_expect_success 'cannot override denyDeletes with git -c send-pack' '
	(
		cd victim &&
		test_might_fail git branch -D extra &&
		git config receive.denyDeletes true &&
		git branch extra master
	) &&
	test_must_fail git -c receive.denyDeletes=false \
					send-pack ./victim :extra master
'

test_expect_success 'override denyDeletes with git -c receive-pack' '
	(
		cd victim &&
		test_might_fail git branch -D extra &&
		git config receive.denyDeletes true &&
		git branch extra master
	) &&
	git send-pack \
		--receive-pack="git -c receive.denyDeletes=false receive-pack" \
		./victim :extra master
'

test_expect_success 'denyNonFastforwards trumps --force' '
	(
	    cd victim &&
	    ( git branch -D extra || : ) &&
	    git config receive.denyNonFastforwards true
	) &&
	victim_orig=$(cd victim && git rev-parse --verify master) &&
	test_must_fail git send-pack --force ./victim master^:master &&
	victim_head=$(cd victim && git rev-parse --verify master) &&
	test "$victim_orig" = "$victim_head"
'

test_expect_success 'push --all excludes remote-tracking hierarchy' '
	mkdir parent &&
	(
	    cd parent &&
	    git init && : >file && git add file && git commit -m add
	) &&
	git clone parent child &&
	(
	    cd child && git push --all
	) &&
	(
	    cd parent &&
	    test -z "$(git for-each-ref refs/remotes/origin)"
	)
'

rewound_push_setup() {
	rm -rf parent child &&
	mkdir parent &&
	(
	    cd parent &&
	    git init &&
	    echo one >file && git add file && git commit -m one &&
	    git config receive.denyCurrentBranch warn &&
	    echo two >file && git commit -a -m two
	) &&
	git clone parent child &&
	(
	    cd child && git reset --hard HEAD^
	)
}

rewound_push_succeeded() {
	cmp ../parent/.git/refs/heads/master .git/refs/heads/master
}

rewound_push_failed() {
	if rewound_push_succeeded
	then
		false
	else
		true
	fi
}

test_expect_success 'pushing explicit refspecs respects forcing' '
	rewound_push_setup &&
	parent_orig=$(cd parent && git rev-parse --verify master) &&
	(
	    cd child &&
	    test_must_fail git send-pack ../parent \
		refs/heads/master:refs/heads/master
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	test "$parent_orig" = "$parent_head" &&
	(
	    cd child &&
	    git send-pack ../parent \
	        +refs/heads/master:refs/heads/master
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" = "$child_head"
'

test_expect_success 'pushing wildcard refspecs respects forcing' '
	rewound_push_setup &&
	parent_orig=$(cd parent && git rev-parse --verify master) &&
	(
	    cd child &&
	    test_must_fail git send-pack ../parent \
	        "refs/heads/*:refs/heads/*"
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	test "$parent_orig" = "$parent_head" &&
	(
	    cd child &&
	    git send-pack ../parent \
	        "+refs/heads/*:refs/heads/*"
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" = "$child_head"
'

test_expect_success 'deny pushing to delete current branch' '
	rewound_push_setup &&
	(
	    cd child &&
	    test_must_fail git send-pack ../parent :refs/heads/master 2>errs
	)
'

echo "0000" > pkt-flush

test_expect_success 'verify that limit-commit-count capability is not advertised by default' '
	rewound_push_setup &&
	(
	    cd parent &&
	    test_might_fail git receive-pack . <../pkt-flush >output &&
	    test_must_fail grep -q "limit-commit-count" output
	)
'

test_expect_success 'verify that receive.commitCountLimit triggers limit-commit-count capability' '
	(
	    cd parent &&
	    git config receive.commitCountLimit 1 &&
	    test_might_fail git receive-pack . <../pkt-flush >output &&
	    grep -q "limit-commit-count=1" output
	)
'

test_expect_success 'deny pushing when receive.commitCountLimit is exceeded' '
	(
	    cd child &&
	    git reset --hard origin/master &&
	    echo three > file && git commit -a -m three &&
	    echo four > file && git commit -a -m four &&
	    test_must_fail git send-pack ../parent master 2>errs &&
	    grep -q "commit count limit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" != "$child_head"
'

test_expect_success 'repeated push failure proves that objects were not stored remotely' '
	(
	    cd child &&
	    test_must_fail git send-pack ../parent master 2>errs &&
	    grep -q "commit count limit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" != "$child_head"
'

test_expect_success 'increase receive.commitCountLimit' '
	(
	    cd parent &&
	    git config receive.commitCountLimit 2 &&
	    test_might_fail git receive-pack . <../pkt-flush >output &&
	    grep -q "limit-commit-count=2" output
	)
'

test_expect_success 'push is allowed when commit limit is not exceeded' '
	(
	    cd child &&
	    git send-pack ../parent master 2>errs &&
	    test_must_fail grep -q "commit count limit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" = "$child_head"
'

test_expect_success 'verify that limit-pack-size capability is not advertised by default' '
	rewound_push_setup &&
	(
	    cd parent &&
	    test_might_fail git receive-pack . <../pkt-flush >output &&
	    test_must_fail grep -q "limit-pack-size" output
	)
'

test_expect_success 'verify that receive.packSizeLimit triggers limit-pack-size capability' '
	(
	    cd parent &&
	    git config receive.packSizeLimit 10 &&
	    test_might_fail git receive-pack . <../pkt-flush >output &&
	    grep -q "limit-pack-size=10" output
	)
'

test_expect_success 'deny pushing when receive.packSizeLimit is exceeded' '
	(
	    cd child &&
	    git reset --hard origin/master &&
	    echo three > file && git commit -a -m three &&
	    test_must_fail git send-pack ../parent master 2>errs &&
	    grep -q "pack size limit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" != "$child_head"
'

test_expect_success 'repeated push failure proves that objects were not stored remotely' '
	(
	    cd child &&
	    test_must_fail git send-pack ../parent master 2>errs &&
	    grep -q "pack size limit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" != "$child_head"
'

test_expect_success 'increase receive.packSizeLimit' '
	(
	    cd parent &&
	    git config receive.packSizeLimit 1000000 &&
	    test_might_fail git receive-pack . <../pkt-flush >output &&
	    grep -q "limit-pack-size=1000000" output
	)
'

test_expect_success 'push is allowed when pack size is not exceeded' '
	(
	    cd child &&
	    git send-pack ../parent master 2>errs &&
	    test_must_fail grep -q "pack size limit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" = "$child_head"
'

test_expect_success 'deny pushing when receive.objectCountLimit is exceeded' '
	rewound_push_setup &&
	(
	    cd parent &&
	    git config receive.objectCountLimit 1
	) &&
	(
	    cd child &&
	    git reset --hard origin/master &&
	    echo three > file && git commit -a -m three &&
	    test_must_fail git send-pack ../parent master 2>errs &&
	    grep -q "receive\\.objectCountLimit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" != "$child_head"
'

test_expect_success 'repeated push failure proves that objects were not stored remotely' '
	(
	    cd child &&
	    test_must_fail git send-pack ../parent master 2>errs &&
	    grep -q "receive\\.objectCountLimit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" != "$child_head"
'

test_expect_success 'push is allowed when object limit is increased' '
	(
	    cd parent &&
	    git config receive.objectCountLimit 10
	) &&
	(
	    cd child &&
	    git send-pack ../parent master 2>errs &&
	    test_must_fail grep -q "receive\\.objectCountLimit" errs
	) &&
	parent_head=$(cd parent && git rev-parse --verify master) &&
	child_head=$(cd child && git rev-parse --verify master) &&
	test "$parent_head" = "$child_head"
'

test_done
