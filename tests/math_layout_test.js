"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const {spawnSync} = require("node:child_process");

const source = fs.readFileSync(process.argv[2], "utf8");
const generated = spawnSync(process.argv[3], [], {
  input: fs.readFileSync(process.argv[4], "utf8"), encoding: "utf8"
});
assert.equal(generated.status, 0, generated.stderr);
const fixtures = JSON.parse(generated.stdout);

// Minimal structural DOM for the actual native HTML. Layout is verified in
// firmware separately; these tests enforce which geometry the repair changes.
function element(tag, attributes = {}) {
  const node = {nodeType: 1, tagName: tag, attributes, childNodes: [],
    className: attributes.class || "", style: {}};
  for (const declaration of (attributes.style || "").split(";")) {
    const colon = declaration.indexOf(":");
    if (colon < 0) continue;
    const property = declaration.slice(0, colon).trim()
      .replace(/-([a-z])/g, (_, letter) => letter.toUpperCase());
    node.style[property] = declaration.slice(colon + 1).trim();
  }
  node.getElementsByClassName = function(name) {
    return descendants(this).filter(child => child.nodeType === 1 &&
      child.className.split(/\s+/).includes(name));
  };
  return node;
}
function descendants(node) {
  return node.childNodes.flatMap(child => [child, ...descendants(child)]);
}
function parse(html) {
  const root = element("div"), stack = [root];
  for (const token of html.match(/<[^>]*>|[^<]+/g) || []) {
    if (token.startsWith("</")) {
      assert.ok(stack.length > 1, "balanced native HTML");
      const closed = stack.pop();
      assert.equal(closed.tagName, token.slice(2, -1));
    } else if (token.startsWith("<")) {
      const tag = /^<([a-z0-9]+)/i.exec(token)[1];
      const attributes = Object.fromEntries(
        [...token.matchAll(/([\w-]+)="([^"]*)"/g)].map(m => [m[1], m[2]])
      );
      const node = element(tag, attributes);
      node.parentNode = stack.at(-1);
      node.parentNode.childNodes.push(node);
      if (!token.endsWith("/>")) stack.push(node);
    } else {
      stack.at(-1).childNodes.push({nodeType: 3, text: token,
        childNodes: [], parentNode: stack.at(-1)});
    }
  }
  assert.equal(stack.length, 1, "balanced native HTML");
  return root;
}
function structure(node) {
  const style = {...node.style};
  delete style.verticalAlign; // the only permitted repair
  return {tag: node.tagName, attributes: node.attributes, className: node.className,
    text: node.text, style, children: node.childNodes.map(structure)};
}
const repair = source.slice(source.indexOf("  function directSpans(node)"),
  source.indexOf("  function mathTop(node, root)"));
const context = {window: {kindle: {}, location: {search: ""}}};
vm.createContext(context);
vm.runInContext(repair, context);

