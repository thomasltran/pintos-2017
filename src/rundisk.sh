qemu-system-x86_64 \
    -drive file=usbdisk.img,format=raw \
    -serial file:serial.log \
    -display curses \
    -m 1024 \
    -smp cores=1,threads=1,sockets=32 \
    -enable-kvm

#
# cheatsheet:
#
# -serial null      - lead serial console to a sink, but make VM think it has a serial console
# -serial file:<file> - redirect serial console output to file
# -display curses   - emulate VGA console via curses 
#
# alternatively
# -serial stdio     - link serial console to own stdio
# -nographic        - turn off VGA console
#
# otherwise, Pintos will output to both
#
# Must use multiple sockets to get Pintos to recognize
# multiple CPUs
#
#   -smp cores=1,threads=1,sockets=4
