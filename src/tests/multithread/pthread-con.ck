# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-con) begin
(pthread-con) share_var: 0
(pthread-con) share_var: 1
(pthread-con) share_var: 2
(pthread-con) share_var: 3
(pthread-con) share_var: 4
(pthread-con) share_var: 5
(pthread-con) share_var: 6
(pthread-con) share_var: 7
(pthread-con) share_var: 8
(pthread-con) share_var: 9
(pthread-con) share_var: 10
(pthread-con) share_var: 11
(pthread-con) share_var: 12
(pthread-con) share_var: 13
(pthread-con) share_var: 14
(pthread-con) share_var: 15
(pthread-con) share_var: 16
(pthread-con) share_var: 17
(pthread-con) share_var: 18
(pthread-con) share_var: 19
(pthread-con) share_var: 20
(pthread-con) share_var: 21
(pthread-con) share_var: 22
(pthread-con) share_var: 23
(pthread-con) share_var: 24
(pthread-con) share_var: 25
(pthread-con) share_var: 26
(pthread-con) share_var: 27
(pthread-con) share_var: 28
(pthread-con) share_var: 29
(pthread-con) share_var: 30
(pthread-con) share_var: 31
(pthread-con) end
pthread-con: exit(0)
EOF
pass;