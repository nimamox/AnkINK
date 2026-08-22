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
assert.match(decks, /loadDecks\(true\)/);
assert.doesNotMatch(polling, /!state\.card\s*\|\|\s*!input\.action/);
assert.match(polling, /pageButtonMode === "normal" && input\.action === "forward"/);
assert.match(polling, /pageButtonMode === "reversed" && input\.action === "backward"/);
assert.match(polling, /if \(!state\.card\) return;[\s\S]*else undoAnswer\(\)/);

// Rapid input cannot overlap answer/undo/loading transitions.
assert.match(polling, /state\.cardLoading \|\| state\.answerInFlight \|\| state\.undoInFlight/);
assert.match(undo, /if \(state\.cardLoading \|\| state\.answerInFlight \|\| state\.undoInFlight\) return;/);
assert.match(undo, /state\.undoInFlight = true;/);

// The backend only undoes review answers and preserves pending until success.
assert.match(rustUndo, /Some\(Op::AnswerCard\)/);
assert.match(rustUndo, /"undo-empty"/);
assert.ok(rustUndo.indexOf(".undo()") < rustUndo.indexOf("self.pending = None"));

console.log("reviewer contract tests passed");
