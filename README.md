
README for pintos-2017
======================

This repository contains the Virginia Tech version of Pintos, which spawned off Stanford's
in 2009.  Added features include support for PCI, USB, and as of 2017, multiple processors.

Additional Notes
================

This version by default uses the Qemu emulator (`--qemu`), but also be used
with KVM (`--kvm`).

Changes for Bochs 2.7.  
This version should work with Bochs, with the following caveats.
Bochs does not support the GDB stub and SMP mode simultaneously.
Therefore, I built two versions of bochs:

```
./configure \
    --with-x --with-x11 --with-term --with-nogui \
    --enable-smp \
    --enable-x86-64 \
    --enable-e1000 \
    --enable-cpu-level=6 \
    --prefix=/web/courses/cs4284/pintostools
```
The version is renamed `bochs-smp`

```
./configure \
    --with-x --with-x11 --with-term --with-nogui \
    --enable-gdb-stub \
    --enable-x86-64 \
    --enable-e1000 \
    --enable-cpu-level=6 \
    --prefix=/web/courses/cs4284/pintostools
```
The version is named `bochs`

I also changed `pintos` to use different names for Bochs's configuration file.
This allows the use of the `-j` flag when running make grade.
