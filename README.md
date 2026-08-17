# AnkINK

AnkINK is a C++17 Anki reader for jailbroken Kindle devices. Its UI uses the
Kindle's built-in Mesquite application runtime; a small C++ daemon provides the
real Anki collection over a loopback-only HTTP API:

```text
Mesquite HTML/CSS/ES5 UI -> XMLHttpRequest -> 127.0.0.1:8765 -> ankinkd -> SQLite
```

There is no bundled WPE WebKit, Mesa, Wayland, DRM, EGL, or FBInk display path.
Mesquite owns painting, e-ink updates, touch input, and system integration.

The current review engine opens a modern `collection.anki2` read-only. Ratings
retire cards only for the current process and do not yet write scheduler or
revlog state. This avoids corrupting a collection until scheduler compatibility
and sync are implemented.

## Host development

```sh
cmake -S . -B build-host -DANKINK_BUILD_APP=ON -DANKINK_BUILD_TESTS=ON
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
./build-host/ankinkd --collection \
  "$HOME/Library/Application Support/Anki2/main_account/collection.anki2" \
  --assets ./assets
```

Open `http://127.0.0.1:8765/`. The daemon registers Anki's `unicase` collation
before reading the database.

## Kindle deployment

Cross-build with `cmake/kindle-debian-toolchain.cmake`, then create the minimal
runtime bundle using `scripts/package-kindle.sh`. Copy the resulting directory
to `/mnt/us/ankink` and copy `kual-extension/AnkINK` to
`/mnt/us/extensions/AnkINK`. Put `collection.anki2` directly inside
`/mnt/us/ankink`.

The KUAL action starts `ankinkd`, installs the Mesquite assets under
`/var/local/mesquite/ankink`, registers `org.ankink.app` in
`/var/local/appreg.db`, and asks `com.lab126.appmgrd` to launch it. It does not
stop `lab126_gui`.
