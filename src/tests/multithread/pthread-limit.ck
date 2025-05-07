# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-limit) begin
(pthread-limit) thread limit reached at 32
(pthread-limit) result 0
(pthread-limit) result 1
(pthread-limit) result 2
(pthread-limit) result 3
(pthread-limit) result 4
(pthread-limit) result 5
(pthread-limit) result 6
(pthread-limit) result 7
(pthread-limit) result 8
(pthread-limit) result 9
(pthread-limit) result 10
(pthread-limit) result 11
(pthread-limit) result 12
(pthread-limit) result 13
(pthread-limit) result 14
(pthread-limit) result 15
(pthread-limit) result 16
(pthread-limit) result 17
(pthread-limit) result 18
(pthread-limit) result 19
(pthread-limit) result 20
(pthread-limit) result 21
(pthread-limit) result 22
(pthread-limit) result 23
(pthread-limit) result 24
(pthread-limit) result 25
(pthread-limit) result 26
(pthread-limit) result 27
(pthread-limit) result 28
(pthread-limit) result 29
(pthread-limit) result 30
(pthread-limit) result 31
(pthread-limit) invalid join at 32
(pthread-limit) end
pthread-limit: exit(0)
EOF
pass;