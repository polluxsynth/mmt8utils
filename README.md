mmt8utils
=========

These are a collection of utilities for the Alesis MMT-8. They include
a librarian (mmt8.c), a simple dump utility to dump pattern and song names
(mmtread.c), and two utilities to convert pattern between the MMT-8
internal pattern format and readable text (mmtdecode.c/mmtencode.c).

See mmt8-utils.txt for detailed usage information.

This were written a long time ago in Turbo C for a DOS PC where the
serial port has been modified to run at a master clock rate of 1 MHz, thus
making the 31.25 kbps MIDI clock available.

Undefining MIDI in the files removes support for the MIDI interface; it is
still possible to read and write the data to ordinary sysex files.

Note however that the code will require more work than that if it is to be
compiled with a modern compiler to run on a modern computer. Since it was
originally written in Turbo C, which is a 16-bit C implementation, standard
ints are 16 bits, etc. There are also a couple of non-standard library
functions used, most notable getche() (getchar with echo) and coreleft() (which
returns the amount of memory available). In the latter case, the modest memory
requirements of the applications on a modern PC make this function redundant,
and getche can be replaced with a getchar-putchar combo. The 16 bit int size
requires some careful work on the code though, as it is used among other
things to map a C struct onto the MMT-8 internal data structures.

I might have a reason to modify these applications to run on a modern 32- or
64-bit machine in the future, however, for now I'm just leaving them here
in case anyone else wants to have a go.
