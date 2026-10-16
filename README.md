About
-----

Coma is a minimalistic, keyboard driven, tiling terminal.

It started as an X11 window manager many years ago but due
to the burden of maintaining something like this across many
different screen configurations and monitors I decided to just
convert it to an SDL2 backed program in 2026.

This way it runs everywhere without specific hard requirements.
It currently is tested under MacOS, Linux with X11 and Wayland.

I hacked up st's terminal code and used that as the backing
terminal emulation.

Read the included manual page for more information.

License
-------
Coma is licensed under the ISC license.

Building
--------

Coma should build fine on MacOS and Linux.

It requires SDL2, SDL2_ttf and fontconfig.

MacOS:
```
$ make
$ sudo make install
```

Linux:
```
$ env CFLAGS=-D_GNU_SOURCE make
$ sudo make install
```

Shell setup
-----------

Coma ties in directly with your terminals and running shell. With the help
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

This information is then presented in the frame status bar.

Key bindings
------------
All key bindings are changable via the config file (see coma.1).

C-t = prefix

prefix-prefix = send the prefix key to the terminal

prefix-c     = new terminal

prefix-v     = new terminal sandboxed with no internet access

prefix-colon = coma internal command prompt

prefix-p     = previous window

prefix-n     = next window

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
