#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnkinkAnkiBackend AnkinkAnkiBackend;

AnkinkAnkiBackend *ankink_anki_backend_new(void);
void ankink_anki_backend_free(AnkinkAnkiBackend *backend);

char *ankink_anki_open(AnkinkAnkiBackend *backend, const char *path);
char *ankink_anki_decks(AnkinkAnkiBackend *backend);
char *ankink_anki_next_card(AnkinkAnkiBackend *backend, int64_t deck_id);
char *ankink_anki_answer(AnkinkAnkiBackend *backend, int64_t card_id,
                         int32_t rating);
char *ankink_anki_undo(AnkinkAnkiBackend *backend);
char *ankink_anki_login(AnkinkAnkiBackend *backend, const char *username,
                        const char *password);
char *ankink_anki_set_host_key(AnkinkAnkiBackend *backend,
                               const char *host_key);
char *ankink_anki_host_key(AnkinkAnkiBackend *backend);
char *ankink_anki_logout(AnkinkAnkiBackend *backend);
char *ankink_anki_sync(AnkinkAnkiBackend *backend);
char *ankink_anki_full_download(AnkinkAnkiBackend *backend);
void ankink_anki_string_free(char *value);

#ifdef __cplusplus
}
#endif
