AnkINK universal Kindle release (ARMEL + ARMHF)
================================================

USB installation
----------------

This release archive is laid out like the Kindle USB drive. Extract it on your
computer, then copy the archive's CONTENTS (ankink, extensions, and documents)
to the top level of the mounted Kindle drive. Do not copy the enclosing dist or
release directory.

After copying, the Kindle drive must contain:

    ankink/ankink.sh
    extensions/AnkINK/config.xml
    documents/AnkINK.sh

Safely eject the Kindle. AnkINK then appears as "AnkINK" in the Kindle Library
and as "Launch AnkINK" in KUAL. The Library entry requires PEKI, the same
launcher support used by a Library-installed KUAL.sh. KUAL remains an optional
second way to launch AnkINK.

On first launch, sign into AnkiWeb. AnkINK stores only
   the returned host key under /var/local/ankink; it does not save the password.

The launcher first closes any old AnkINK UI/backend, starts the bundled ankinkd
backend on 127.0.0.1:9257, installs the HTML application at a content-versioned
path under /var/local/mesquite, registers org.ankink.app, and launches it through
Amazon's app manager. The UI Close button exits both the UI and backend.
The same installation contains isolated ARMEL and ARMHF native runtimes. The
launcher detects the firmware userspace ABI and selects the matching executable,
dynamic loader, and libraries automatically; users do not need to choose an
architecture. The Whisper Touch helper is enabled on ARMEL and on detected
ARMHF Oasis 2/3 devices with physical page buttons.
The UI removes the otherwise-empty native navigation strip and renders Anki
TeX equations locally using the bundled KaTeX fonts and native renderer.
Reviews use Anki 26.08's official scheduler and are written to the collection.
Normal collection/media sync and guarded full download are supported. Full
upload is not included in this release.

AnkINK is free software under GNU AGPL v3 or later, without warranty. License,
source, and third-party notices are included in this directory and at:

    https://github.com/nimamox/AnkINK

Tap a card image to toggle its full-width view. The moon/sun button toggles
night mode. Oasis page buttons map Forward to Show Answer/Good and Backward to
Undo/Again. Refresh performs a full flashing e-ink refresh when FBInk is
available at /usr/bin/fbink or in MRInstaller's PW2 directory. The rotation
button switches between automatic sensor rotation and locking the current
orientation. AnkINK releases the orientation lock when it closes.
