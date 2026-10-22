"use strict";
// Synthetic CSS/DOM only: personal cards and collections never enter fixtures.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const source = fs.readFileSync(process.argv[2], "utf8");
const helpers = source.slice(source.indexOf("  /* CARD_CSS_BEGIN */"), source.indexOf("  /* CARD_CSS_END */"));
const elements = {}, matching = {};
function element(tag = "div") {
  const node = {tagName: tag.toUpperCase(), nodeType: 1, attributes: {}, childNodes: [],
    style: {calls: [], setProperty(name, value, priority) { this.calls.push(name); this[name] = value; this[name + "Priority"] = priority; }},
    getAttribute(name) {return this.attributes[name] || "";},
    setAttribute(name, value) {this.attributes[name] = value;},
    appendChild(child) {
      if (child.parentNode) child.parentNode.childNodes.splice(child.parentNode.childNodes.indexOf(child), 1);
      this.childNodes.push(child); child.parentNode = this; return child;
    },
    getElementsByTagName() {return this.childNodes.flatMap(child => [child, ...child.getElementsByTagName()]);}
  };
  Object.defineProperties(node, {
    firstChild: {get() {return this.childNodes[0] || null;}},
    nextSibling: {get() {return this.parentNode.childNodes[this.parentNode.childNodes.indexOf(this) + 1] || null;}}
  });
  return node;
}
const head = element("head");
const context = {document: {createElement: element, createTextNode: text => ({text}),
  getElementsByTagName: () => [head], querySelectorAll: selector => matching[selector] || []},
  window: {matchMedia: query => ({matches: query.includes("900px")})},
  byId: id => elements[id], clear: node => {node.childNodes = [];}};
