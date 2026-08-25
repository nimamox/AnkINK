// Copyright: AnkINK contributors
// License: GNU AGPL, version 3 or later

mod math;

use std::ffi::{c_char, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use anki::card::CardId;
use anki::collection::{Collection, CollectionBuilder};
use anki::decks::DeckId;
use anki::scheduler::answering::{CardAnswer, Rating};
use anki::scheduler::states::{CardState, SchedulingStates};
use anki::sync::collection::normal::SyncActionRequired;
use anki::sync::login::{sync_login, SyncAuth};
use anki::sync::media::progress::MediaSyncProgress;
use anki::timestamp::{TimestampMillis, TimestampSecs};
use anki::undo::Op;
use math::render_card_html;
use serde_json::{json, Value};

fn flatten_deck_tree(
    node: &anki_proto::decks::DeckTreeNode,
    parent: &str,
    decks: &mut Vec<Value>,
) {
    let name = if parent.is_empty() {
        node.name.clone()
    } else {
        format!("{parent}::{}", node.name)
    };
    if node.deck_id != 0 {
        let due = node.new_count + node.learn_count + node.review_count;
        decks.push(json!({
            "id": node.deck_id,
            "name": name,
            "cards": due,
            "new": node.new_count,
            "learning": node.learn_count,
            "review": node.review_count
        }));
    }
    for child in &node.children {
        flatten_deck_tree(child, &name, decks);
    }
}

struct PendingCard {
    id: CardId,
    states: SchedulingStates,
    token: u64,
}

pub struct AnkinkAnkiBackend {
    collection: Option<Collection>,
    collection_path: String,
    pending: Option<PendingCard>,
    next_review_token: u64,
    sync_auth: Option<SyncAuth>,
    runtime: tokio::runtime::Runtime,
    web_client: reqwest::Client,
}

impl AnkinkAnkiBackend {
    fn new() -> Result<Self, String> {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .map_err(|error| error.to_string())?;
        let web_client = reqwest::Client::builder()
            .user_agent("AnkINK/0.3")
            .build()
            .map_err(|error| error.to_string())?;
        Ok(Self {
            collection: None,
            collection_path: String::new(),
            pending: None,
            next_review_token: 0,
            sync_auth: None,
            runtime,
            web_client,
        })
    }

    fn collection(&mut self) -> Result<&mut Collection, String> {
        self.collection
            .as_mut()
            .ok_or_else(|| "No collection is open".to_owned())
    }

    fn open(&mut self, path: &str) -> Result<Value, String> {
        let collection = CollectionBuilder::new(path)
            .with_desktop_media_paths()
            .build()
            .map_err(|error| error.to_string())?;
        self.collection = Some(collection);
        self.collection_path = path.to_owned();
        self.pending = None;
        Ok(json!({"type": "opened", "path": path}))
    }

    fn decks(&mut self) -> Result<Value, String> {
        let path = self.collection_path.clone();
        let tree = self
            .collection()?
            .deck_tree(Some(TimestampSecs::now()))
            .map_err(|error| error.to_string())?;
        let mut decks = Vec::new();
        for child in &tree.children {
            flatten_deck_tree(child, "", &mut decks);
        }
        Ok(json!({"type": "decks", "path": path, "decks": decks}))
    }

    fn next_card(&mut self, deck_id: i64) -> Result<Value, String> {
        self.pending = None;
        let collection = self.collection()?;
        collection
            .set_current_deck(DeckId(deck_id))
            .map_err(|error| error.to_string())?;

        let queued_cards = collection
            .get_queued_cards(1, false)
            .map_err(|error| error.to_string())?;
        let counts = json!({
            "new": queued_cards.new_count,
            "learning": queued_cards.learning_count,
            "review": queued_cards.review_count,
        });
        let Some(queued) = queued_cards.cards.first().cloned()
        else {
            return Ok(json!({"type": "complete", "deckId": deck_id}));
        };

        let rendered = collection
            .render_existing_card(queued.card.id(), false, false)
            .map_err(|error| error.to_string())?;
        let intervals = collection
            .describe_next_states(&queued.states)
            .map_err(|error| error.to_string())?;
        let id = queued.card.id();
        self.next_review_token = self.next_review_token.wrapping_add(1);
        if self.next_review_token == 0 {
            self.next_review_token = 1;
        }
        let review_token = self.next_review_token;
        self.pending = Some(PendingCard {
            id,
            states: queued.states,
            token: review_token,
        });

        Ok(json!({
            "type": "card",
            "deckId": deck_id,
            "id": id.0,
            "reviewToken": review_token,
            "front": render_card_html(&rendered.question()),
            "back": render_card_html(&rendered.answer()),
            "css": rendered.css,
            "counts": counts,
            "buttons": [
                {"rating": 1, "label": "Again", "interval": intervals[0]},
                {"rating": 2, "label": "Hard", "interval": intervals[1]},
                {"rating": 3, "label": "Good", "interval": intervals[2]},
                {"rating": 4, "label": "Easy", "interval": intervals[3]}
            ]
        }))
    }

    fn answer(&mut self, card_id: i64, review_token: u64, rating: i32) -> Result<Value, String> {
        let rating_number = rating;
        let rating = match rating_number {
            1 => Rating::Again,
            2 => Rating::Hard,
            3 => Rating::Good,
            4 => Rating::Easy,
            _ => return Err("Rating must be between 1 and 4".to_owned()),
        };

        // Keep pending intact until the request is fully validated and Anki has
        // successfully applied the answer. A stale or duplicate request must
        // never poison the currently displayed review card.
        let (pending_id, current_state, new_state): (CardId, CardState, CardState) = {
            let pending = self
                .pending
                .as_ref()
                .ok_or_else(|| "No queued card is awaiting an answer".to_owned())?;
            if pending.id.0 != card_id || pending.token != review_token {
                return Err("Stale review command; reloading the current card".to_owned());
            }
            let new_state = match rating {
                Rating::Again => pending.states.again.clone(),
                Rating::Hard => pending.states.hard.clone(),
                Rating::Good => pending.states.good.clone(),
                Rating::Easy => pending.states.easy.clone(),
            };
            (pending.id, pending.states.current.clone(), new_state)
        };
        self.collection()?
            .answer_card(&mut CardAnswer {
                card_id: pending_id,
                current_state,
                new_state,
                rating,
                answered_at: TimestampMillis::now(),
                milliseconds_taken: 0,
                custom_data: None,
                from_queue: true,
            })
            .map_err(|error| error.to_string())?;
        self.pending = None;
        Ok(json!({"type": "answered", "id": card_id, "reviewToken": review_token, "rating": rating_number}))
    }

    fn undo(&mut self) -> Result<Value, String> {
        if !matches!(self.collection()?.can_undo(), Some(Op::AnswerCard)) {
            return Ok(json!({"type": "undo-empty"}));
        }
        self.collection()?
            .undo()
            .map_err(|error| error.to_string())?;
        self.pending = None;
        Ok(json!({"type": "undone"}))
    }

    fn login(&mut self, username: &str, password: &str) -> Result<Value, String> {
        let auth = self
            .runtime
            .block_on(sync_login(
                username.to_owned(),
                password.to_owned(),
                None,
                self.web_client.clone(),
            ))
            .map_err(|error| error.to_string())?;
        self.sync_auth = Some(auth);
        Ok(json!({"type": "authenticated", "username": username}))
    }

    fn set_host_key(&mut self, host_key: &str) -> Result<Value, String> {
        if host_key.is_empty() {
            return Err("Host key is empty".to_owned());
        }
        self.sync_auth = Some(SyncAuth {
            hkey: host_key.to_owned(),
            endpoint: None,
            io_timeout_secs: Some(120),
        });
        Ok(json!({"type": "authenticated"}))
    }

    fn logout(&mut self) -> Result<Value, String> {
        self.sync_auth = None;
        Ok(json!({"type": "signed-out"}))
    }

    fn sync(&mut self) -> Result<Value, String> {
        let auth = self
            .sync_auth
            .clone()
            .ok_or_else(|| "Sign in to AnkiWeb first".to_owned())?;
        let web_client = self.web_client.clone();
        let runtime = &self.runtime;
        let collection = self
            .collection
            .as_mut()
            .ok_or_else(|| "No collection is open".to_owned())?;
        let output = runtime
            .block_on(collection.normal_sync(auth, web_client))
            .map_err(|error| error.to_string())?;
        self.pending = None;
        if let Some(endpoint) = &output.new_endpoint {
            let endpoint = reqwest::Url::parse(endpoint)
                .map_err(|error| format!("Invalid sync endpoint: {error}"))?;
            if let Some(auth) = &mut self.sync_auth {
                auth.endpoint = Some(endpoint);
            }
        }
        let (required, upload_ok, download_ok) = match output.required {
            SyncActionRequired::NoChanges => ("none", false, false),
            SyncActionRequired::NormalSyncRequired => ("normal", false, false),
            SyncActionRequired::FullSyncRequired {
                upload_ok,
                download_ok,
            } => ("full", upload_ok, download_ok),
        };
        let media_synced = required == "none";
        if media_synced {
            self.sync_media()?;
        }
        Ok(json!({
            "type": "sync",
            "required": required,
            "uploadAllowed": upload_ok,
            "downloadAllowed": download_ok,
            "message": output.server_message,
            "hostNumber": output.host_number,
            "newEndpoint": output.new_endpoint,
            "mediaSynced": media_synced
        }))
    }

    fn sync_media(&mut self) -> Result<(), String> {
        let auth = self
            .sync_auth
            .clone()
            .ok_or_else(|| "Sign in to AnkiWeb first".to_owned())?;
        let client = self.web_client.clone();
        let runtime = &self.runtime;
        let collection = self
            .collection
            .as_ref()
            .ok_or_else(|| "No collection is open".to_owned())?;
        let media = collection.media().map_err(|error| error.to_string())?;
        let progress = collection.new_progress_handler::<MediaSyncProgress>();
        runtime
            .block_on(media.sync_media(progress, auth, client, None))
            .map_err(|error| error.to_string())
    }

    fn full_download(&mut self) -> Result<Value, String> {
        let auth = self
            .sync_auth
            .clone()
            .ok_or_else(|| "Sign in to AnkiWeb first".to_owned())?;
        let path = self.collection_path.clone();
        let collection = self
            .collection
            .take()
            .ok_or_else(|| "No collection is open".to_owned())?;
        let result = self
            .runtime
            .block_on(collection.full_download(auth, self.web_client.clone()))
            .map_err(|error| error.to_string());
        let reopen = CollectionBuilder::new(&path)
            .with_desktop_media_paths()
            .build()
            .map_err(|error| error.to_string());
        match reopen {
            Ok(collection) => self.collection = Some(collection),
            Err(error) => return Err(error),
        }
        result?;
        self.pending = None;
        self.sync_media()?;
        Ok(json!({"type": "sync", "required": "none", "fullDownload": true, "mediaSynced": true}))
    }
}

fn json_string(result: Result<Value, String>) -> *mut c_char {
    let value = match result {
        Ok(value) => value,
        Err(message) => json!({"type": "error", "message": message}),
    };
    CString::new(value.to_string()).unwrap().into_raw()
}

fn ffi_json<F>(backend: *mut AnkinkAnkiBackend, operation: F) -> *mut c_char
where
    F: FnOnce(&mut AnkinkAnkiBackend) -> Result<Value, String>,
{
    if backend.is_null() {
        return json_string(Err("Invalid Anki backend handle".to_owned()));
    }
    match catch_unwind(AssertUnwindSafe(|| operation(unsafe { &mut *backend }))) {
        Ok(result) => json_string(result),
        Err(_) => json_string(Err("The Anki backend panicked".to_owned())),
    }
}

fn required_string(value: *const c_char, name: &str) -> Result<String, String> {
    if value.is_null() {
        return Err(format!("Missing {name}"));
    }
    unsafe { CStr::from_ptr(value) }
        .to_str()
        .map(str::to_owned)
        .map_err(|_| format!("{name} is not valid UTF-8"))
}

#[no_mangle]
pub extern "C" fn ankink_anki_backend_new() -> *mut AnkinkAnkiBackend {
    match AnkinkAnkiBackend::new() {
        Ok(backend) => Box::into_raw(Box::new(backend)),
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn ankink_anki_backend_free(backend: *mut AnkinkAnkiBackend) {
    if !backend.is_null() {
        drop(Box::from_raw(backend));
    }
}

#[no_mangle]
pub extern "C" fn ankink_anki_open(
    backend: *mut AnkinkAnkiBackend,
    path: *const c_char,
) -> *mut c_char {
    ffi_json(backend, |backend| {
        let path = required_string(path, "collection path")?;
        backend.open(&path)
    })
}

#[no_mangle]
pub extern "C" fn ankink_anki_decks(backend: *mut AnkinkAnkiBackend) -> *mut c_char {
    ffi_json(backend, AnkinkAnkiBackend::decks)
}

#[no_mangle]
pub extern "C" fn ankink_anki_next_card(
    backend: *mut AnkinkAnkiBackend,
    deck_id: i64,
) -> *mut c_char {
    ffi_json(backend, |backend| backend.next_card(deck_id))
}

#[no_mangle]
pub extern "C" fn ankink_anki_answer(
    backend: *mut AnkinkAnkiBackend,
    card_id: i64,
    review_token: u64,
    rating: i32,
) -> *mut c_char {
    ffi_json(backend, |backend| backend.answer(card_id, review_token, rating))
}

#[no_mangle]
pub extern "C" fn ankink_anki_undo(backend: *mut AnkinkAnkiBackend) -> *mut c_char {
    ffi_json(backend, AnkinkAnkiBackend::undo)
}

#[no_mangle]
pub extern "C" fn ankink_anki_login(
    backend: *mut AnkinkAnkiBackend,
    username: *const c_char,
    password: *const c_char,
) -> *mut c_char {
    ffi_json(backend, |backend| {
        let username = required_string(username, "username")?;
        let password = required_string(password, "password")?;
        backend.login(&username, &password)
    })
}

#[no_mangle]
pub extern "C" fn ankink_anki_set_host_key(
    backend: *mut AnkinkAnkiBackend,
    host_key: *const c_char,
) -> *mut c_char {
    ffi_json(backend, |backend| {
        let host_key = required_string(host_key, "host key")?;
        backend.set_host_key(&host_key)
    })
}

#[no_mangle]
pub extern "C" fn ankink_anki_host_key(
    backend: *mut AnkinkAnkiBackend,
) -> *mut c_char {
    if backend.is_null() {
        return ptr::null_mut();
    }
    let backend = unsafe { &mut *backend };
    match &backend.sync_auth {
        Some(auth) => CString::new(auth.hkey.clone()).unwrap().into_raw(),
        None => ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn ankink_anki_logout(backend: *mut AnkinkAnkiBackend) -> *mut c_char {
    ffi_json(backend, AnkinkAnkiBackend::logout)
}

#[no_mangle]
pub extern "C" fn ankink_anki_sync(backend: *mut AnkinkAnkiBackend) -> *mut c_char {
    ffi_json(backend, AnkinkAnkiBackend::sync)
}

#[no_mangle]
pub extern "C" fn ankink_anki_full_download(
    backend: *mut AnkinkAnkiBackend,
) -> *mut c_char {
    ffi_json(backend, AnkinkAnkiBackend::full_download)
}

#[no_mangle]
pub unsafe extern "C" fn ankink_anki_string_free(value: *mut c_char) {
    if !value.is_null() {
        drop(CString::from_raw(value));
    }
}

#[no_mangle]
pub extern "C" fn ankink_anki_null() -> *mut c_char {
    ptr::null_mut()
}
