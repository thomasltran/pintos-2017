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

check_expected ([<<'EOF']);
(balance-synch2) begin
(balance-synch2) This test is very unforgiving of race conditions.
(balance-synch2) It will not run fast!.
(balance-synch2) Running 10000 tests.
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
(balance-synch2) Finished test 5000
(balance-synch2) Finished test 5100
(balance-synch2) Finished test 5200
(balance-synch2) Finished test 5300
(balance-synch2) Finished test 5400
(balance-synch2) Finished test 5500
(balance-synch2) Finished test 5600
(balance-synch2) Finished test 5700
(balance-synch2) Finished test 5800
(balance-synch2) Finished test 5900
(balance-synch2) Finished test 6000
(balance-synch2) Finished test 6100
(balance-synch2) Finished test 6200
(balance-synch2) Finished test 6300
(balance-synch2) Finished test 6400
(balance-synch2) Finished test 6500
(balance-synch2) Finished test 6600
(balance-synch2) Finished test 6700
(balance-synch2) Finished test 6800
(balance-synch2) Finished test 6900
(balance-synch2) Finished test 7000
(balance-synch2) Finished test 7100
(balance-synch2) Finished test 7200
(balance-synch2) Finished test 7300
(balance-synch2) Finished test 7400
(balance-synch2) Finished test 7500
(balance-synch2) Finished test 7600
(balance-synch2) Finished test 7700
(balance-synch2) Finished test 7800
(balance-synch2) Finished test 7900
(balance-synch2) Finished test 8000
(balance-synch2) Finished test 8100
(balance-synch2) Finished test 8200
(balance-synch2) Finished test 8300
(balance-synch2) Finished test 8400
(balance-synch2) Finished test 8500
(balance-synch2) Finished test 8600
(balance-synch2) Finished test 8700
(balance-synch2) Finished test 8800
(balance-synch2) Finished test 8900
(balance-synch2) Finished test 9000
(balance-synch2) Finished test 9100
(balance-synch2) Finished test 9200
(balance-synch2) Finished test 9300
(balance-synch2) Finished test 9400
(balance-synch2) Finished test 9500
(balance-synch2) Finished test 9600
(balance-synch2) Finished test 9700
(balance-synch2) Finished test 9800
(balance-synch2) Finished test 9900
(balance-synch2) PASS
(balance-synch2) end
EOF
pass;
