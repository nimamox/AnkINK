"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const frontend = fs.readFileSync(process.argv[2], "utf8");
const backend = fs.readFileSync(process.argv[3], "utf8");
const index = fs.readFileSync(process.argv[4], "utf8");
const katexJavascript = process.argv[5];
const katexCss = fs.readFileSync(process.argv[6], "utf8");
const fontDirectory = process.argv[7];
const appCss = fs.readFileSync(process.argv[8], "utf8");
const runKindle = fs.readFileSync(process.argv[9], "utf8");
const simulatorIndex = fs.readFileSync(process.argv[10], "utf8");
const simulatorJavascript = fs.readFileSync(process.argv[11], "utf8");
const simulatorCss = fs.readFileSync(process.argv[12], "utf8");
const httpServer = fs.readFileSync(process.argv[13], "utf8");
const collectionBackend = fs.readFileSync(process.argv[14], "utf8");
const whisperTouch = fs.readFileSync(process.argv[15], "utf8");

function between(source, start, end) {
  const first = source.indexOf(start);
  const last = source.indexOf(end, first + start.length);
  assert.notEqual(first, -1, `missing ${start}`);
  assert.notEqual(last, -1, `missing ${end}`);
  return source.slice(first, last);
}

const undo = between(frontend, "function undoAnswer()", "function cardCanScrollDown()");
const pageButtons = between(frontend, "var pageButtonDownCode", 'byId("show-answer")');
const decks = between(frontend, 'byId("back").onclick', 'byId("refresh").onclick');
const answer = between(frontend, "function answer(rating)", "function showAnswer()");
const loading = between(frontend, "function nextCard()", "function isReviewStateError");
const controlRepaint = between(frontend, "function prepareShowAnswerControl()", "function nextCard()");
const loadDecks = between(frontend, "function loadDecks()", "function applyFontScale(");
const startup = between(frontend, "function startApplication()", "function loadDecks()");
const sync = between(frontend, "function syncNow(done)", "function nextCard()");
const close = between(frontend, "function closeApplication()", "function warning(message)");
const mediaLoading = between(frontend, "function loadMediaImage", "function answerOnly");
const rustAnswer = between(backend, "fn answer(&mut self", "fn undo(&mut self)");
const rustCardAction = between(backend, "fn card_action(", "fn undo(&mut self)");
const rustUndo = between(backend, "fn undo(&mut self)", "fn login(&mut self");
const cardActions = between(frontend, "/* CARD_ACTIONS_BEGIN */", "/* CARD_ACTIONS_END */");

// The Mesquite frontend and Kindle launcher share AnkINK's dedicated loopback port.
assert.match(frontend, /var API = "http:\/\/127\.0\.0\.1:9257"/);
assert.doesNotMatch(frontend, /8765/);
assert.match(runKindle, /--port 9257/);

