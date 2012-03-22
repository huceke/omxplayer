#!/bin/sh

insmod /lib/modules/3.1.10+/kernel/arch/arm/oprofile/oprofile.ko  timer=1
opcontrol --init
opcontrol --vmlinux=/boot/vmlinux
opcontrol --start 

./omxplayer.bin -s -o hdmi /media/net/1080p/Gone\ in\ Sixty\ Seconds\ 2000.mkv

opcontrol --dump
opreport -l
