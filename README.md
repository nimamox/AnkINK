![AnkINK](logo/ankink_logo_orig_size.png)

# AnkINK

AnkINK is an Anki reader and review client optimized for Kindle and
e-ink hardware. It is designed to make reviewing feel immediate on older,
resource-constrained devices: the interface is small, touch-friendly, and
careful about unnecessary work and e-ink refreshes.

AnkINK uses Anki's official Rust backend rather than reimplementing Anki's
scheduler. Card queues, answer intervals, review history, template rendering,
and FSRS or legacy scheduling behavior therefore come from Anki itself.

## Features

- AnkiWeb sign-in and normal collection and media synchronization.
- Official Anki scheduling, including FSRS and legacy scheduler compatibility.
- Deck selection, live new/learning/review counts, review activity, and the
  standard Again, Hard, Good, and Easy answers.
- Card actions for undo, bury, suspend, and flags.
- Card images, including a tap-to-expand view.
- Native mathematics rendering with bundled KaTeX fonts; no browser-side TeX
  engine or network-hosted math assets are required.
- Reader font selection and sizing, portrait/landscape support, and configurable
  day/night presentation.
- Manual or periodic full e-ink refreshes to control ghosting.
- Physical Kindle page-button support where the device and firmware provide
  those buttons. Direction can be reversed in Settings.

AnkINK is primarily a reviewing client. It does not currently provide full
Anki authoring: notes, decks, card templates, and deck options are not edited
on the Kindle. A guarded full download is available when Anki requires one;
full upload is deliberately not exposed.

## Platform support

Current releases target **PW2-compatible ARMEL Kindles** with
Amazon's Mesquite application runtime. Development and real-device behavior in
this repository are focused on that Kindle firmware family, including Oasis
page-button integration where available.

The simulator includes profiles for many Kindle screen sizes, but a simulator
profile is not a claim that the corresponding physical model has been tested.
There is not yet a comprehensive per-model compatibility matrix. Other e-ink
platforms are not currently supported; support for additional e-ink devices is
intended in the future.

## Installation over USB

Download and extract a packaged AnkINK release on a computer. The archive is
laid out like the root of the Kindle USB drive. Copy the archive's **contents**
to the top level of the mounted Kindle drive:

```text
ankink/                  -> /mnt/us/ankink
extensions/AnkINK/       -> /mnt/us/extensions/AnkINK
documents/AnkINK.sh      -> /mnt/us/documents/AnkINK.sh
```

Do not copy an enclosing release or `dist` directory. Safely eject the Kindle,
then launch AnkINK from the Library or from KUAL. The direct Library entry
requires PEKI, the same script-launcher support used by a Library-installed
`KUAL.sh`; KUAL remains an optional launch route.

On first launch, sign in to AnkiWeb and download the collection. Later, use the
Sync button to exchange reviews and media with AnkiWeb. The Close button exits
both the interface and its local backend.

## Using AnkINK

Choose a deck from the collection screen, reveal each answer, and select the
Anki rating. The toolbar reports the current new, learning, and review counts.
Card actions provide undo, bury, suspend, and flag controls without turning the
application into a note editor.

On Kindles with physical page buttons, the configured forward button scrolls
through long card content before revealing the answer and selecting Good; the
other direction scrolls upward before offering Undo or Again. Touch controls
remain available on every supported device. Use Refresh for a full flashing
update when ghosting accumulates, and use Settings for fonts, night mode,
orientation, page-button direction, and automatic refresh frequency.

## Security and privacy

The AnkiWeb password is used only to obtain an Anki host key and is then
discarded. The host key, collection, and synchronized media are stored outside
the USB-visible filesystem under `/var/local/ankink`; the host-key file is mode
0600. AnkINK's application API listens only on the Kindle loopback interface.

Card HTML is sanitized, card styling is scoped to the card area, scripts and
external resources are blocked, and media is loaded from the local synchronized
collection. As with Anki itself, synchronization sends collection data to the
configured AnkiWeb service.

## Technical characteristics

AnkINK uses the Kindle's built-in Mesquite browser for display and a compact
C++17 daemon for local application services. Anki's pinned Rust backend handles
the collection, scheduler, synchronization, and native math conversion. The
package does not install a replacement browser, graphics stack, or Rust runtime
on the Kindle.

## Building from source

With Docker installed and running locally:

```sh
./build_on_docker.sh
```

To build through an SSH-accessible machine that has Docker:

```sh
./build_on_docker.sh user@host
```

The remote form synchronizes the working tree, runs the same Docker build on
the remote machine, and returns the completed artifacts to the local `dist/`
directory. In either mode, `dist/` contains the USB-ready `ankink`,
`extensions`, and `documents` entries.

## Deploy over SSH

If the Kindle has SSH access, for example through USBNetwork, the built project
can optionally be installed with:

```sh
bash push_over_ssh.sh root@device_ip
```

This installs the three components under `/mnt/us` but does not relaunch the
application. Normal users do not need SSH and can use the USB installation
method above.

For simulator setup, development environment configuration, architecture
details, cross-compilation internals, and debugging workflows, see
[DEVELOPMENT.md](DEVELOPMENT.md).

## Project status and license

AnkINK is an independent, focused 0.x reviewing client under active
development. It is not affiliated with or endorsed by Ankitects. Anki and
AnkiWeb are used only to identify compatibility; AnkINK does not distribute
Anki's official logo.

AnkINK is Copyright (C) 2026 AnkINK contributors and is free software licensed
under the [GNU Affero General Public License v3.0 or later](LICENSE). It
statically incorporates the pinned AGPL-3.0-or-later Anki backend. Binary
releases and redistributions must preserve the applicable notices and provide
equivalent access to the corresponding source. See [SOURCE.md](SOURCE.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
