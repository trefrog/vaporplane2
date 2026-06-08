
# Third-Party Notices

Vaporplane uses third-party open-source software.

This file documents third-party libraries used by Vaporplane itself. It does not
cover user-provided audio, sample packs, drum kits, loops, presets, or exported
project media.

This file is informational and is not a replacement for the full license texts
included with each dependency.

## SDL3

**Name:** Simple DirectMedia Layer 3  
**Project:** SDL3  
**Website:** https://www.libsdl.org/  
**Source:** https://github.com/libsdl-org/SDL  
**License:** zlib license  

Vaporplane uses SDL3 for windowing, input, rendering, audio device access, and
platform glue.

SDL3 license notice:

```text
Copyright (C) 1997-2026 Sam Lantinga

This software is provided 'as-is', without any express or implied warranty. In no
event will the authors be held liable for any damages arising from the use of
this software.

Permission is granted to anyone to use this software for any purpose, including
commercial applications, and to alter it and redistribute it freely, subject to
the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
   that you wrote the original software. If you use this software in a product,
   an acknowledgment in the product documentation would be appreciated but is not
   required.

2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

3. This notice may not be removed or altered from any source distribution.
```

For packaged binary builds, Vaporplane may bundle the SDL3 runtime library, such
as `SDL3.dll` on Windows or `libSDL3.0.dylib` on macOS.

## libsndfile

**Name:** libsndfile
**Website:** https://libsndfile.github.io/libsndfile/
**Source:** https://github.com/libsndfile/libsndfile
**License:** GNU Lesser General Public License, version 2.1 or version 3

Vaporplane can optionally use libsndfile for importing audio file formats beyond
WAV, such as FLAC, MP3, AIFF, and AIF, when libsndfile is available at build
time.

If Vaporplane is distributed as a binary linked with libsndfile, the distribution
must comply with the applicable LGPL terms for libsndfile and any codec
libraries that libsndfile uses in that build.

## SoundTouch

**Name:** SoundTouch Audio Processing Library
**Author:** Olli Parviainen
**Website:** [https://www.surina.net/soundtouch/](https://www.surina.net/soundtouch/)
**Source:** [https://codeberg.org/soundtouch/soundtouch](https://codeberg.org/soundtouch/soundtouch)
**License:** GNU Lesser General Public License, version 2.1
**Alternative licensing:** A commercial non-LGPL license may be available from
the SoundTouch project.

Vaporplane uses SoundTouch for offline audio rendering operations such as:

* tempo change / time-stretch while preserving pitch
* pitch shifting while preserving duration
* playback-rate style rendering where speed and pitch change together

SoundTouch project notice:

```text
The SoundTouch Library
Copyright © Olli Parviainen 2001-2024
Licensed under the GNU Lesser General Public License (LGPL) v2.1.
```

A copy of the LGPL v2.1 license should be included in source and binary
distributions that include SoundTouch, for example at:

```text
third_party/soundtouch/COPYING.TXT
docs/licenses/LGPL-2.1.txt
```

If Vaporplane is distributed as a binary linked with SoundTouch, the distribution
must comply with the LGPL v2.1 terms. This may include providing the SoundTouch
source code, any SoundTouch modifications, the LGPL license text, and any other
materials required to allow users to exercise their LGPL rights.

## Audio Assets And Sample Packs

Audio samples, loops, drum kits, demo media, and user-provided project files are
not covered by the library notices above.

Do not assume that a sample pack, WAV file, drum kit, exported loop, or demo song
is redistributable unless it has explicit license terms allowing redistribution.

Bundled audio assets should have their own source and license notes, either in a
separate asset notice file or beside the asset pack itself.

### Bundled CC0 Drum Kit Material

Vaporplane may bundle starter drum kit material distributed under Creative
Commons CC0. The source-provided CC0 user license is included as:

```text
CC0_License_For_Users.pdf
```

The bundled starter drum material currently includes curated kit mappings and
samples from these folders:

```text
Burial Kicks
Cowbells - Dynamite Cowbel VSTi
Cymbal Crashes - SignatureSounds.org
Forest Kit - Stick And Twig Snaps
Fred Again Styled Drum Kit
Kick Drums - Multiple Genres
Trap 808 SignatureSounds.Org
vhs-drumkit CC0
```

The bundled JSON kit mappings are Vaporplane project data. The underlying audio
samples remain third-party audio assets distributed with their source-provided
CC0 notice.

Do not assume that future or user-provided drum packs are redistributable unless
they include explicit license terms allowing redistribution.

### Bundled Demo Sample Material

Packaged Vaporplane builds may include this starter demo sample:

```text
[demo] Makaih Beats - Vibration.wav
```

**Creator:** Makaih Beats  
**License:** Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International  
**License URL:** https://creativecommons.org/licenses/by-nc-sa/4.0/

This sample is not CC0. The Creative Commons BY-NC-SA 4.0 license requires
attribution, allows sharing and adaptation under the license terms, does not
permit commercial use, and requires adaptations to be shared under the same
license.

The adjacent JSON file contains Vaporplane tempo metadata plus a copy of the
license identifier and license URL for this demo sample.
