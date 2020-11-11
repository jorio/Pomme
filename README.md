# Pomme: a cross-platform implementation of the Macintosh Toolbox C API

**Pomme** is a partial, cross-platform implementation of the Macintosh Toolbox C API. It is designed to ease the porting of 90's games written for MacOS 7-9 to modern operating systems. You can think of it as a cross-platform reimagining of Apple's own [Carbon](https://en.wikipedia.org/wiki/Carbon_(API)), albeit at a much reduced scope.

The goal isn't to achieve 100% source compatibility with old Mac C programs, but rather, to make it a bit easier to port them. I only intend to implement the bare minimum functionality required to keep a reasonable level of source code compatibility with the games I'm interested in porting.

This project originated in 2020 to aid in updating Pangea Software's [Nanosaur](https://github.com/jorio/Nanosaur) to run on modern operating systems.

## Credits

- © 2020 Iliyas Jorio (unless other attribution noted in source modules)
- Portions derived from [cmixer](https://github.com/rxi/cmixer), © 2017 rxi
- Portions derived from ffmpeg, © 2001-2003 The FFmpeg Project
- Portions copied from [gulrak/filesystem](https://github.com/gulrak/filesystem), © 2018 Steffen Schümann
- Portions copied from [tcbrindle/span](https://github.com/tcbrindle/span), © 2018 Tristan Brindle
