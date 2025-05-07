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
(pthread-mult) end
pthread-mult: exit(0)
EOF
pass;