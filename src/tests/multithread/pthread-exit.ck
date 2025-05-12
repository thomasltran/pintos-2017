# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-exit) begin
(pthread-exit) pthread create 42
(pthread-exit) result 21
pthread-exit: exit(0)
EOF
pass;