# Pomme

## A cross-platform implementation of the Macintosh Toolbox C API

**Pomme** is a partial, cross-platform implementation of the Macintosh Toolbox C API. It is designed to ease the porting of 90's games written for MacOS 7-9 to modern operating systems. You can think of it as a cross-platform reimagining of Apple's own [Carbon](https://en.wikipedia.org/wiki/Carbon_(API)), albeit at a much reduced scope.

The goal isn't to achieve 100% source compatibility with old Mac C programs, but rather, to make it a bit easier to port them. I only intend to implement the bare minimum functionality required to keep a reasonable level of source code compatibility with the games I'm interested in porting.

## Games ported with Pomme

- [Bugdom](https://github.com/jorio/bugdom)
- [Mighty Mike](https://github.com/jorio/mightymike)
- [Nanosaur](https://github.com/jorio/nanosaur)
- [Otto Matic](https://github.com/jorio/ottomatic)

## License

Please see [LICENSE.md](LICENSE.md)

## Features

Files and resources:
- Access files on the host's filesystem with `FSSpec` structures.
- Read/write data forks.
- Access resources inside AppleDouble files (transparently presented as resource forks to application code).
  
QuickDraw 2D:
- Load images from QuickDraw 2D `PICT` resources and files.
- Manipulate ports.
- Basic draw calls: lines, rects, bitmap text, CopyBits.
  
Sound Manager:
- Load audio from AIFF & AIFF-C files and `snd ` resources.
- Supported audio codecs: raw PCM, `ima4`, `MAC3`, `ulaw`, `alaw`.
- Use SndChannels to output audio thanks to the built-in software mixer (requires SDL).

QuickDraw 3D-ish:
- Basic QD3D geometry structures and math routines. 
- Load 3D model data from 3DMF files.
- Please note: Accurate source compatibility with QD3D is out of scope for Pomme. For a faithful implementation of QD3D, look at [Quesa](https://github.com/jwwalker/quesa).

Misc:
- Memory management routines.
- Limited playback of QuickTime `moov` files (only Cinepak is supported).
- Byte-swapping routines inspired from [Python's `struct` format strings](https://docs.python.org/3/library/struct.html#struct-format-strings) to convert big-endian structs to little-endian.
- Basic keyboard/mouse input via SDL.

