# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(tls) begin
hi0
hi1
hi2
hi3
hi4
hi5
hi6
hi7
hi8
hi9
hi10
hi11
hi12
hi13
hi14
hi15
tls NULL in main
(tls) count1 16
(tls) count2 32
(tls) end
tls: exit(0)
EOF
pass;