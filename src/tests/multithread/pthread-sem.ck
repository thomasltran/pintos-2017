# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pthread-sem) begin
(pthread-sem) sem_init pthread_sem
(pthread-sem) sem_init main_sem
(pthread-sem) sem_post pthread_sem: 0
(pthread-sem) sem_post pthread_sem: 1
(pthread-sem) sem_post pthread_sem: 2
(pthread-sem) sem_post pthread_sem: 3
(pthread-sem) sem_post pthread_sem: 4
(pthread-sem) sem_post pthread_sem: 5
(pthread-sem) sem_post pthread_sem: 6
(pthread-sem) sem_post pthread_sem: 7
(pthread-sem) sem_post pthread_sem: 8
(pthread-sem) sem_post pthread_sem: 9
(pthread-sem) sem_post pthread_sem: 10
(pthread-sem) sem_post pthread_sem: 11
(pthread-sem) sem_post pthread_sem: 12
(pthread-sem) sem_post pthread_sem: 13
(pthread-sem) sem_post pthread_sem: 14
(pthread-sem) sem_post pthread_sem: 15
(pthread-sem) sem_post pthread_sem: 16
(pthread-sem) sem_post pthread_sem: 17
(pthread-sem) sem_post pthread_sem: 18
(pthread-sem) sem_post pthread_sem: 19
(pthread-sem) sem_post pthread_sem: 20
(pthread-sem) sem_post pthread_sem: 21
(pthread-sem) sem_post pthread_sem: 22
(pthread-sem) sem_post pthread_sem: 23
(pthread-sem) sem_post pthread_sem: 24
(pthread-sem) sem_post pthread_sem: 25
(pthread-sem) sem_post pthread_sem: 26
(pthread-sem) sem_post pthread_sem: 27
(pthread-sem) sem_post pthread_sem: 28
(pthread-sem) sem_post pthread_sem: 29
(pthread-sem) sem_post pthread_sem: 30
(pthread-sem) sem_post pthread_sem: 31
(pthread-sem) sem_down main_sem: 0
(pthread-sem) sem_down main_sem: 1
(pthread-sem) sem_down main_sem: 2
(pthread-sem) sem_down main_sem: 3
(pthread-sem) sem_down main_sem: 4
(pthread-sem) sem_down main_sem: 5
(pthread-sem) sem_down main_sem: 6
(pthread-sem) sem_down main_sem: 7
(pthread-sem) sem_down main_sem: 8
(pthread-sem) sem_down main_sem: 9
(pthread-sem) sem_down main_sem: 10
(pthread-sem) sem_down main_sem: 11
(pthread-sem) sem_down main_sem: 12
(pthread-sem) sem_down main_sem: 13
(pthread-sem) sem_down main_sem: 14
(pthread-sem) sem_down main_sem: 15
(pthread-sem) sem_down main_sem: 16
(pthread-sem) sem_down main_sem: 17
(pthread-sem) sem_down main_sem: 18
(pthread-sem) sem_down main_sem: 19
(pthread-sem) sem_down main_sem: 20
(pthread-sem) sem_down main_sem: 21
(pthread-sem) sem_down main_sem: 22
(pthread-sem) sem_down main_sem: 23
(pthread-sem) sem_down main_sem: 24
(pthread-sem) sem_down main_sem: 25
(pthread-sem) sem_down main_sem: 26
(pthread-sem) sem_down main_sem: 27
(pthread-sem) sem_down main_sem: 28
(pthread-sem) sem_down main_sem: 29
(pthread-sem) sem_down main_sem: 30
(pthread-sem) sem_down main_sem: 31
(pthread-sem) sem_destroy pthread_sem
(pthread-sem) sem_destroy main_sem
(pthread-sem) end
pthread-sem: exit(0)
EOF
pass;