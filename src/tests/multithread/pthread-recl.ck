# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-recl) begin
(pthread-recl) count 0
(pthread-recl) count 1
(pthread-recl) count 2
(pthread-recl) count 3
(pthread-recl) count 4
(pthread-recl) count 5
(pthread-recl) count 6
(pthread-recl) count 7
(pthread-recl) count 8
(pthread-recl) count 9
(pthread-recl) count 10
(pthread-recl) count 11
(pthread-recl) count 12
(pthread-recl) count 13
(pthread-recl) count 14
(pthread-recl) count 15
(pthread-recl) count 16
(pthread-recl) count 17
(pthread-recl) count 18
(pthread-recl) count 19
(pthread-recl) count 20
(pthread-recl) count 21
(pthread-recl) count 22
(pthread-recl) count 23
(pthread-recl) count 24
(pthread-recl) count 25
(pthread-recl) count 26
(pthread-recl) count 27
(pthread-recl) count 28
(pthread-recl) count 29
(pthread-recl) count 30
(pthread-recl) count 31
(pthread-recl) count 32
(pthread-recl) count 33
(pthread-recl) count 34
(pthread-recl) count 35
(pthread-recl) count 36
(pthread-recl) count 37
(pthread-recl) count 38
(pthread-recl) count 39
(pthread-recl) count 40
(pthread-recl) count 41
(pthread-recl) count 42
(pthread-recl) count 43
(pthread-recl) count 44
(pthread-recl) count 45
(pthread-recl) count 46
(pthread-recl) count 47
(pthread-recl) count 48
(pthread-recl) count 49
(pthread-recl) end
pthread-recl: exit(0)
EOF
pass;