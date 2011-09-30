#!/bin/sh

test_description='calculate and cache commit generations'
. ./test-lib.sh

test_expect_success 'setup history' '
	test_commit one &&
	test_commit two &&
	test_commit three &&
	test_commit four &&
	git checkout -b other two &&
	test_commit five &&
	git checkout master &&
	git merge other &&
	test_commit six
'

cat >expect <<'EOF'
5 six
4 Merge branch 'other'
2 five
3 four
2 three
1 two
0 one
EOF
test_expect_success 'check commit generations' '
	git log --format="%G %s" >actual &&
	test_cmp expect actual
'

test_expect_success 'cache file was created' '
	test_path_is_file .git/cache/generations
'

test_expect_success 'cached values are the same' '
	git log --format="%G %s" >actual &&
	test_cmp expect actual
'

cat >expect-grafted <<'EOF'
1 six
0 Merge branch 'other'
EOF
test_expect_success 'adding grafts invalidates generation cache' '
	git rev-parse six^ >.git/info/grafts &&
	git log --format="%G %s" >actual &&
	test_cmp expect-grafted actual
'

test_expect_success 'removing graft invalidates cache' '
	rm .git/info/grafts &&
	git log --format="%G %s" >actual &&
	test_cmp expect actual
'

test_expect_success 'setup replace ref' '
	H=$(git rev-parse six^) &&
	R=$(git cat-file commit $H |
	    sed /^parent/d |
	    git hash-object -t commit --stdin -w) &&
	git update-ref refs/replace/$H $R
'

test_expect_success 'adding replace refs invalidates generation cache' '
	git log --format="%G %s" >actual &&
	test_cmp expect-grafted actual
'

test_expect_success 'cache respects replace-object settings' '
	git --no-replace-objects log --format="%G %s" >actual &&
	test_cmp expect actual &&
	git log --format="%G %s" >actual &&
	test_cmp expect-grafted actual
'

test_done
