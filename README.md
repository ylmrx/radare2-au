radare2-au
==========

This module for r2 enables the `au` command to initialize the
audio device, create waves, apply filters and interactively
play sounds from the custom visual mode.

Slides: [RidingTheWave.pdf](https://github.com/radareorg/r2con2018/raw/master/talks/riding-the-wave/r2con2018-RidingTheWave.pdf)

--pancake

## Build under debian

There's a bug with the gcc build you'll find in debian-based system (Ubuntu, Debian, Linuxmint, ...)

If upon initialization (`aui`), radare2 crashes with :

```
[0x00000000]> aui
[au] 22050 Hz 16 bits 1 channels
r2: symbol lookup error: /home/fuzz/.local/share/radare2/plugins/core_au.so: undefined symbol: ao_initialize
```

You can build using clang compiler:
```bash
sudo apt install clang-6.0
CC=clang-6.0 r2pm -ci r2au
```

Or build it normally and use:
```bash
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libao.so r2 -
```
