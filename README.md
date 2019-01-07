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

You can also specify the width of the frames yourself using the -w
option. For example running with an 8x16 font and wanting an 80 column
terminal:

```
$ coma -w 644
```

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

prefix-c     = new xterm

prefix-Space = toggle popup area

prefix-p     = previous window

prefix-n     = next window

prefix-r     = restart Coma

prefix-h     = move to frame to the left of current frame

prefix-Left  = move to frame to the left of current frame

prefix-l     = move to frame to the right of current frame

prefix-Right = move to frame to the right of current frame

prefix-k     = kill client in current frame

prefix-i     = move active client to the frame on the left

prefix-o     = move active client to the frame on the right

prefix-s     = split frame

prefix-m     = merge frame back together

prefix-Up    = move to upper part in a split frame

prefix-Down  = move to lower part in a split frame

Screenshots
-----------

Default frame:
![Default](/screenshots/default.png?raw=true)

Large frame:
![Large](/screenshots/large.png?raw=true)
