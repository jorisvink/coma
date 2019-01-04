About
-----

Coma is a minimalistic X11 Window Manager.

It makes my life easier when hacking on my laptop as it will keep
my windows in the exact place I want them.

It has 2 framing modes: default, large. Only one can be active at
a given time and is specified at startup time using -f.

Default means it will split up your screen into columns just large
enough to fit 80 column xterms.

Large means it will calculate the frames based on 161 column xterms
(so I can use tmux and split it to get 80 columns in each pane).

License
-------
Coma is licensed under the ISC license.

Building
--------

Coma should build fine on MacOS, Linux and OpenBSD.

OpenBSD:
```
$ gmake
```

MacOS:
```
$ export PKG_CONFIG_PATH=/opt/X11/share/pkgconfig:/opt/X11/lib/pkgconfig
$ make
```

Linux:
```
$ env CFLAGS=-D_GNU_SOURCE make
```

Key bindings
------------
Key bindings cannot be changed unless you hack the code:

C-t = prefix

prefix-C     = new xterm

prefix-P     = previous window

prefix-N     = next window

prefix-R     = restart Coma

prefix-H     = move to frame to the left of current frame

prefix-Left  = move to frame to the left of current frame

prefix-L     = move to frame to the right of current frame

prefix-Right = move to frame to the right of current frame

prefix-K     = kill client in current frame
