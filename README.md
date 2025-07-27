# amigos

A super tiny, ultra portable, single file, standalone Gopher server written in
ANSI C. Also known as **a** **mi**cro **G**opher **s**erver.

This server implementation strictly follows [RFC 1436](https://www.rfc-editor.org/rfc/rfc1436),
with the exception of the `i` type that's commonly used in modern Gopher
implementations.

## Compiling and Configuration

Configuration of this software is done via a `config.h` file at the root of the
repository, thus requiring recompiling for every configuration change. This is
meant to simplify the source code while maintaining flexibility. In order to
compile the application the following should be enough for a UNIX system:

```sh
gcc -ansi -std=c89 -Wall -pedantic amigos.c -o amigos
```

If you're compiling this under Linux using GCC, you should set the C standard
parameter to `-std=gnu89`, otherwise GCC will complain about `snprintf` not
being defined and a couple of other things.

To learn more about which options are available for configuration, consult the
`amigos.c` section commented as `/* Some common definitions and defaults. */`
until the inclusion of the `config.h` file. All definitions there are
overwriteable and should be pretty self-explanatory.

After the server has been compiled it's simply a matter of running it and
specifying a `docroot` as the first argument, equivalent to the `htdocs` folder
on Apache, where the root of your gopherhole will reside.

### Windows

This project is developed in such a way that it's able to be compiled under
Microsoft Visual C++ 6, giving it the maximum amount of compatibility, and
enabling the generation of executables that work from Windows 95 all the way to
the latest Windows 11 version.

A workspace for Visual C++ 6 can be found under the `win32/` directory, although
before trying to compile this project you **MUST** install the
[Windows Server 2003 PSDK (February 2003)](https://archive.org/details/MSDN_-_No_0004.7_-_May_2003).
This SDK, specifically the Internet Explorer SDK, contains updated definitions
of sockets-related functions and types.

### Detecting Memory Leaks

Since this is a server that's supposed to be running for many months unattended,
it's important to ensure that the code base is free of memory leaks on any
platforms. If you're contributing to this project you **MUST** test if your code
is producing any memory leaks.

If you're developing under Windows, compiling a `Debug` target should already
give you some information on memory leaks in the Debug Console whenever the
execution of the applications ends. If you're developing under a UNIX variant,
`valgrind` is required for checking this. The following commands should be used
for this operation:

```sh
gcc -ansi -std=gnu89 -Wall -pedantic -g3 -DDEBUG -DMEMCHECK amigos.c -o amigos
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
    --track-origins=yes ./amigos godocs/
```

## gophermap

This server implementation supports the usage of `gophermap` files inside
directories, which will be read and rendered automatically depending on the
selector sent to the server. The syntax is close to what
[gophernicus](https://github.com/gophernicus/gophernicus/blob/master/README.gophermap)
uses, although there are some slight differences, mainly to simplify it, and may
catch you off guard, so watch out for them if you're used to that
implementation.

Any line that doesn't contain any `TAB` characters will automatically be
rendered as an info (`i` type) entry.

If a line has at least one (1) `TAB` character it will be interpreted as an
entry and requires at least the entry's `type`, `name`, and `selector`, with the
`hostname` and `port` fields being optional, as per the following description:

    <type><name>TAB<selector>[TAB[hostname]][TAB[port]]

If a selector starts with a `/` (forward slash) character, it will be sent to
the client as is, meaning the selector points to an absolute reference. All
other selectors will have their content prepended by the current selector sent
to the server, meaning they will be relative to the current selector.

If a single dot (`.`) is found on a line by itself, the processing of the
gophermap will stop and no further lines will be rendered.

If a single asterisk (`*`) is found on a line by itself, a listing of the
current directory is rendered (without the inclusion of the gophermap file), and
regular rendering resumes on the next line.

## License

This library is free software; you may redistribute and/or modify it under the
terms of the [Mozilla Public License 2.0](https://www.mozilla.org/en-US/MPL/2.0/).
