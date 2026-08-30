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

function between(source, start, end) {
  const first = source.indexOf(start);
  const last = source.indexOf(end, first + start.length);
  assert.notEqual(first, -1, `missing ${start}`);
  assert.notEqual(last, -1, `missing ${end}`);
  return source.slice(first, last);
}

const undo = between(frontend, "function undoAnswer()", "function cardCanScrollDown()");
const polling = between(frontend, "function pollPageButtons()", 'byId("show-answer")');
const inputWaiting = between(frontend, "var inputWaitTimer", 'byId("show-answer")');
const decks = between(frontend, 'byId("back").onclick', 'byId("refresh").onclick');
const answer = between(frontend, "function answer(rating)", "function showAnswer()");
const loading = between(frontend, "function nextCard()", "function isReviewStateError");
const loadDecks = between(frontend, "function loadDecks()", "function applyFontScale(");
const startup = between(frontend, "function startApplication()", "function loadDecks()");
const sync = between(frontend, "function syncNow(done)", "function nextCard()");
const close = between(frontend, "function closeApplication()", "function warning(message)");
const mediaLoading = between(frontend, "function loadMediaImage", "function answerOnly");
const rustAnswer = between(backend, "fn answer(&mut self", "fn undo(&mut self)");
const rustUndo = between(backend, "fn undo(&mut self)", "fn login(&mut self");

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
assert.match(index, /id="sync"[\s\S]*id="rotation"[\s\S]*id="refresh"/);
const headerActions = index.match(/<div class="header-actions">([\s\S]*?)<\/div>/)[1];
assert.equal([...headerActions.matchAll(/<button\b/g)].length, 7,
  "header must contain exactly seven uniformly sized action buttons");
assert.match(index, /id="busy-indicator" class="busy-indicator"/);
assert.match(appCss, /\.header-actions > button, \.font-controls button\s*{[^}]*width:\s*66px;[^}]*height:\s*66px;/);
assert.match(appCss, /\.header-actions\s*{[^}]*width:\s*492px;/);
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
vm.runInContext(heatmapMatch[1], heatmap);
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
assert.equal(partial.months[0], "Sep");
assert.ok(partial.months.includes("Jan"), "year boundary must label January");

const leap = model("2024-03-01");
assert.equal(leap.days.length, 365);
assert.ok(leap.days.some(function (day) { return day.key === "2024-02-29"; }),
  "rolling range must retain leap day");
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
assert.doesNotMatch(polling, /!state\.card\s*\|\|\s*!input\.action/);
assert.match(polling, /pageButtonMode === "normal" && input\.action === "forward"/);
assert.match(polling, /pageButtonMode === "reversed" && input\.action === "backward"/);
assert.match(polling, /if \(!state\.card\)[\s\S]*return;[\s\S]*else undoAnswer\(\)/);

// Rapid input cannot overlap answer/undo/loading transitions.
assert.match(inputWaiting, /function reviewerReadyForInput\(\)[\s\S]*state\.cardLoading[\s\S]*state\.answerInFlight[\s\S]*state\.undoInFlight[\s\S]*state\.syncInFlight/);
assert.match(polling, /if \(state\.inputBusy \|\| !pageButtonInputReady\(\)\) return;[\s\S]*state\.inputBusy = true;/);
assert.match(polling, /pollEpoch !== state\.inputEpoch/);
assert.match(polling, /request\("GET", "\/api\/input"/);
assert.match(polling, /resumePageButtonInput\(\)/);
assert.doesNotMatch(frontend, /setInterval\(pollPageButtons/);
assert.match(inputWaiting, /function deckListReadyForInput\(\)/);
assert.match(inputWaiting, /function pageButtonInputReady\(\)/);
assert.match(polling, /if \(deckListReadyForInput\(\)\)[\s\S]*pageScroll\(input\.action === "forward" \? 1 : -1\)/);
assert.match(frontend, /hide\(byId\("sync"\)\).*show\(byId\("review-view"\)\)/);
assert.match(loadDecks, /show\(byId\("sync"\)\)/);
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
assert.match(frontend, /document\.onclick = hideSettingsTooltip/);

console.log("reviewer contract tests passed");