vm.createContext(context); vm.runInContext(helpers, context);
const scope = "#card #back-face";
const rules = css => Array.from(context.cardCssRules(css, scope));
const css = rules(`
 @import "https://invalid.example/style.css";
 .panel { --Ink: #123; --ink: #456; color:var(--Ink); border:1px solid var(--ink) }
 .panel :is(.a,.b), .panel [title="a,b"] { content:"x;y"; color:rgb(1,2,3) }
 @media (max-width:900px) {.panel {padding:11px} @media (orientation:portrait) {.a{color:red}}}
 @font-face{font-family:remote;src:url(https://invalid.example/font)}
`);
assert.equal(css.length, 5);
assert.equal(css[0].selector, scope + " .panel");
assert.match(css[0].body, /--Ink:#123;--ink:#456/);
assert.match(css[1].body, /content:"x;y"/);
assert.equal(css[3].media[0].trim(), "(max-width:900px)");
assert.equal(css[4].media.length, 2);
assert.equal(rules('.card + button {color:red} #card ~ * {display:none}').length, 0);
assert.equal(rules('#card-other {color:red}')[0].selector, scope + ' #card-other');
assert.equal(context.cardCssRules('#card-other{color:red}', '#card')[0].selector, '#card #card-other');
assert.equal(rules('body .panel {color:red}')[0].selector, scope + ' .panel');
assert.equal(rules('.bad\\78 {color:red}').length, 0);
const unsafe = String.raw`background:url (https://invalid.example);color:red;border-image:u\72l(x);position:fixed;z-index:900;--bad:expression(x);--remote:url(x);width:10px`;
assert.equal(context.safeInlineStyle(unsafe), "color:red;width:10px");
assert.equal(context.resolveCardVars('1px solid var(--line, rgb(1,2,3))', {}), '1px solid  rgb(1,2,3)');
assert.equal(context.resolveCardVars('var(--a)', {'--a':'var(--b)', '--b':'#123'}), '#123');
assert.equal(context.resolveCardVars('var(--a, red)', {'--a':'var(--b)', '--b':'var(--a)'}), ' red');
assert.equal(context.resolveCardVars('var(--missing)', {}), '');

// Ordinary styles, local variables, descendants and pseudos on old WebKit.
const face = element(), panel = element(), box = element(), inline = element();
face.id = "back-face"; face.appendChild(panel); panel.appendChild(box); panel.appendChild(inline);
inline.setAttribute('style', '--ink:#789;color:var(--ink)');
matching[scope + ' .panel'] = [panel]; matching[scope + ' .box'] = [box];
matching[scope + ' .focus'] = [box]; matching[scope + ' .inline'] = [inline];
context.legacyCardStyles(face, rules(`
.panel {--ink:#123;--line:#456;color:var(--ink)}
.box {border:1px solid var(--line);color:var(--ink);display:flex;flex:1}
.focus {border-width:2px}
.inline {color:red!important}
.box:before {content:'a;b';border-left:1px solid var(--line)}
@media(max-width:900px){.box{padding:11px}}
@media(max-width:480px){.box{padding:2px}}
`));
assert.equal(panel.style.color, '#123');
assert.equal(box.style.border, '1px solid #456');
assert.equal(box.style['border-width'], '2px');
assert.ok(box.style.calls.indexOf('border') < box.style.calls.indexOf('border-width'));
assert.equal(box.style.display, '-webkit-box');
assert.equal(box.style.webkitBoxFlex, '1');
assert.equal(box.style.padding, '11px');
assert.equal(inline.style.color, 'red');
assert.equal(inline.style.colorPriority, 'important');
assert.match(head.childNodes[0].childNodes[0].text, /:before\{content:'a;b';border-left:1px solid #456\}/);

// Repeated shorthand declarations must retain declaration order within a rule.
box.style.calls = [];
context.legacyCardStyles(face, rules('.box{border:1px solid var(--line,#123);border-width:2px;border:3px solid var(--line,#123)}'));
assert.ok(box.style.calls.indexOf('border-width') < box.style.calls.indexOf('border'));
assert.equal(box.style.border, '3px solid #123');

// Template variables on the actual card root reach each independently scoped face.
const cardRoot = element(); cardRoot.appendChild(face); elements.card = cardRoot;
matching['#card'] = [cardRoot];
context.legacyCardStyles(face, Array.from(context.cardCssRules('.card{--root:#abc}', '#card')).concat(rules('.box{background:var(--root)}')));
assert.equal(box.style.background, '#abc');

// Equal-column CSS grids retain order and content using ordinary CSS tables.
const grid = element(), cells = [element(), element(), element(), element(), element()];
cells.forEach(cell => grid.appendChild(cell));
context.legacyCardLayout(grid, {display:{value:'grid'}, 'grid-template-columns':{value:'repeat(2,minmax(0,1fr))'}, gap:{value:'7px'}}, {});
const table = grid.firstChild;
assert.equal(table.style.borderSpacing, '7px');
assert.equal(table.childNodes.length, 3);
assert.deepEqual(table.childNodes.flatMap(row => row.childNodes), cells);
assert.equal(cells[0].style.display, 'table-cell');
assert.equal(cells[0].style.width, '50%');
// Front and back always have different anchors; a later card replaces CSS.
context.writeCardCss('test-style', rules('.panel{color:red}'));
const sheet = head.childNodes.at(-1); elements['test-style'] = sheet;
context.writeCardCss('test-style', []);
assert.equal(sheet.childNodes[0].text, '');
assert.match(source, /if \(child\.tagName === "STYLE"\) embeddedCss\.push/);
assert.match(source, /cardCssRules\(embeddedCss\.join\("\\n"\), "#card #" \+ element\.id\)/);
assert.doesNotMatch(source, /\b(?:let|const)\s+|=>|\bfetch\s*\(/);

const path = require("node:path");
const assetDir = path.dirname(process.argv[2]);
assert.match(fs.readFileSync(path.join(assetDir, 'index.html'), 'utf8'), /class="math-font-warmup katex" aria-hidden="true"><span class="mathbb">E<\/span>/);
assert.match(fs.readFileSync(path.join(assetDir, 'app.css'), 'utf8'), /\.math-font-warmup\s*\{[^}]*left:\s*-10000px/);

console.log('Card CSS: scoped embedded styles, media, safe values, variables, cascade and legacy layouts passed');
