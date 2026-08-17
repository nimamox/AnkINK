AnkINK for PW2-compatible ARMEL Kindles
=======================================

This package is laid out for the root of the Kindle USB drive.

1. Connect the jailbroken Kindle by USB.
2. Copy the `ankink` and `extensions` directories to the root of the Kindle
   drive. Merge the existing `extensions` directory when prompted; do not
   replace or delete it.
3. Put your Anki collection at `ankink/collection.anki2`.
4. Safely eject the Kindle, open KUAL, and choose AnkINK > Start AnkINK.

The resulting Kindle paths must be:

  /mnt/us/ankink/ankink.sh
  /mnt/us/ankink/bin/ankink
  /mnt/us/ankink/collection.anki2
  /mnt/us/extensions/AnkINK/menu.json

This build targets 32-bit ARM EABI5 with the soft-float calling convention
(the same ABI family used by PW2 release packages). It is not the ARMHF build.
Its software renderer is compiled specifically for ARMv7 and does not require
the Linux 3.1 __kernel_cmpxchg64 helper, so it supports the Kindle 3.0 kernel.
