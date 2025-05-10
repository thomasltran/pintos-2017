# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-mutex) begin
(pthread-mutex) counter: 0
(pthread-mutex) counter: 1
(pthread-mutex) counter: 2
(pthread-mutex) counter: 3
(pthread-mutex) counter: 4
(pthread-mutex) end
pthread-mutex: exit(0)
EOF
pass;