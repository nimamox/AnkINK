#include "ankink/anki_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_and_free(char *json) {
  puts(json ? json : "<null>");
  ankink_anki_string_free(json);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s COLLECTION DECK_ID\n", argv[0]);
    return 2;
  }
  AnkinkAnkiBackend *backend = ankink_anki_backend_new();
  if (!backend)
    return 1;

  print_and_free(ankink_anki_open(backend, argv[1]));
  print_and_free(ankink_anki_decks(backend));
  print_and_free(ankink_anki_next_card(backend, strtoll(argv[2], NULL, 10)));
  ankink_anki_backend_free(backend);
  return 0;
}
