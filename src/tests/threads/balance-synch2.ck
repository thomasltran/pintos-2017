# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::threads::balance;

our ($test);
my (@output) = read_text_file("$test.output");

common_checks ("run", @output);

my (@kernel_ticks) = ();
my (@idle_ticks) = ();
foreach (@output) {
	my ($a, $b, $c) = /CPU(\d+): (\d+) idle ticks, (\d+) kernel ticks(.*)/ or next;
	push (@idle_ticks, $b);
	push (@kernel_ticks, $c);
}

load_balance_check (@kernel_ticks);
idle_check (\@idle_ticks, \@kernel_ticks);

check_expected ([<<'EOF']);
(balance-synch2) begin
(balance-synch2) This test is very unforgiving of race conditions.
(balance-synch2) It will not run fast!.
(balance-synch2) Running 5000 tests.
(balance-synch2) Finished test 0
(balance-synch2) Finished test 100
(balance-synch2) Finished test 200
(balance-synch2) Finished test 300
(balance-synch2) Finished test 400
(balance-synch2) Finished test 500
(balance-synch2) Finished test 600
(balance-synch2) Finished test 700
(balance-synch2) Finished test 800
(balance-synch2) Finished test 900
(balance-synch2) Finished test 1000
(balance-synch2) Finished test 1100
(balance-synch2) Finished test 1200
(balance-synch2) Finished test 1300
(balance-synch2) Finished test 1400
(balance-synch2) Finished test 1500
(balance-synch2) Finished test 1600
(balance-synch2) Finished test 1700
(balance-synch2) Finished test 1800
(balance-synch2) Finished test 1900
(balance-synch2) Finished test 2000
(balance-synch2) Finished test 2100
(balance-synch2) Finished test 2200
(balance-synch2) Finished test 2300
(balance-synch2) Finished test 2400
(balance-synch2) Finished test 2500
(balance-synch2) Finished test 2600
(balance-synch2) Finished test 2700
(balance-synch2) Finished test 2800
(balance-synch2) Finished test 2900
(balance-synch2) Finished test 3000
(balance-synch2) Finished test 3100
(balance-synch2) Finished test 3200
(balance-synch2) Finished test 3300
(balance-synch2) Finished test 3400
(balance-synch2) Finished test 3500
(balance-synch2) Finished test 3600
(balance-synch2) Finished test 3700
(balance-synch2) Finished test 3800
(balance-synch2) Finished test 3900
(balance-synch2) Finished test 4000
(balance-synch2) Finished test 4100
(balance-synch2) Finished test 4200
(balance-synch2) Finished test 4300
(balance-synch2) Finished test 4400
(balance-synch2) Finished test 4500
(balance-synch2) Finished test 4600
(balance-synch2) Finished test 4700
(balance-synch2) Finished test 4800
(balance-synch2) Finished test 4900
(balance-synch2) PASS
(balance-synch2) end
EOF
pass;