// The daemon owns durable state; the frontend only mirrors returned counts.
assert.doesNotMatch(frontend, /localStorage|sessionStorage/);
assert.match(frontend, /request\("GET", "\/api\/settings"/);
assert.match(frontend, /request\("POST", "\/api\/settings"/);
assert.match(frontend, /storePendingFromResult\(result, pendingReviews \+ 1\)/);
assert.match(undo, /if \(result\.type === "undo-empty"\)[\s\S]*return;[\s\S]*if \(result\.type === "error"\)[\s\S]*storePendingFromResult\(result, pendingReviews - 1\)/);
assert.match(frontend, /pendingReviews = Math\.max\(0, value\)/);
assert.equal(Math.max(0, 0 + 1 - 1), 0);
assert.equal(Math.max(0, 0 + 1 + 1 - 1), 1);
assert.equal(Math.max(0, 0 - 1), 0);

// Rotation is persistent, uses Mesquite's device API when available, and is
// harmless in the host simulator. The launcher and simulator expose all four
// orientations while keeping the seven action buttons uniformly sized.
assert.match(runKindle, /'supportedOrientation','UDLR'/);
assert.match(runKindle, /LD_PRELOAD=.*libmesquite-whisper-touch\.so \/usr\/bin\/mesquite/);
assert.match(whisperTouch, /win_mgr_utils_new_application_name/);
assert.match(whisperTouch, /win_mgr_utils_add_is_wisper_touch_supported/);
assert.match(whisperTouch, /win_mgr_utils_new_name\(0, "application"\)/);
assert.match(whisperTouch, /win_mgr_utils_add_is_wisper_touch_supported\(name, 1\)/);
assert.match(index, /class="header-actions"[\s\S]*id="close"[\s\S]*id="refresh"[\s\S]*id="rotation"[\s\S]*id="sync"[\s\S]*id="night-mode"[\s\S]*class="font-controls"/);
const headerActions = index.match(/<div class="header-actions">([\s\S]*?)<\/div>/)[1];
assert.equal([...headerActions.matchAll(/<button\b/g)].length, 7,
  "header must contain exactly seven uniformly sized action buttons");
assert.match(index, /id="busy-indicator" class="busy-indicator"/);
assert.match(appCss, /\.header-actions > button, \.font-controls button\s*{[^}]*width:\s*66px;[^}]*height:\s*66px;/);
assert.match(appCss, /\.header-actions\s*{[^}]*position:\s*fixed;[^}]*right:\s*4%;[^}]*width:\s*492px;/);
assert.match(appCss, /\.header-actions > button, \.header-actions > \.font-controls\s*{[^}]*float:\s*right;/);
for (const width of [758, 1024, 1080, 1440]) {
  const compact = width <= 900;
  const inset = width * 0.04;
  const centerCellLeft = inset + 66;
  const centerCellRight = width - inset - 492;
  const logoLeft = width / 2 + (compact ? -273 : -328);
  const logoRight = logoLeft + (compact ? 120 : 230);
  assert.ok(logoLeft >= centerCellLeft && logoRight <= centerCellRight,
    `header brand overlaps controls at ${width}px`);
}
assert.match(appCss, /\.busy-indicator\s*{[^}]*visibility:\s*hidden;/);
assert.match(frontend, /function setBusy\(reason, active\)/);
assert.doesNotMatch(frontend, /setInterval\([^)]*busy|busy[^\n]*setInterval/);
assert.match(simulatorIndex, /id="orientation"[\s\S]*value="portrait"[\s\S]*value="landscape"/);
assert.match(simulatorJavascript, /function dimensions\(\)[\s\S]*orientation === "landscape"/);
assert.match(simulatorJavascript, /view\.orientation = orientation === "landscape" \? 90 : 0/);
assert.match(simulatorJavascript, /initEvent\("orientationchange", false, false\)/);
assert.match(simulatorCss, /#orientation\s*{\s*min-width:\s*110px;/);

const rotationMatch = frontend.match(
  /\/\* ROTATION_LOGIC_BEGIN \*\/([\s\S]*?)\/\* ROTATION_LOGIC_END \*\//);
assert.ok(rotationMatch, "missing rotation logic test boundary");
function rotationContext(options) {
  const button = { attributes: {}, setAttribute: function (key, value) {
    this.attributes[key] = value;
  }};
  const calls = [];
  const context = {
    rotationMode: "auto",
    window: { innerWidth: 1072, innerHeight: 1448, orientation: options.orientation },
    byId: function () { return button; },
    encodeURIComponent,
    warning: function (error) { calls.push("warning:" + error); },
    request: function (method, path, body, done) {
      calls.push(method + " " + path + " " + body);
      done(options.failure ? "save failed" : null);
    }
  };
  if (options.device) context.window.kindle = { device: {
    setOrientation: function (value) { calls.push("orientation:" + value); }
  }};
  vm.createContext(context);
  vm.runInContext(rotationMatch[1], context);
  return { context, button, calls };
}
const autoRotation = rotationContext({ device: true, orientation: 0 });
autoRotation.context.chooseRotationMode("auto", true);
assert.equal(autoRotation.context.rotationMode, "auto");
assert.equal(autoRotation.button.innerHTML, "⌽");
assert.deepEqual(autoRotation.calls,
  ["orientation:auto", "POST /api/settings key=rotationMode&value=auto"]);
const lockedRotation = rotationContext({ device: true, orientation: -90 });
lockedRotation.context.chooseRotationMode("locked", false);
assert.equal(lockedRotation.context.rotationMode, "locked");
assert.equal(lockedRotation.button.attributes["aria-label"], "Auto rotation");
assert.deepEqual(lockedRotation.calls, ["orientation:landscapeRight"]);
assert.doesNotThrow(function () {
  rotationContext({ device: false }).context.chooseRotationMode("locked", false);
});
assert.match(frontend, /settings\.rotationMode === "locked"/);
assert.match(frontend, /key=rotationMode&value=/);

// Review activity is a local, table-based 365-day calendar. Exercise the pure
// ES5 date/layout/bucketing code independently of the browser DOM.
const heatmapMatch = frontend.match(
  /\/\* HEATMAP_LOGIC_BEGIN \*\/([\s\S]*?)\/\* HEATMAP_LOGIC_END \*\//);
assert.ok(heatmapMatch, "missing heatmap logic test boundary");
const heatmap = { Date, Math, parseInt, String };
vm.createContext(heatmap);
vm.runInContext('"use strict";\n' + heatmapMatch[1], heatmap);
function model(endDate, days) {
  return heatmap.buildActivityModel({ endDate, days: days || [] });
}
const aligned = model("2026-08-30");
assert.equal(aligned.days.length, 365);
assert.equal(aligned.startDate, "2025-08-31");
assert.equal(aligned.endDate, "2026-08-30");
assert.equal(aligned.weeks.length, 53);
assert.equal(aligned.weeks[0][0].key, "2025-08-31");
assert.equal(aligned.weeks[52][0].key, "2026-08-30");
assert.deepEqual(Object.assign({}, aligned.monthSegments[0]),
  { name: "Sep", startColumn: 0, span: 4 });
assert.deepEqual(Object.assign({}, aligned.monthSegments[aligned.monthSegments.length - 1]),
  { name: "Aug", startColumn: 47, span: 6 });
assert.equal(aligned.monthSegments.reduce(function (total, segment) {
  return total + segment.span;
}, 0), aligned.weeks.length, "month colspans must cover every week column");
const december = aligned.monthSegments.find(function (segment) {
  return segment.name === "Dec";
});
const january = aligned.monthSegments.find(function (segment) {
  return segment.name === "Jan";
});
assert.deepEqual(Object.assign({}, december), { name: "Dec", startColumn: 13, span: 4 });
assert.deepEqual(Object.assign({}, january), { name: "Jan", startColumn: 17, span: 5 });

const partial = model("2026-09-01", [
  { date: "2025-09-02", count: 2 },
  { date: "2026-09-01", count: 7 }
]);
assert.equal(partial.weeks.length, 53);
assert.equal(partial.weeks[0][0], null);
assert.equal(partial.weeks[0][1], null);
assert.equal(partial.weeks[0][2].key, "2025-09-02");
assert.equal(partial.days[0].count, 2);
assert.equal(partial.days[364].count, 7);
assert.equal(partial.days[1].count, 0, "sparse dates must become empty cells");
assert.equal(partial.monthSegments[0].name, "Sep");
assert.equal(partial.monthSegments[0].startColumn, 0,
  "first partial month must begin at the first week column");
assert.deepEqual(Object.assign({}, partial.monthSegments[partial.monthSegments.length - 1]),
  { name: "Sep", startColumn: 52, span: 1 });
assert.ok(partial.monthSegments.some(function (segment) { return segment.name === "Jan"; }),
  "year boundary must label January");

const leap = model("2024-03-01");
assert.equal(leap.days.length, 365);
assert.ok(leap.days.some(function (day) { return day.key === "2024-02-29"; }),
  "rolling range must retain leap day");
assert.equal(leap.monthSegments[leap.monthSegments.length - 1].startColumn +
  leap.monthSegments[leap.monthSegments.length - 1].span, leap.weeks.length,
  "final month must reach the final visible week after leap day");
assert.deepEqual(Array.from(heatmap.activityThresholds(aligned.days)), [0, 0, 0]);
assert.equal(heatmap.activityLevel(0, [0, 0, 0]), 0);
const bucketDays = [1, 2, 3, 4, 5, 6, 7, 1000].map(function (count) {
  return { count };
});
const thresholds = Array.from(heatmap.activityThresholds(bucketDays));
assert.deepEqual(thresholds, [2, 4, 6]);
assert.equal(heatmap.activityLevel(3, thresholds), 2,
  "a maximum outlier must not bleach ordinary active days");
assert.equal(heatmap.activityLevel(1000, thresholds), 4);
assert.equal(model("not-a-date"), null);

assert.match(index, /id="review-activity"[\s\S]*id="activity-table"[\s\S]*COLLECTION/);
assert.match(index, /<table id="activity-table"/);
assert.doesNotMatch(index, /id="collection-path"/);
assert.doesNotMatch(frontend, /collection-path/);
assert.doesNotMatch(index, /<canvas[^>]+activity|<svg[^>]+activity/i);
assert.doesNotMatch(appCss, /\.review-activity[^}]*display:\s*grid/i);
assert.match(frontend, /labels = \["", "Mon", "", "Wed", "", "Fri", ""\]/);
assert.match(frontend, /document\.createElement\("colgroup"\)/);
assert.match(frontend, /columnElement\.className = "activity-weekday-column"/);
assert.match(frontend, /columnElement\.className = "activity-week-column"/);
assert.match(frontend, /cell\.setAttribute\("colspan", String\(segment\.span\)\)/);
assert.match(frontend, /for \(row = 0; row < 7; \+\+row\)[\s\S]*cell\.className = "activity-weekday-label"[\s\S]*for \(column = 0; column < model\.weeks\.length; \+\+column\)[\s\S]*tableRow\.appendChild\(cell\)/);
assert.match(appCss, /\.activity-table \.activity-month\s*{[^}]*text-align:\s*center;[^}]*white-space:\s*nowrap;/);
assert.doesNotMatch(appCss, /\.activity-table \.activity-month\s*{[^}]*overflow:\s*visible;/);
assert.match(appCss, /\.activity-table \.activity-weekday-label\s*{[^}]*height:\s*11px;[^}]*text-align:\s*right;[^}]*vertical-align:\s*middle;/);
assert.match(appCss, /\.activity-square\s*{[^}]*box-sizing:\s*border-box;[^}]*width:\s*11px;[^}]*height:\s*11px;/);
assert.match(appCss, /@media \(max-width:\s*620px\)[\s\S]*\.activity-table \.activity-month-spacer-column, \.activity-table \.activity-month-spacer\s*{\s*width:\s*1px;[\s\S]*\.activity-square\s*{\s*width:\s*8px;\s*height:\s*8px;/);
const narrowHeatmapWidth = 26 + 53 * 8 + 55 * 1;
assert.ok(narrowHeatmapWidth < 600,
  "all 53 week columns, including today, must fit a 600px Kindle profile");
assert.match(frontend, /activity-today/);
assert.match(appCss, /\.activity-today\s*{[^}]*border:\s*2px/);
assert.match(appCss, /\.night-mode \.activity-level-4/);
assert.match(frontend, /request\("GET", "\/api\/review-activity"/);
assert.match(loadDecks, /loadReviewActivity\(\)/);
assert.match(httpServer, /request\.target == "\/api\/review-activity"/);
assert.match(collectionBackend, /FROM revlog/);
assert.match(collectionBackend, /type NOT IN \(4,5\)/);
assert.match(backend, /fn review_activity\(&mut self\)/);
assert.match(backend, /storage[\s\S]*\.db\(\)[\s\S]*FROM revlog/);
assert.match(backend, /type NOT IN \(4,5\)/);

// Deck navigation is local, and physical Undo remains available with no card.
assert.doesNotMatch(decks, /syncNow\(/);
assert.match(decks, /loadDecks\(\)/);
assert.doesNotMatch(loadDecks, /syncNow\(/);
assert.match(startup, /syncNow\(function \(\)/);
assert.ok(startup.indexOf('syncNow(function ()') < startup.indexOf('loadDecks();'));
assert.match(startup, /hide\(byId\("decks-view"\)\).*hide\(byId\("sync"\)\)/);
assert.match(close, /"POST", "\/api\/sync"/);
assert.match(close, /state\.syncInFlight = true;/);
assert.ok(close.indexOf('"/api/sync"') < close.indexOf('"/api/quit"'));
assert.match(close, /"\/api\/quit", "", function \(\) \{ closeInterface\(\); \}/);
assert.doesNotMatch(close, /setTimeout\(closeInterface/);
assert.match(pageButtons, /pageButtonMode === "normal" && action === "forward"/);
assert.match(pageButtons, /pageButtonMode === "reversed" && action === "backward"/);
assert.match(frontend, /function cardCanScrollUp\(\)[\s\S]*scrollTop > 8/);
assert.match(frontend, /function scrollCardBackward\(\)[\s\S]*scrollTop = Math\.max\(0,/);
assert.match(pageButtons, /if \(cardCanScrollUp\(\)\) scrollCardBackward\(\);[\s\S]*else undoAnswer\(\)/);

// Rapid input cannot overlap answer/undo/loading transitions.
assert.match(frontend, /function reviewerReadyForInput\(\)[\s\S]*state\.cardLoading[\s\S]*state\.answerInFlight[\s\S]*state\.undoInFlight[\s\S]*state\.syncInFlight/);
assert.match(frontend, /function deckListReadyForInput\(\)/);
assert.match(frontend, /function pageButtonInputReady\(\)/);
assert.match(pageButtons, /if \(deckListReadyForInput\(\)\)[\s\S]*pageScroll\(advance \? 1 : -1\)/);
assert.match(pageButtons, /code === 34 \? "forward" : "backward"/);
assert.match(pageButtons, /document\.addEventListener\("keydown", pageButtonKeyDown, false\)/);
assert.match(pageButtons, /document\.addEventListener\("keyup", pageButtonKeyUp, false\)/);
assert.match(pageButtons, /event\.preventDefault/);
assert.match(pageButtons, /event\.stopPropagation/);
assert.match(pageButtons, /pageButtonDownCode === code/);
assert.doesNotMatch(frontend, /\/api\/input|\/api\/simulator\/input|pollPageButtons|clearPhysicalInput/);
assert.match(simulatorJavascript, /ankinkSimulatorPageButton/);
assert.doesNotMatch(httpServer,
  /\/api\/input|\/api\/simulator\/input|gpiokey|\/dev\/input|input_event/);
assert.match(httpServer,
  /select\(highest \+ 1, &set, nullptr, nullptr, nullptr\)/);
{
  const calls = [];
  const listeners = {};
  const context = {
    Date,
    pageButtonMode: "normal",
    state: {deck: {}, card: {}, answerShown: false, cardLoading: false,
      answerInFlight: false, undoInFlight: false, cardActionInFlight: false,
      syncInFlight: false},
    byId: function (name) {
      return {className: name === "review-view" ? "" : "hidden"};
    },
    cardCanScrollDown: function () { return false; },
    cardCanScrollUp: function () { return context.canScrollUp; },
    scrollCardForward: function () { calls.push("scroll-card"); },
    scrollCardBackward: function () { calls.push("scroll-card-backward"); },
    showAnswer: function () { calls.push("show-answer"); },
    answer: function () { calls.push("answer"); },
    undoAnswer: function () { calls.push("undo"); },
    pageScroll: function (direction) { calls.push("scroll:" + direction); },
    document: {addEventListener: function (name, listener) { listeners[name] = listener; }},
    window: {event: null, location: {protocol: "file:"}},
    canScrollUp: true
  };
  function key(code) {
    return {keyCode: code, prevented: 0, stopped: 0,
      preventDefault: function () { this.prevented += 1; },
      stopPropagation: function () { this.stopped += 1; }};
  }
  vm.createContext(context);
  vm.runInContext(pageButtons, context);
  const forward = key(34);
  listeners.keydown(forward);
  listeners.keydown(key(34));
  assert.deepEqual(calls, ["show-answer"], "held/repeated keydown activates once");
  assert.equal(forward.prevented, 1);
  assert.equal(forward.stopped, 1);
  listeners.keyup(key(34));
  listeners.keydown(key(33));
  assert.deepEqual(calls, ["show-answer", "scroll-card-backward"],
    "backward scrolls toward the top before undoing");
  listeners.keyup(key(33));
  context.canScrollUp = false;
  listeners.keydown(key(33));
  assert.deepEqual(calls, ["show-answer", "scroll-card-backward", "undo"],
    "backward undoes once the card is at the top");
  listeners.keyup(key(33));
  context.pageButtonMode = "reversed";
  listeners.keydown(key(33));
  assert.deepEqual(calls, ["show-answer", "scroll-card-backward", "undo", "show-answer"],
    "reversed mode maps backward to the forward review action");
}
assert.match(frontend, /hide\(byId\("sync"\)\).*show\(byId\("review-view"\)\)/);
assert.match(loadDecks, /show\(byId\("sync"\)\)/);
assert.match(appCss, /#sync\.hidden\s*{[^}]*display:\s*none;/);
assert.match(undo, /if \(state\.cardLoading \|\| state\.answerInFlight \|\| state\.undoInFlight \|\| state\.cardActionInFlight \|\| state\.syncInFlight\) return;/);
assert.match(undo, /state\.undoInFlight = true;/);
assert.match(loading, /state\.cardLoading = false/);
assert.match(loading, /prepareShowAnswerControl\(\)/);
assert.match(frontend, /var RATING_ERASE_MS = 400;/);
assert.match(frontend, /function beginRatingErase\(\)[\s\S]*ratings\.className \+= " rating-erasing";[\s\S]*ratings\.offsetHeight;[\s\S]*ratingEraseStartedAt = new Date\(\)\.getTime\(\)/);
assert.match(answer, /beginRatingErase\(\);[\s\S]*state\.operationEpoch \+= 1/);
assert.match(answer, /if \(error \|\| !result \|\| result\.type === "error"\)[\s\S]*ratingEraseStartedAt = 0;[\s\S]*removeRatingEraseClass\(\);[\s\S]*show\(byId\("rating-controls"\)\)/);
assert.match(controlRepaint, /show\(controls\);[\s\S]*hide\(button\);[\s\S]*if \(!ratingEraseStartedAt\)/);
assert.match(controlRepaint, /remaining = Math\.max\(0, RATING_ERASE_MS - elapsed\);[\s\S]*window\.setTimeout\(function \(\)[\s\S]*hide\(ratings\)[\s\S]*removeRatingEraseClass\(\)[\s\S]*show\(button\)[\s\S]*}, remaining\)/);
assert.match(appCss, /\.ratings\.rating-erasing button\s*{[^}]*background:\s*#fff;[^}]*border-color:\s*#fff;[^}]*color:\s*#fff;/);
assert.match(appCss, /\.night-mode \.ratings\.rating-erasing button\s*{[^}]*background:\s*#000;[^}]*border-color:\s*#000;[^}]*color:\s*#000;/);

// A fixed 88px card-actions button sits beside either equal-width review
// control bar. Its ES5 menu dispatches official, generation-bound card actions.
assert.match(index, /id="card-actions-button"[^>]*>&#xE6FA;<\/button>/);
assert.match(index, /id="card-actions-menu"[\s\S]*>Undo<\/button>[\s\S]*>Bury card<\/button>[\s\S]*>Suspend card<\/button>[\s\S]*>Flag \(Red\)<\/button>/);
assert.match(index, /class="review-title"[\s\S]*id="card-flag"[^>]*>&#x2691;<\/div>[\s\S]*id="session-count"/);
assert.match(appCss, /\.controls\s*{[^}]*left:\s*0;[^}]*width:\s*100%;[^}]*height:\s*112px;[^}]*padding:\s*12px 0 12px 100px;/);
assert.match(appCss, /\.card-actions-button\s*{[^}]*width:\s*88px;[^}]*height:\s*88px;[^}]*font:[^}]*"Code2000"/);
assert.match(appCss, /\.card-flag\s*{[^}]*width:\s*46px;[^}]*font:[^}]*"Code2000"/);
assert.match(cardActions, /request\("POST", "\/api\/card-action"/);
assert.match(cardActions, /card=" \+ encodeURIComponent\(cardId\)[\s\S]*&token=" \+ encodeURIComponent\(reviewToken\)[\s\S]*&action=" \+ encodeURIComponent\(action\)/);
assert.match(frontend, /function updateCardFlag\(flag\)[\s\S]*flag === 1 \? "&#x2691;" : "&#x2690;"/);
assert.match(cardActions, /if \(action === "flag-red"\)[\s\S]*state\.card\.flag = parseInt\(result\.flag, 10\) \|\| 0;[\s\S]*updateCardFlag\(state\.card\.flag\)[\s\S]*"Card unflagged\."[\s\S]*return;[\s\S]*nextCard\(\)/);
assert.match(loading, /updateCardFlag\(0\)[\s\S]*updateCardFlag\(card\.flag\)/);
assert.match(httpServer, /request\.target == "\/api\/card-action"/);
assert.match(rustCardAction, /pending\.id\.0 != card_id \|\| pending\.token != review_token/);
assert.match(rustCardAction, /BuryOrSuspendMode::BuryUser/);
assert.match(rustCardAction, /BuryOrSuspendMode::Suspend/);
assert.match(rustCardAction, /let flag = if current_flag == 1 \{ 0 \} else \{ 1 \};[\s\S]*set_card_flag\(&\[pending_id\], flag\)[\s\S]*Some\(flag\)/);
assert.match(backend, /get_card\(anki_proto::cards::CardId \{ cid: id\.0 \}\)[\s\S]*\.flags[\s\S]*& 0b111[\s\S]*"flag": flag/);
assert.equal((rustCardAction.match(/self\.pending = None/g) || []).length, 2,
  "bury and suspend must retire the pending card, while flagging must not");

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
assert.match(frontend, /nightCardMode = "standard"/);
assert.match(frontend, /nightCardMode !== "palette"[\s\S]*nightCardMode !== "palette-images"[\s\S]*nightCardMode = "standard"/);
assert.match(frontend, /saveSetting\("nightCardMode", nightCardMode\)/);
assert.match(frontend, /_ankinkNightOriginalStyle/);
assert.match(frontend, /getImageData\(0, 0, width, height\)/);
assert.match(frontend, /data\[i\] = 255 - data\[i\]/);
assert.doesNotMatch(frontend, /style\.(?:webkitFilter|filter)\s*=/);
assert.match(frontend, /image\.setAttribute\("src", API \+ "\/api\/media\/"/);
assert.match(frontend, /images\[i\]\.onerror = function/);
assert.match(mediaLoading, /image\.setAttribute\("src", API \+ "\/api\/media\/"/);
assert.doesNotMatch(mediaLoading, /api\/media-data/);

// The backend sends final HTML-only KaTeX markup. Mesquite never loads or
// executes KaTeX JavaScript; it only lazily repairs nearby legacy layout bugs.
assert.doesNotMatch(frontend, /window\.katex|katex\.render|loadKatex|katexState|data-expr/);
assert.match(index, /vendor\/katex\/katex\.min\.css\?v=0\.16\.25-native/);
assert.ok(index.indexOf("vendor/katex/katex.min.css") < index.indexOf("app.css"),
  "application compatibility CSS must load after KaTeX CSS");
assert.equal(fs.existsSync(katexJavascript), false);
assert.doesNotMatch(loadDecks, /deferKatexLoad/);

// Only the Mesquite-verified WOFF KaTeX fonts may be advertised or shipped.
const fontReferences = [...katexCss.matchAll(/fonts\/([^)'"]+\.(?:woff2?|ttf))/g)]
  .map((match) => match[1]);
assert.equal(fontReferences.length, 20);
assert.equal(new Set(fontReferences).size, 20);
assert.ok(fontReferences.every((name) => name.endsWith(".woff")),
  "Mesquite-verified WOFF must be the only advertised font format");
for (const name of fontReferences) {
  assert.ok(fs.existsSync(`${fontDirectory}/${name}`), `missing KaTeX font ${name}`);
}
for (const family of ["KaTeX_AMS", "KaTeX_Main", "KaTeX_Math",
  "KaTeX_Size1", "KaTeX_Size2", "KaTeX_Size3", "KaTeX_Size4"]) {
  assert.match(katexCss, new RegExp(`font-family:["']?${family}["']?`));
}

// Mesquite repairs: reconstruct only ordinary scripts, then restore every
// native KaTeX two-row vlist baseline generically instead of rebuilding
// fractions, operator limits, radicals, or matrices one at a time.
assert.match(frontend, /function repairKindleScripts\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleFractions\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleLimits\(root\)/);
assert.match(frontend, /function isInsideMathStructure\(node, className\)/);
assert.match(frontend, /repairKindleScripts\(root\)[\s\S]*?isInsideMathStructure\(node, "mfrac"\)/);
assert.match(frontend, /function repairKindleVlistBaselines\(root\)/);
assert.match(frontend, /getElementsByClassName\("vlist-t2"\)/);
assert.match(frontend, /\.style\.verticalAlign = "-" \+ depth \+ "em"/);
assert.match(frontend, /function repairKindleMath\(root\)[\s\S]*?kindleMathLayout\(\)[\s\S]*?repairKindleScripts\(root\);[\s\S]*?repairKindleVlistBaselines\(root\);[\s\S]*?\n\s*\}/);
assert.match(appCss, /\.katex \.mfrac \.frac-line\s*{\s*border-bottom-width:\s*2px;/);
assert.doesNotMatch(appCss, /frac-line\s*{[^}]*!important/);
assert.doesNotMatch(appCss, /ankink-(?:fraction|frac-)/);
assert.match(frontend, /function repairMathNearViewport\(\)/);
assert.match(frontend, /root\.scrollTop \+ root\.clientHeight \* 2\.5/);
assert.match(frontend, /repaired >= 16/);
assert.match(frontend, /collectCardMath\(\)[\s\S]*scheduleMathRepair\(0\)/);
assert.match(backend, /"front": render_card_html\(&rendered\.question\(\)\)/);
assert.match(backend, /"back": render_card_html\(&rendered\.answer\(\)\)/);
assert.match(frontend, /function loadAboutLogo\(\)[\s\S]*getAttribute\("data-src"\)/);
assert.match(frontend, /function openAbout\(\)[\s\S]*loadAboutLogo\(\)[\s\S]*playAboutLogoAnimation\(\)/);

// The card font is allowlisted, persistent, and applied only to card faces.
assert.match(frontend, /cardFont = "Bookerly"/);
assert.match(frontend, /"Caecilia": '\"Caecilia Regular\"/);
assert.match(frontend, /"Caecilia Condensed": 'condensed,/);
assert.match(frontend, /"Helvetica": '\"Helvetica Neue LT\"/);
assert.match(frontend, /saveSetting\("cardFont", cardFont\)/);
assert.match(frontend, /byId\("front"\)\.style\.setProperty\("font-family", family, "important"\)/);
assert.match(frontend, /byId\("back-face"\)\.style\.setProperty\("font-family", family, "important"\)/);
assert.match(frontend, /function applyCardFont[\s\S]*restoreNightPalette\(byId\("front"\)\)[\s\S]*applyNightCardAppearance\(\)/);

// Font-size previews share the selected card face and the header controls use
// the same discrete, persistent scale choices.
assert.match(frontend, /fontScales = \[0\.7, 0\.8, 0\.9, 1, 1\.1, 1\.25, 1\.4, 1\.6\]/);
assert.match(frontend, /getElementsByName\("card-font-size"\)/);
assert.match(frontend, /style\.setProperty\("font-family", family, "important"\)/);
assert.match(frontend, /byId\("font-plus"\)\.onclick = function \(\) \{ changeFontScale\(1\); \}/);
assert.match(frontend, /byId\("font-minus"\)\.onclick = function \(\) \{ changeFontScale\(-1\); \}/);

// Explanatory settings copy is revealed on demand by touch-friendly help
// buttons instead of permanently occupying space in the dialog.
assert.match(frontend, /getElementsByClassName\("setting-help"\)/);
assert.match(frontend, /function showSettingsTooltip\(button\)/);
assert.match(frontend, /button\.getAttribute\("data-help"\)/);
assert.match(frontend, /byId\("settings-dialog"\)\.onscroll = hideSettingsTooltip/);
assert.match(frontend, /event\.stopPropagation\(\)/);
assert.match(frontend, /document\.onclick = function \(\) \{ hideSettingsTooltip\(\); hideCardActionsMenu\(\); \};/);

console.log("reviewer contract tests passed");
