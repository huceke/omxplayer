OMXPlayer
=========

OMXPlayer is a commandline OMX player for the Raspberry Pi. It was developed as
a testbed for the XBMC Raspberry PI implementation and is quite handy to use
standalone. 

Downloading OMXPlayer
---------------------

    git clone git://github.com/huceke/omxplayer.git

Compiling OMXPlayer
-------------------

### Cross Compiling

You need the content of your sdcard somewhere mounted or copied. There might be
development headers to install on the running Pi system for the crosscompiling.

Edit Makefile.include and change the settings according your locations.

### Compiling on the Pi

You can also compile it on the PI the native way ;)

Running OMXPlayer
-----------------

    make ffmpeg
    make
    make dist

Installing OMXPlayer
--------------------

Copy over `omxplayer-dist/*` to the Pi `/`.

Using OMXPlayer
---------------

    Usage: omxplayer [OPTIONS] [FILE]
    Options :
             -h / --help                    print this help
             -a / --alang language          audio language        : e.g. ger
             -n / --aidx  index             audio stream index    : e.g. 1
             -o / --adev  device            audio out device      : e.g. hdmi/local
             -i / --info                    dump stream format and exit
             -s / --stats                   pts and buffer stats
             -p / --passthrough             audio passthrough
             -d / --deinterlace             deinterlacing
             -w / --hw                      hw audio decoding
             -3 / --3d                      switch tv into 3d mode
             -y / --hdmiclocksync           adjust display refresh rate to match video
             -t / --sid index               show subtitle with index

For example:

    ./omxplayer -p -o hdmi test.mkv

