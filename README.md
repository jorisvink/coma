About
-----

Coma is a minimalistic X11 Window Manager.

It makes my life easier when hacking on my laptop as by default
I will get my 2 frames containing 80 column terminals with a nice
gap and layout.

Read the included manual page for more information.

License
-------
Coma is licensed under the ISC license.

Building
--------

Coma should build fine on MacOS, Linux and OpenBSD.

OpenBSD:
```
$ make
$ doas make install
```

MacOS:
```
$ export PKG_CONFIG_PATH=/opt/X11/share/pkgconfig:/opt/X11/lib/pkgconfig
$ make
$ sudo make install
```

Linux (requires libbsd):
```
$ env CFLAGS=-D_GNU_SOURCE LDFLAGS=-lbsd make
$ sudo make install
```

Shell setup
-----------

Coma ties in directly with your xterms and running shell. With the help
of your shell you can update coma on the hostname, pwd and commands
running in the clients.

Here's a snippet from my .zshrc that can get you started:

```
precmd() {
        print -Pn "\e]0;%M;%d;zsh\a"
}

preexec() {
        cmd=`echo $1 | cut -f1 -d' '`
        if [ "$cmd" = "vi" ] || [ "$cmd" = $EDITOR ]; then
                cmd=`echo $1 | cut -f2 -d' '`
        fi

        print -Pn "\e]0;%M;%d;$cmd\a"
}
```

Coma expects the information from the shell in the following format:

hostname;directory;running command

If your environment is configured like the above Coma will be able to
execute commands on remote hosts transparently via prefix-e as it will
auto detect what host you are currently on and execute the coma-remote
script to connect to it before executing the command given.

Key bindings
------------
All key bindings are changable via the config file (see coma.1).

C-t = prefix

prefix-c     = new xterm

prefix-e     = open command execution input

prefix-Space = toggle popup area

prefix-colon = coma internal command prompt

prefix-p     = previous window

prefix-n     = next window

prefix-r     = restart Coma

prefix-h     = move to frame to the left of current frame

prefix-l     = move to frame to the right of current frame

prefix-k     = kill client in current frame

prefix-i     = move the active client to the left

prefix-o     = move the active client to the right

prefix-s     = split frame

prefix-q     = client quick selection list

prefix-m     = merge frame back together

prefix-f     = toggle focus between split frames

prefix-z     = zoom/unzoom current frame to cover all frames

Screenshots
-----------

![coma](https://coma.one/wm/screenshots/coma.png?raw=true)
![coma2](https://coma.one/wm/screenshots/coma-split.png?raw=true)
