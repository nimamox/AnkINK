"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const path = require("node:path");
const source = fs.readFileSync(process.argv[2], "utf8");
const ankink = process.argv[3] === "ankink";
const marked = source.slice(source.indexOf("/* NATIVE_NIGHT_MODE_BEGIN */"),
  source.indexOf("/* NATIVE_NIGHT_MODE_END */"));
const button = {disabled: false, attributes: {}, setAttribute: function(key, value) { this.attributes[key] = value; }};
let requests = [], warnings = [];
const context = {
  nightKnown: false, nightRequestInFlight: false, nightMode: false, night: false,
  byId: () => button, id: () => button,
  warning: value => warnings.push(value),
  request: (method, path, body, callback) => requests.push({method, path, body, callback})
};
vm.createContext(context); vm.runInContext(marked, context);
context.toggleNightMode(); context.toggleNightMode();
assert.equal(requests.length, 1, "one in-flight request; do not race toggles");
assert.equal(requests[0].method, "GET");
assert.equal(button.disabled, true);
// Outside hardware state overrides the stale local day preference.
requests[0].callback(null, {nightKnown: true, nightMode: true});
assert.equal(requests[1].path, "/api/night-mode");
assert.equal(requests[1].body, "value=0", "explicit opposite of actual hardware state");
assert.equal(button.innerHTML, "&#9788;");
requests[1].callback(null, {nightKnown: true, nightMode: false});
assert.equal(button.disabled, false);
assert.equal(context[ankink ? "nightMode" : "night"], false);
assert.equal(button.innerHTML, "&#9789;");
assert.equal(button.attributes["aria-pressed"], "false");
requests = [];
context.toggleNightMode();
requests[0].callback(null, {nightKnown: true, nightMode: false});
assert.equal(requests[1].body, "value=1");
requests[1].callback("refresh failed", {nightKnown: true, nightMode: true});
assert.equal(context[ankink ? "nightMode" : "night"], true, "refresh failure still synchronizes actual changed display state");
assert.equal(button.innerHTML, "&#9788;");
assert.equal(warnings.at(-1), "refresh failed");
requests = [];
context.toggleNightMode();
requests[0].callback(null, {nightKnown: false, nightError: "read failed"});
assert.equal(requests.length, 1, "unknown state prevents a blind write");
assert.equal(button.disabled, false);
assert.equal(button.innerHTML, "?");
assert.equal(warnings.at(-1), "read failed");
context.syncNightState({message: "ordinary backend failure"});
assert.equal(context.nightKnown, false, "generic response cannot invent a hardware state");
requests = [];
context.toggleNightMode();
requests[0].callback(null, {nightNative: false, nightKnown: false, nightMode: false, nightError: "Native Night Mode is unavailable in the host simulator"});
assert.equal(requests.length, 1, "simulator has no software inversion fallback");
assert.equal(button.innerHTML, "?");
assert.match(warnings.at(-1), /simulator/);
requests = [];
context.toggleNightMode();
requests[0].callback("network failed", null);
assert.equal(requests.length, 1);
assert.equal(button.disabled, false);
assert.equal(warnings.at(-1), "network failed");

// No legacy transformations, settings, logo variants or dark CSS remain.
const dir = path.dirname(process.argv[2]);
const css = fs.readFileSync(path.join(dir, "app.css"), "utf8");
const html = fs.readFileSync(path.join(dir, "index.html"), "utf8");
assert.doesNotMatch(source, /night(?:Page|Card)Mode|nativeNightMode|nightFriendlyColor|colorLuminance|restoreNight|applyNightPalette|invertNightImage|NightOriginal|NightStyle|nightStyledElements|getImageData|putImageData|data-night-src/);
assert.doesNotMatch(css, /\.night-mode|filter:\s*invert/);
assert.doesNotMatch(html, /night-(?:page|card)-mode|data-night-src|logo_night/);
// Canvas is still needed for the About animation and Potion busy indicator.
assert.match(source, /function drawAboutLogoClean/);
console.log("Native-only Night Mode button synchronization, errors and cleanup passed");
