# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-mutex) begin
(pthread-mutex) counter: 3200000
(pthread-mutex) end
pthread-mutex: exit(0)
EOF
pass;