# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-mult) begin
(pthread-mult) result 0
(pthread-mult) result 1
(pthread-mult) result 2
(pthread-mult) result 3
(pthread-mult) result 4
(pthread-mult) result 5
(pthread-mult) result 6
(pthread-mult) result 7
(pthread-mult) result 8
(pthread-mult) result 9
(pthread-mult) result 10
(pthread-mult) result 11
(pthread-mult) result 12
(pthread-mult) result 13
(pthread-mult) result 14
(pthread-mult) result 15
(pthread-mult) result 16
(pthread-mult) result 17
(pthread-mult) result 18
(pthread-mult) result 19
(pthread-mult) result 20
(pthread-mult) result 21
(pthread-mult) result 22
(pthread-mult) result 23
(pthread-mult) result 24
(pthread-mult) result 25
(pthread-mult) result 26
(pthread-mult) result 27
(pthread-mult) result 28
(pthread-mult) result 29
(pthread-mult) result 30
(pthread-mult) result 31
(pthread-mult) end
pthread-mult: exit(0)
EOF
pass;