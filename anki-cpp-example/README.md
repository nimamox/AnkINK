# AnkiWeb deck names (C++)

Requires libcurl, zstd, SQLite, CMake, and a C++17 compiler.

```sh
cmake -S . -B build
cmake --build build
./build/anki-decks
```

Copy `config.example.json` to the ignored `config.json`, then add your AnkiWeb
credentials. Never commit that file. This spike is not used by the AnkINK app.
