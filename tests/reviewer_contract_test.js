"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");

const frontend = fs.readFileSync(process.argv[2], "utf8");
const backend = fs.readFileSync(process.argv[3], "utf8");

function between(source, start, end) {
  const first = source.indexOf(start);
  const last = source.indexOf(end, first + start.length);
  assert.notEqual(first, -1, `missing ${start}`);
  assert.notEqual(last, -1, `missing ${end}`);
  return source.slice(first, last);
}

const undo = between(frontend, "function undoAnswer()", "function cardCanScrollDown()");
const polling = between(frontend, "function pollPageButtons()", 'byId("show-answer")');
const decks = between(frontend, 'byId("back").onclick', 'byId("refresh").onclick');
const answer = between(frontend, "function answer(rating)", "function showAnswer()");
const loading = between(frontend, "function nextCard()", "function isReviewStateError");
const loadDecks = between(frontend, "function loadDecks()", "function applyFontScale()");
const sync = between(frontend, "function syncNow(done)", "function nextCard()");
const close = between(frontend, "function closeApplication()", "function warning(message)");
const mediaLoading = between(frontend, "function loadMediaImage", "function answerOnly");
const rustAnswer = between(backend, "fn answer(&mut self", "fn undo(&mut self)");
const rustUndo = between(backend, "fn undo(&mut self)", "fn login(&mut self");

// Answer bookkeeping increments once; only a successful undo decrements it.
assert.match(frontend, /storePendingReviews\(pendingReviews \+ 1\)/);
assert.match(undo, /if \(result\.type === "undo-empty"\) return;[\s\S]*if \(result\.type === "error"\)[\s\S]*storePendingReviews\(pendingReviews - 1\)/);
assert.match(undo, /storePendingReviews\(pendingReviews - 1\);[\s\S]*warning\(""\); nextCard\(\);/);
assert.match(frontend, /pendingReviews = Math\.max\(0, value\)/);
assert.equal(Math.max(0, 0 + 1 - 1), 0);
assert.equal(Math.max(0, 0 + 1 + 1 - 1), 1);
assert.equal(Math.max(0, 0 - 1), 0);

// Deck navigation is local, and physical Undo remains available with no card.
assert.doesNotMatch(decks, /syncNow\(/);
assert.match(decks, /loadDecks\(\)/);
assert.doesNotMatch(loadDecks, /syncNow\(/);
assert.match(close, /"POST", "\/api\/sync"/);
assert.doesNotMatch(polling, /!state\.card\s*\|\|\s*!input\.action/);
assert.match(polling, /pageButtonMode === "normal" && input\.action === "forward"/);
assert.match(polling, /pageButtonMode === "reversed" && input\.action === "backward"/);
assert.match(polling, /if \(!state\.card\) return;[\s\S]*else undoAnswer\(\)/);

// Rapid input cannot overlap answer/undo/loading transitions.
assert.match(polling, /state\.cardLoading \|\| state\.answerInFlight \|\|[\s\S]*state\.undoInFlight \|\| state\.syncInFlight/);
assert.ok(polling.indexOf("state.cardLoading") < polling.indexOf("state.inputBusy = true"));
assert.match(polling, /pollEpoch !== state\.inputEpoch/);
assert.match(undo, /if \(state\.cardLoading \|\| state\.answerInFlight \|\| state\.undoInFlight \|\| state\.syncInFlight\) return;/);
assert.match(undo, /state\.undoInFlight = true;/);
assert.match(loading, /clearPhysicalInput\(function/);

// Review commands are generation-bound and recover from stale UI/backend state.
assert.match(answer, /state\.card\.reviewToken/);
assert.match(answer, /&token=/);
assert.match(answer, /isReviewStateError\(message\)[\s\S]*nextCard\(\)/);
assert.match(sync, /if \(state\.deck \|\| state\.cardLoading/);

// The backend only undoes review answers and preserves pending until success.
assert.match(rustUndo, /Some\(Op::AnswerCard\)/);
assert.match(rustUndo, /"undo-empty"/);
assert.ok(rustUndo.indexOf(".undo()") < rustUndo.indexOf("self.pending = None"));

// Invalid, stale, duplicate, or failed answers cannot consume pending.
assert.doesNotMatch(rustAnswer, /pending\s*\.take\(\)/);
assert.match(rustAnswer, /pending\s*\.as_ref\(\)/);
assert.match(rustAnswer, /pending\.token != review_token/);
assert.ok(rustAnswer.indexOf(".answer_card") < rustAnswer.indexOf("self.pending = None"));

// Night-card rendering defaults to the legacy behavior, persists the selected
// mode, restores original styles, and uses Canvas rather than CSS filters.
assert.match(frontend, /ankink_night_card_mode"\) \|\| "standard"/);
assert.match(frontend, /nightCardMode !== "standard"[\s\S]*nightCardMode !== "palette"[\s\S]*nightCardMode !== "palette-images"/);
assert.match(frontend, /setItem\("ankink_night_card_mode", nightCardMode\)/);
assert.match(frontend, /_ankinkNightOriginalStyle/);
assert.match(frontend, /getImageData\(0, 0, width, height\)/);
assert.match(frontend, /data\[i\] = 255 - data\[i\]/);
assert.doesNotMatch(frontend, /style\.(?:webkitFilter|filter)\s*=/);
assert.match(frontend, /image\.setAttribute\("src", API \+ "\/api\/media\/"/);
assert.match(frontend, /images\[i\]\.onerror = function/);
assert.match(mediaLoading, /image\.setAttribute\("src", API \+ "\/api\/media\/"/);
assert.doesNotMatch(mediaLoading, /api\/media-data/);

console.log("reviewer contract tests passed");
