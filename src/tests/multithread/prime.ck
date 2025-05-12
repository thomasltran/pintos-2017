# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-create) begin
(pthread-create) end
pthread-create: exit(0)
EOF
pass;