let corrected = 0;
for (const fixture of fixtures) {
  assert.doesNotMatch(fixture.html, /math-error/);
  assert.match(fixture.html, /class="katex/);
  const root = parse(fixture.html), before = structure(root);
  const nodes = descendants(root);
  const styles = nodes.map(node => ({...node.style}));
  const scripts = root.getElementsByClassName("msupsub");
  const tables = root.getElementsByClassName("vlist-t2");
  const expected = tables.map(table => {
    const rows = table.childNodes.filter(child => child.nodeType === 1 &&
      child.className === "vlist-r");
    const cells = rows.at(-1).childNodes.filter(child => child.nodeType === 1 &&
      child.className === "vlist");
    return [table, parseFloat(cells[0].style.height)];
  });
  context.repairKindleMath(root);
  assert.deepEqual(structure(root), before,
    `${fixture.name}: preserve heights, tops, widths, margins, sizing and glyphs`);
  const afterNodes = descendants(root);
  assert.equal(afterNodes.length, nodes.length);
  for (let i = 0; i < nodes.length; i++) {
    assert.equal(afterNodes[i], nodes[i],
      `${fixture.name}: preserve DOM node identity and order`);
    const ownDepth = expected.find(([table]) => table === nodes[i]);
    const expectedStyle = {...styles[i]};
    if (ownDepth) expectedStyle.verticalAlign = `-${ownDepth[1]}em`;
    assert.deepEqual({...nodes[i].style}, expectedStyle,
      `${fixture.name}: only the owning table's baseline may change`);
  }
  assert.deepEqual(root.getElementsByClassName("msupsub"), scripts);
  for (const [table, depth] of expected) {
    assert.ok(depth > 0 && Number.isFinite(depth), fixture.name);
    assert.equal(table.style.verticalAlign, `-${depth}em`,
      `${fixture.name}: use this table's own encoded depth`);
    corrected++;
  }
  for (const table of root.getElementsByClassName("vlist-t")) {
    if (!tables.includes(table)) {
      assert.equal(table.style.verticalAlign, undefined,
        `${fixture.name}: nested depth must not shift a one-row parent`);
    }
  }
  context.repairKindleMath(root);
  for (const [table, depth] of expected)
    assert.equal(table.style.verticalAlign, `-${depth}em`,
      `${fixture.name}: repeated repair cannot accumulate offsets`);

  const host = parse(fixture.html), hostBefore = structure(host);
  context.window.kindle = null;
  context.repairKindleMath(host);
  assert.deepEqual(structure(host), hostBefore);
  assert.ok(host.getElementsByClassName("vlist-t2").every(t =>
    t.style.verticalAlign === undefined), "ordinary host math stays native");
  context.window.kindle = {};
}
assert.equal(fixtures.length, 46, "page equations, deeper nesting and all Anki delimiters");
assert.ok(corrected > 100, "exercise many independently encoded nested stacks");

const exponent = parse(fixtures.find(f => f.name === "nested-exponent").html);
const exponentScripts = exponent.getElementsByClassName("msupsub");
assert.equal(exponentScripts.length, 3, "s_k, outer exponent and inner f_k");
assert.equal(exponentScripts[1].childNodes[0].className, "vlist-t",
  "the outer exponent has no depth row");
assert.equal(exponentScripts[1].getElementsByClassName("vlist-t2").length, 1,
  "its descendant depth row belongs to f_k, not to e's exponent");

// Broken/absent depth data is ignored; a descendant cannot supply it.
for (const height of ["", "garbage", "NaNem", "Infinityem", "0em", "-1em"]) {
  const root = parse('<span class="vlist-t vlist-t2">' +
    '<span class="vlist-r"><span class="vlist" style="height:1em;"></span></span>' +
    '<span class="vlist-r"><span class="vlist" style="height:' + height +
    ';"></span></span></span>');
  context.repairKindleMath(root);
  assert.equal(root.childNodes[0].style.verticalAlign, undefined);
}
const missing = parse('<span class="vlist-t vlist-t2"><span class="vlist-r">' +
  '<span class="vlist"><span class="vlist-t vlist-t2">' +
  '<span class="vlist-r"><span class="vlist"></span></span>' +
  '<span class="vlist-r"><span class="vlist" style="height:0.3em;"></span></span>' +
  '</span></span></span></span>');
context.repairKindleMath(missing);
assert.equal(missing.childNodes[0].style.verticalAlign, undefined);
assert.equal(missing.getElementsByClassName("vlist-t2")[1].style.verticalAlign, "-0.3em");

context.window.kindle = null;
context.window.location.search = "?mesquite=1";
context.repairKindleMath(exponent);
assert.equal(exponentScripts[0].childNodes[0].style.verticalAlign, "-0.15em",
  "explicit Mesquite diagnostic mode retains the repair");
console.log(`Native math layout: ${fixtures.length} cases, ${corrected} depth rows passed`);
