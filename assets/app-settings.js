(function () {
  "use strict";
  var API = "http://127.0.0.1:8765";
  var MEDIA_VERSION = String(new Date().getTime());
  var state = { deck: null, card: null, reviewed: 0, answerShown: false, inputBusy: false,
    cardLoading: false, answerInFlight: false, undoInFlight: false,
    syncInFlight: false, inputEpoch: 0 };
  var warningTimer = null;
  var settingWrites = [], settingWriteInFlight = false, settingsDrainedCallback = null;
  var pendingReviews = 0;
  var aboutLogoTimer = null, aboutLogoReady = false, aboutLogoPending = false;
  var katexState = 0, katexStyleDone = false, katexScriptDone = false;
  var katexCallbacks = [], katexDeferredTimer = null;
  var fontScales = [0.7, 0.8, 0.9, 1, 1.1, 1.25, 1.4, 1.6];
  function nearestFontScale(value) {
    var nearest = fontScales[0], distance = Math.abs(value - nearest), i, candidateDistance;
    if (!isFinite(value)) return 1;
    for (i = 1; i < fontScales.length; ++i) {
      candidateDistance = Math.abs(value - fontScales[i]);
      if (candidateDistance < distance) { nearest = fontScales[i]; distance = candidateDistance; }
    }
    return nearest;
  }
  var fontScale = 1;
  var cardFonts = {
    "Amazon Ember": '"Amazon Ember", Arial, sans-serif',
    "Baskerville": 'Baskerville, Georgia, serif',
    "Bookerly": 'Bookerly, Georgia, serif',
    "Caecilia": '"Caecilia Regular", Georgia, serif',
    "Caecilia Condensed": 'condensed, "Caecilia Regular", Georgia, serif',
    "Futura": 'Futura, Arial, sans-serif',
    "Helvetica": '"Helvetica Neue LT", Helvetica, Arial, sans-serif',
    "OpenDyslexic": 'OpenDyslexic, Arial, sans-serif',
    "Palatino": 'Palatino, Georgia, serif'
  };
  var cardFont = "Bookerly";
  var nightMode = false;
  var nightCardMode = "standard";
  var pageButtonMode = "normal";
  var fullRefreshMode = "manual";
  var reviewsSinceFullRefresh = 0;
  var collapsedDecks = {};
  function byId(id) { return document.getElementById(id); }
  function hide(element) { if (element.className.indexOf("hidden") < 0) element.className += " hidden"; }
  function show(element) { element.className = element.className.replace(/(^|\s)hidden(?=\s|$)/g, ""); }
  function clear(element) { while (element.firstChild) element.removeChild(element.firstChild); }
  function stopAboutLogoAnimation() {
    if (aboutLogoTimer !== null) {
      window.clearTimeout(aboutLogoTimer); aboutLogoTimer = null;
    }
  }
  function drawAboutLogoClean() {
    var image = byId("about-logo"), canvas = byId("about-logo-static"), context;
    if (!aboutLogoReady || !canvas || !canvas.getContext) return;
    context = canvas.getContext("2d");
    context.fillStyle = "#fff"; context.fillRect(0, 0, canvas.width, canvas.height);
    context.drawImage(image, 0, 0, canvas.width, canvas.height);
  }
  function prepareAboutLogo() {
    var image = byId("about-logo"), canvas = byId("about-logo-static");
    if (aboutLogoReady || !image || !canvas || !canvas.getContext) return;
    try {
      aboutLogoReady = true; drawAboutLogoClean(); show(canvas); hide(image);
    } catch (ignored) {
      aboutLogoReady = false; show(image); hide(canvas); return;
    }
    if (aboutLogoPending && byId("about-dialog").className.indexOf("hidden") < 0) {
      aboutLogoPending = false; playAboutLogoAnimation();
    }
  }
  function loadAboutLogo() {
    var image = byId("about-logo"), source;
    if (aboutLogoReady || image.getAttribute("src")) return;
    source = image.getAttribute("data-src");
    if (!source) return;
    image.setAttribute("src", source);
    if (image.complete) window.setTimeout(prepareAboutLogo, 0);
  }
  function playAboutLogoAnimation() {
    var canvas = byId("about-logo-static"), context;
    var levels = [1, 0.92, 0.8, 0.64, 0.47, 0.31, 0.18, 0.08, 0];
    var frame = 0, cell = 12;
    if (!aboutLogoReady) { aboutLogoPending = true; return; }
    aboutLogoPending = false; context = canvas.getContext("2d"); stopAboutLogoAnimation();
    function tick() {
      var amount = levels[frame], x, y;
      drawAboutLogoClean();
      if (amount > 0) {
        for (y = 0; y < canvas.height; y += cell) {
          for (x = 0; x < canvas.width; x += cell) {
            if (Math.random() < amount) {
              context.fillStyle = Math.random() < 0.5 ? "#000" : "#fff";
              context.fillRect(x, y, cell, cell);
            }
          }
        }
      }
      frame += 1;
      if (frame < levels.length) aboutLogoTimer = window.setTimeout(tick, 125);
      else { aboutLogoTimer = null; drawAboutLogoClean(); }
    }
    tick();
  }
  function openAbout() {
    hideSettingsTooltip(); show(byId("about-dialog")); loadAboutLogo(); playAboutLogoAnimation();
  }
  function closeAbout() {
    aboutLogoPending = false; stopAboutLogoAnimation();
    if (aboutLogoReady) drawAboutLogoClean();
    hide(byId("about-dialog")); resumePageButtonInput();
  }
  function deckName(name) { return (name || "").split("\u001f").join("::"); }
  function activeScrollTarget() {
    if (byId("review-view").className.indexOf("hidden") < 0) return byId("card");
    if (byId("decks-view").className.indexOf("hidden") < 0) return byId("decks-view");
    return null;
  }
  function updateScrollButtons() {
    var target = activeScrollTarget(), pager = byId("scroll-pager"), maximum;
    if (!target) {
      hide(byId("scroll-up")); hide(byId("scroll-down")); return;
    }
    pager.className = target.id === "card" ? "scroll-pager review-scroll" : "scroll-pager";
    maximum = Math.max(0, target.scrollHeight - target.clientHeight);
    if (target.scrollTop > 6) show(byId("scroll-up")); else hide(byId("scroll-up"));
    if (target.scrollTop < maximum - 6) show(byId("scroll-down")); else hide(byId("scroll-down"));
  }
  function scheduleScrollButtonUpdate() { window.setTimeout(updateScrollButtons, 0); }
  function pageScroll(direction) {
    var target = activeScrollTarget(), distance, maximum;
    if (!target) return;
    distance = Math.max(1, Math.floor(target.clientHeight * 0.8));
    maximum = Math.max(0, target.scrollHeight - target.clientHeight);
    target.scrollTop = Math.max(0, Math.min(maximum, target.scrollTop + direction * distance));
    updateScrollButtons();
  }
  function saveCollapsedDecks() {
    saveSetting("collapsedDecks", JSON.stringify(collapsedDecks));
  }
  function buildDeckTree(decks) {
    var roots = [], i, j, parts, parent, children, lookup, key, node, fullName;
    for (i = 0; i < decks.length; ++i) {
      parts = deckName(decks[i].name).split("::");
      parent = null; children = roots; fullName = "";
      for (j = 0; j < parts.length; ++j) {
        if (!parts[j]) continue;
        fullName = fullName ? fullName + "::" + parts[j] : parts[j];
        lookup = parent ? parent.childLookup : roots.childLookup;
        if (!lookup) {
          lookup = {};
          if (parent) parent.childLookup = lookup; else roots.childLookup = lookup;
        }
        key = parts[j]; node = lookup[key];
        if (!node) {
          node = { name: parts[j], fullName: fullName, deck: null, children: [], childLookup: {} };
          lookup[key] = node; children.push(node);
        }
        if (j === parts.length - 1) node.deck = decks[i];
        parent = node; children = node.children;
      }
    }
    return roots;
  }
  function renderDeckNode(node, container) {
    var wrapper = document.createElement("div"), row = document.createElement("div");
    var toggle = document.createElement("button"), deckButton, content, name, count, arrow, children, collapsed;
    wrapper.className = "deck-node"; row.className = "deck-row";
    toggle.type = "button";
    if (node.children.length) {
      collapsed = collapsedDecks[node.fullName] === true;
      toggle.className = "deck-toggle";
      toggle.innerHTML = collapsed ? "+" : "&minus;";
      toggle.title = collapsed ? "Expand subdecks" : "Collapse subdecks";
    } else {
      toggle.className = "deck-toggle deck-toggle-placeholder";
      toggle.disabled = true;
      toggle.innerHTML = "&nbsp;";
    }
    row.appendChild(toggle);
    if (node.deck) {
      deckButton = document.createElement("button"); deckButton.className = "deck"; deckButton.type = "button";
      content = document.createElement("span"); content.className = "deck-content";
      name = document.createElement("span"); name.className = "deck-name"; name.appendChild(document.createTextNode(node.name));
      count = document.createElement("span"); count.className = "deck-count"; count.appendChild(document.createTextNode(node.deck.cards + " due"));
      arrow = document.createElement("span"); arrow.className = "deck-arrow"; arrow.innerHTML = "&rsaquo;";
      content.appendChild(name); content.appendChild(count); content.appendChild(arrow); deckButton.appendChild(content);
      deckButton.onclick = (function (selected) { return function () { openDeck(selected); }; }(node.deck));
      row.appendChild(deckButton);
    } else {
      deckButton = document.createElement("div"); deckButton.className = "deck deck-group";
      name = document.createElement("span"); name.className = "deck-name"; name.appendChild(document.createTextNode(node.name));
      deckButton.appendChild(name); row.appendChild(deckButton);
    }
    wrapper.appendChild(row);
    if (node.children.length) {
      children = document.createElement("div"); children.className = "deck-children";
      for (var i = 0; i < node.children.length; ++i) renderDeckNode(node.children[i], children);
      if (collapsed) children.style.display = "none";
      toggle.onclick = (function (path, childContainer, control) {
        return function () {
          var isCollapsed = childContainer.style.display === "none";
          childContainer.style.display = isCollapsed ? "block" : "none";
          control.innerHTML = isCollapsed ? "&minus;" : "+";
          control.title = isCollapsed ? "Collapse subdecks" : "Expand subdecks";
          if (isCollapsed) delete collapsedDecks[path]; else collapsedDecks[path] = true;
          saveCollapsedDecks();
          scheduleScrollButtonUpdate();
        };
      }(node.fullName, children, toggle));
      wrapper.appendChild(children);
    }
    container.appendChild(wrapper);
  }
  function resetCardScroll() { byId("card").scrollTop = 0; }
  function selectedRadio(name, value) {
    var inputs = document.getElementsByName(name), i;
    for (i = 0; i < inputs.length; ++i) inputs[i].checked = inputs[i].value === value;
  }
  var settingsTooltipSource = null;
  function hideSettingsTooltip() {
    hide(byId("settings-tooltip"));
    settingsTooltipSource = null;
  }
  function showSettingsTooltip(button) {
    var tooltip = byId("settings-tooltip"), text = button.getAttribute("data-help") || "";
    var bounds, left, top;
    if (settingsTooltipSource === button && tooltip.className.indexOf("hidden") < 0) {
      hideSettingsTooltip(); return;
    }
    clear(tooltip); tooltip.appendChild(document.createTextNode(text)); show(tooltip);
    bounds = button.getBoundingClientRect();
    left = Math.max(20, Math.min(window.innerWidth - tooltip.offsetWidth - 20,
      bounds.right - tooltip.offsetWidth));
    top = bounds.bottom + 8;
    if (top + tooltip.offsetHeight > window.innerHeight - 20)
      top = Math.max(20, bounds.top - tooltip.offsetHeight - 8);
    tooltip.style.left = left + "px"; tooltip.style.top = top + "px";
    settingsTooltipSource = button;
  }
  function saveFullRefreshProgress(value) {
    reviewsSinceFullRefresh = value;
    saveSetting("reviewsSinceFullRefresh", String(value));
  }
  function updateSyncStatus() {
    byId("status").innerHTML = pendingReviews > 0
      ? "&#9679; " + pendingReviews + " review" + (pendingReviews === 1 ? "" : "s") + " not synced"
      : "&#10003; Synced";
  }
  function storePendingReviews(value) {
    pendingReviews = Math.max(0, value);
    updateSyncStatus();
  }
  function storePendingFromResult(result, fallback) {
    storePendingReviews(result && typeof result.pendingReviews === "number"
      ? result.pendingReviews : fallback);
  }
  function closeInterface() {
    if (window.kindle && window.kindle.appmgr && window.kindle.appmgr.back) window.kindle.appmgr.back();
    else window.close();
  }
  function closeApplication() {
    if (state.closing) return;
    if (settingWriteInFlight || settingWrites.length) {
      byId("status").innerHTML = "Saving settings...";
      afterSettingsSaved(closeApplication);
      return;
    }
    if (state.cardLoading || state.answerInFlight || state.undoInFlight || state.syncInFlight) {
      warning("Please wait for the current operation to finish.");
      return;
    }
    state.closing = true;
    state.inputEpoch += 1;
    state.syncInFlight = true;
    warning("");
    byId("status").innerHTML = "Synchronizing with AnkiWeb...";
    request("POST", "/api/sync", "", function (error, result) {
      if (!error && result && result.type !== "error" && result.required === "none" &&
          result.statePersisted !== false)
        storePendingFromResult(result, 0);
      if (result && result.statePersisted === false) {
        state.closing = false; state.syncInFlight = false;
        warning("Sync succeeded, but AnkINK could not save its local state.");
        return;
      }
      request("POST", "/api/quit", "", function () { closeInterface(); }, 0);
    }, 0);
  }
  function warning(message) {
    if (warningTimer !== null) {
      window.clearTimeout(warningTimer);
      warningTimer = null;
    }
    byId("warning").innerHTML = "";
    byId("warning").appendChild(document.createTextNode(message || ""));
    if (message) {
      show(byId("warning"));
      warningTimer = window.setTimeout(function () {
        warningTimer = null;
        warning("");
      }, 2000);
    } else hide(byId("warning"));
  }
  function request(method, path, body, callback, retries) {
    var xhr = new XMLHttpRequest();
    xhr.open(method, API + path, true);
    if (method === "POST") xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
    xhr.onreadystatechange = function () {
      var data;
      if (xhr.readyState !== 4) return;
      try { data = JSON.parse(xhr.responseText); } catch (error) { data = null; }
      if (xhr.status >= 200 && xhr.status < 300 && data) callback(null, data);
      else if (retries > 0) window.setTimeout(function () {
        request(method, path, body, callback, retries - 1);
      }, 500);
      else callback((data && (data.message || data.error)) || "AnkINK engine is not responding.", data);
    };
    xhr.onerror = function () {
      if (retries > 0) window.setTimeout(function () { request(method, path, body, callback, retries - 1); }, 500);
      else callback("AnkINK engine is not responding.", null);
    };
    xhr.send(body || null);
  }
  function flushSettingWrites() {
    var setting, callback;
    if (settingWriteInFlight) return;
    if (!settingWrites.length) {
      callback = settingsDrainedCallback; settingsDrainedCallback = null;
      if (callback) callback();
      return;
    }
    setting = settingWrites.shift(); settingWriteInFlight = true;
    request("POST", "/api/settings", "key=" + encodeURIComponent(setting.key) +
      "&value=" + encodeURIComponent(setting.value), function (error) {
        settingWriteInFlight = false;
        if (error) warning(error);
        flushSettingWrites();
      }, 0);
  }
  function saveSetting(key, value) {
    var index;
    for (index = settingWrites.length - 1; index >= 0; --index) {
      if (settingWrites[index].key === key) {
        settingWrites[index].value = value; flushSettingWrites(); return;
      }
    }
    settingWrites.push({ key: key, value: value }); flushSettingWrites();
  }
  function afterSettingsSaved(callback) {
    if (!settingWriteInFlight && !settingWrites.length) callback();
    else { settingsDrainedCallback = callback; flushSettingWrites(); }
  }
  function clearPhysicalInput(done) {
    request("POST", "/api/input/clear", "", function () {
      if (done) done();
    }, 0);
  }
  function safeHtml(element, html, fallback) {
    var container = document.createElement("div");
    var blocked = { SCRIPT:1, STYLE:1, IFRAME:1, OBJECT:1, EMBED:1, LINK:1, META:1 };
    function clean(node) {
      var child = node.firstChild, next, i, attribute, name, value;
      while (child) {
        next = child.nextSibling;
        if (child.nodeType === 1 && blocked[child.tagName]) node.removeChild(child);
        else {
          if (child.nodeType === 1) {
            for (i = child.attributes.length - 1; i >= 0; --i) {
              attribute = child.attributes[i]; name = attribute.name.toLowerCase();
              value = attribute.value.toLowerCase();
              if (name === "src" && child.tagName === "IMG" && value.indexOf("data:image/") !== 0 && value.indexOf("http://127.0.0.1:8765/api/media/") !== 0) {
                if (value.indexOf("..") < 0 && value.indexOf(":") < 0 && value.indexOf("/") < 0) {
                  child.setAttribute("data-ankink-media", attribute.value);
                  child.setAttribute("src", "data:image/gif;base64,R0lGODlhAQABAAD/ACwAAAAAAQABAAACADs=");
                }
                value = (child.getAttribute("src") || "").toLowerCase();
              }
              if (name === "style") {
                value = safeInlineStyle(attribute.value);
                if (value) child.setAttribute("style", value); else child.removeAttribute("style");
              } else if (name.indexOf("on") === 0 || name === "srcset" ||
                  (name === "src" && value.indexOf("data:image/") !== 0 &&
                    value.indexOf("http://127.0.0.1:8765/api/media/") !== 0) ||
                  (name === "href" && value.indexOf("#") !== 0))
                child.removeAttribute(attribute.name);
            }
          }
          clean(child);
        }
        child = next;
      }
    }
    container.innerHTML = html || ("<em>" + fallback + "</em>"); clean(container);
    clear(element); while (container.firstChild) element.appendChild(container.firstChild);
    renderMath(element);
    prepareImages(element);
  }
  function safeInlineStyle(value) {
    var declarations = String(value || "").split(";"), safe = [], i, part, colon, property, content, lower;
    for (i = 0; i < declarations.length; ++i) {
      part = declarations[i]; colon = part.indexOf(":");
      if (colon < 1) continue;
      property = part.substring(0, colon).replace(/^\s+|\s+$/g, "").toLowerCase();
      content = part.substring(colon + 1).replace(/^\s+|\s+$/g, ""); lower = content.toLowerCase();
      if (!property || lower.indexOf("url(") >= 0 || lower.indexOf("expression") >= 0 ||
          property === "behavior" || property === "-moz-binding" || property === "z-index" ||
          (property === "position" && (lower === "fixed" || lower === "sticky"))) continue;
      safe.push(property + ":" + content);
    }
    return safe.join(";");
  }
  function applyCardCss(css) {
    var source = String(css || "").replace(/\/\*[\s\S]*?\*\//g, "").replace(/@import[^;]*;/gi, "");
    var output = [], block = /([^{}]+)\{([^{}]*)\}/g, match, selectors, scoped, i, selector, body;
    while ((match = block.exec(source))) {
      selectors = match[1].split(","); body = safeInlineStyle(match[2]); scoped = [];
      if (!body || /^\s*@/.test(match[1])) continue;
      for (i = 0; i < selectors.length; ++i) {
        selector = selectors[i].replace(/^\s+|\s+$/g, "");
        if (!selector || selector.indexOf("@") >= 0) continue;
        selector = selector.replace(/^\.nightMode\b/, ".night-mode #card");
        selector = selector.replace(/\.nightMode\b/g, ".night-mode");
        selector = selector.replace(/(^|[ >+~])\.card\b/g, "$1#card");
        selector = selector.replace(/(^|[ >+~])(html|body)\b/gi, "$1#card");
        if (selector.indexOf("#card") < 0) selector = "#card " + selector;
        if (/#card\s*[+~]/.test(selector)) continue;
        scoped.push(selector);
      }
      if (scoped.length) output.push(scoped.join(",") + "{" + body + "}");
    }
    byId("card-template-style").innerHTML = output.join("\n");
  }
  function parsedColor(value) {
    var match, hex, alpha;
    value = String(value || "").replace(/^\s+|\s+$/g, "").toLowerCase();
    if (!value || value === "transparent") return null;
    match = value.match(/^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*([\d.]+))?\s*\)$/);
    if (match) {
      alpha = typeof match[4] === "undefined" ? 1 : parseFloat(match[4]);
      if (alpha === 0) return null;
      return [parseInt(match[1], 10), parseInt(match[2], 10), parseInt(match[3], 10)];
    }
    match = value.match(/^#([0-9a-f]{3}|[0-9a-f]{6})$/);
    if (!match) return null;
    hex = match[1];
    if (hex.length === 3) hex = hex.charAt(0) + hex.charAt(0) + hex.charAt(1) + hex.charAt(1) + hex.charAt(2) + hex.charAt(2);
    return [parseInt(hex.substring(0, 2), 16), parseInt(hex.substring(2, 4), 16), parseInt(hex.substring(4, 6), 16)];
  }
  function colorText(rgb) {
    return "rgb(" + Math.round(rgb[0]) + "," + Math.round(rgb[1]) + "," + Math.round(rgb[2]) + ")";
  }
  function colorLuminance(rgb) { return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]; }
  function nightFriendlyColor(value, background) {
    var rgb = parsedColor(value), luminance, factor;
    if (!rgb) return null;
    luminance = colorLuminance(rgb);
    if (background) {
      if (luminance <= 55) return null;
      factor = 55 / luminance;
      return colorText([rgb[0] * factor, rgb[1] * factor, rgb[2] * factor]);
    }
    if (luminance >= 190) return null;
    factor = (190 - luminance) / (255 - luminance);
    return colorText([
      rgb[0] + (255 - rgb[0]) * factor,
      rgb[1] + (255 - rgb[1]) * factor,
      rgb[2] + (255 - rgb[2]) * factor
    ]);
  }
  function saveNightStyle(element) {
    if (element._ankinkNightStyleSaved) return;
    element._ankinkNightStyleSaved = true;
    element._ankinkNightOriginalStyle = element.getAttribute("style");
  }
  function restoreNightPalette(root) {
    var elements = [root], descendants = root.getElementsByTagName("*"), i, element;
    for (i = 0; i < descendants.length; ++i) elements.push(descendants[i]);
    for (i = 0; i < elements.length; ++i) {
      element = elements[i];
      if (!element._ankinkNightStyleSaved) continue;
      if (element._ankinkNightOriginalStyle === null) element.removeAttribute("style");
      else element.setAttribute("style", element._ankinkNightOriginalStyle);
      try { delete element._ankinkNightStyleSaved; delete element._ankinkNightOriginalStyle; }
      catch (ignored) { element._ankinkNightStyleSaved = false; element._ankinkNightOriginalStyle = null; }
    }
  }
  function setImportantStyle(element, property, value) {
    if (!value) return;
    saveNightStyle(element);
    if (element.style.setProperty) element.style.setProperty(property, value, "important");
    else element.style[property === "background-color" ? "backgroundColor" : property] = value;
  }
  function applyNightPalette(root) {
    var elements = [root], descendants = root.getElementsByTagName("*"), i, element, computed, parentComputed;
    var foreground, background;
    for (i = 0; i < descendants.length; ++i) elements.push(descendants[i]);
    for (i = 0; i < elements.length; ++i) {
      element = elements[i];
      if (element.tagName === "IMG" || element.tagName === "VIDEO" || element.tagName === "CANVAS") continue;
      computed = window.getComputedStyle ? window.getComputedStyle(element, null) : element.currentStyle;
      if (!computed) continue;
      parentComputed = element.parentNode && element.parentNode.nodeType === 1 && window.getComputedStyle ?
        window.getComputedStyle(element.parentNode, null) : null;
      if (!parentComputed || computed.color !== parentComputed.color) {
        foreground = nightFriendlyColor(computed.color, false);
        setImportantStyle(element, "color", foreground);
      }
      background = nightFriendlyColor(computed.backgroundColor, true);
      setImportantStyle(element, "background-color", background);
    }
  }
  function restoreNightImage(image) {
    var original;
    if (!image._ankinkNightImageInverted) return;
    original = image._ankinkNightOriginalSource;
    image._ankinkNightImageInverted = false;
    image._ankinkNightOriginalSource = null;
    if (original) image.setAttribute("src", original);
  }
  function invertNightImage(image) {
    var width, height, scale, canvas, context, pixels, data, i, source;
    if (!nightMode || nightCardMode !== "palette-images" || image._ankinkNightImageInverted ||
        image._ankinkNightImageBusy || image._ankinkMediaReady === false) return;
    width = image.naturalWidth || image.width; height = image.naturalHeight || image.height;
    source = image.getAttribute("src") || "";
    if (!width || !height || !source || source.indexOf("data:image/gif;base64,R0lGODlhAQABAAD/") === 0) return;
    scale = Math.min(1, 1600 / Math.max(width, height));
    width = Math.max(1, Math.round(width * scale)); height = Math.max(1, Math.round(height * scale));
    image._ankinkNightImageBusy = true;
    try {
      canvas = document.createElement("canvas"); canvas.width = width; canvas.height = height;
      context = canvas.getContext("2d"); context.drawImage(image, 0, 0, width, height);
      pixels = context.getImageData(0, 0, width, height); data = pixels.data;
      for (i = 0; i < data.length; i += 4) {
        data[i] = 255 - data[i]; data[i + 1] = 255 - data[i + 1]; data[i + 2] = 255 - data[i + 2];
      }
      context.putImageData(pixels, 0, 0);
      image._ankinkNightOriginalSource = source;
      image._ankinkNightImageInverted = true;
      image.setAttribute("src", canvas.toDataURL("image/png"));
    } catch (ignored) {
      image._ankinkNightImageInverted = false;
      image._ankinkNightOriginalSource = null;
    }
    image._ankinkNightImageBusy = false;
    scheduleScrollButtonUpdate();
  }
  function applyNightCardAppearance() {
    var roots = [byId("front"), byId("back-face")], images, i, j;
    for (i = 0; i < roots.length; ++i) {
      restoreNightPalette(roots[i]);
      if (nightMode && nightCardMode !== "standard") applyNightPalette(roots[i]);
      images = roots[i].getElementsByTagName("img");
      for (j = 0; j < images.length; ++j) {
        if (nightMode && nightCardMode === "palette-images") invertNightImage(images[j]);
        else restoreNightImage(images[j]);
      }
    }
  }
  function prepareImages(root) {
    var images = root.getElementsByTagName("img"), i, filename;
    for (i = 0; i < images.length; ++i) {
      filename = images[i].getAttribute("data-ankink-media");
      images[i]._ankinkMediaReady = !filename;
      images[i].onload = function () {
        this._ankinkMediaReady = true;
        scheduleScrollButtonUpdate(); invertNightImage(this);
      };
      images[i].onerror = function () {
        var failedImage = this, failedFilename = this._ankinkMediaFilename;
        if (!failedFilename || this._ankinkMediaFallbackTried) return;
        this._ankinkMediaFallbackTried = true;
        request("GET", "/api/media-data/" + encodeURIComponent(failedFilename) + "?v=" + MEDIA_VERSION,
          null, function (error, data) {
            if (!error && data && typeof data.data === "string" && data.data.indexOf("data:image/") === 0)
              failedImage.setAttribute("src", data.data);
          }, 0);
      };
      if (filename) loadMediaImage(images[i], filename);
      if (!filename && images[i].complete) invertNightImage(images[i]);
      images[i].onclick = function () {
        if (this.className.indexOf("image-expanded") >= 0)
          this.className = this.className.replace(/(^|\s)image-expanded(?=\s|$)/g, "");
        else {
          this.className += " image-expanded";
          if (this.scrollIntoView) this.scrollIntoView(true);
        }
        scheduleScrollButtonUpdate();
      };
    }
  }
  function loadMediaImage(image, filename) {
    image._ankinkMediaFilename = filename;
    image._ankinkMediaFallbackTried = false;
    image._ankinkMediaReady = false;
    image.setAttribute("src", API + "/api/media/" + encodeURIComponent(filename) + "?v=" + MEDIA_VERSION);
  }
  function answerOnly(html) {
    var source = html || "";
    var lower = source.toLowerCase();
    var marker = lower.indexOf("<hr id=answer");
    var end;
    if (marker < 0) marker = lower.indexOf("<hr id=\"answer\"");
    if (marker < 0) marker = lower.indexOf("<hr id='answer'");
    if (marker >= 0) {
      end = lower.indexOf(">", marker);
      if (end >= 0) return source.substring(end + 1);
    }
    return source;
  }
  function finishKatexLoad() {
    var callbacks, i;
    if (katexState !== 1 || !katexStyleDone || !katexScriptDone) return;
    if (!window.katex || !window.katex.render) { katexState = 3; katexCallbacks = []; return; }
    katexState = 2; callbacks = katexCallbacks; katexCallbacks = [];
    for (i = 0; i < callbacks.length; ++i) callbacks[i]();
  }
  function loadKatex(done) {
    var head = document.getElementsByTagName("head")[0], style, script;
    if (katexState === 2) { if (done) done(); return; }
    if (katexState === 3) return;
    if (done) katexCallbacks.push(done);
    if (katexState === 1) return;
    katexState = 1;
    if (katexDeferredTimer !== null) {
      window.clearTimeout(katexDeferredTimer); katexDeferredTimer = null;
    }
    style = document.createElement("link"); style.rel = "stylesheet";
    style.href = "vendor/katex/katex.min.css";
    style.onload = function () { katexStyleDone = true; finishKatexLoad(); };
    style.onerror = function () { katexStyleDone = true; finishKatexLoad(); };
    script = document.createElement("script"); script.src = "vendor/katex/katex.min.js";
    script.async = true;
    script.onload = function () { katexScriptDone = true; finishKatexLoad(); };
    script.onerror = function () { katexState = 3; katexCallbacks = []; };
    head.appendChild(style); head.appendChild(script);
    window.setTimeout(function () {
      if (!katexStyleDone) { katexStyleDone = true; finishKatexLoad(); }
    }, 1000);
  }
  function deferKatexLoad() {
    if (katexState !== 0 || katexDeferredTimer !== null) return;
    katexDeferredTimer = window.setTimeout(function () {
      katexDeferredTimer = null; loadKatex();
    }, 500);
  }
  function unboxMath(expression) {
    var value = String(expression || "").replace(/^\s+|\s+$/g, ""), i, depth = 0;
    if (value.substring(0, 7) !== "\\boxed{" || value.charAt(value.length - 1) !== "}") return null;
    for (i = 6; i < value.length; ++i) {
      if (value.charAt(i) === "{") depth += 1;
      else if (value.charAt(i) === "}") {
        depth -= 1;
        if (depth === 0 && i !== value.length - 1) return null;
      }
    }
    return depth === 0 ? value.substring(7, value.length - 1) : null;
  }
  function texGroup(value, start) {
    var i, depth = 0;
    if (value.charAt(start) !== "{") return null;
    for (i = start; i < value.length; ++i) {
      if (value.charAt(i) === "{") depth += 1;
      else if (value.charAt(i) === "}") {
        depth -= 1;
        if (depth === 0) return { text: value.substring(start + 1, i), end: i + 1 };
      }
    }
    return null;
  }
  function limitedSum(expression) {
    var at = expression.indexOf("\\sum"), cursor, marker, group;
    var lower = null, upper = null, count;
    if (at < 0) return null;
    cursor = at + 4;
    for (count = 0; count < 2; ++count) {
      while (/\s/.test(expression.charAt(cursor))) cursor += 1;
      marker = expression.charAt(cursor);
      if (marker !== "_" && marker !== "^") break;
      cursor += 1;
      if (expression.charAt(cursor) === "{") {
        group = texGroup(expression, cursor);
        if (!group) return null;
        cursor = group.end;
      } else {
        if (!expression.charAt(cursor)) return null;
        group = { text: expression.charAt(cursor), end: cursor + 1 };
        cursor = group.end;
      }
      if (marker === "_") lower = group.text; else upper = group.text;
    }
    return lower !== null || upper !== null ? {
      before: expression.substring(0, at), lower: lower,
      upper: upper, after: expression.substring(cursor)
    } : null;
  }
  function renderMathPart(element, expression, displayStyle) {
    if (!expression) return;
    window.katex.render((displayStyle ? "\\displaystyle " : "") + expression, element, {
      displayMode: false, throwOnError: false, strict: "ignore", trust: false
    });
  }
  function directSpans(node) {
    var result = [], children = node ? node.childNodes : [], i;
    for (i = 0; i < children.length; ++i) {
      if (children[i].nodeType === 1 && children[i].tagName.toLowerCase() === "span")
        result.push(children[i]);
    }
    return result;
  }
  function positionedContents(vlist) {
    var result = [], children = directSpans(vlist), i, parts;
    for (i = 0; i < children.length; ++i) {
      parts = directSpans(children[i]);
      if (parts.length > 1) result.push({
        content: parts[parts.length - 1], top: parseFloat(children[i].style.top || "0")
      });
    }
    return result;
  }
  function repairKindleScripts(root) {
    var live = root.getElementsByClassName("msupsub"), nodes = [], i, node, vlists;
    var positions, isSub, sup, sub, wrapper, width;
    for (i = 0; i < live.length; ++i) nodes.push(live[i]);
    for (i = 0; i < nodes.length; ++i) {
      node = nodes[i]; vlists = node.getElementsByClassName("vlist");
      if (!vlists.length) continue;
      positions = positionedContents(vlists[0]);
      if (!positions.length || positions.length > 2) continue;
      isSub = node.getElementsByClassName("vlist-t2").length > 0;
      sub = positions.length === 2 || isSub ? positions[0] : null;
      sup = positions.length === 2 ? positions[1] : (!isSub ? positions[0] : null);
      clear(node); node.className += " ankink-script";
      if (sub && sup) {
        node.className += " ankink-script-both";
        wrapper = document.createElement("span"); wrapper.className = "ankink-script-sup";
        wrapper.appendChild(sup.content); node.appendChild(wrapper); sup = wrapper;
        wrapper = document.createElement("span"); wrapper.className = "ankink-script-sub";
        wrapper.appendChild(sub.content); node.appendChild(wrapper); sub = wrapper;
        width = Math.max(sup.offsetWidth, sub.offsetWidth); node.style.width = width + "px";
        sup.style.left = Math.max(0, (width - sup.offsetWidth) / 2) + "px";
        sub.style.left = Math.max(0, (width - sub.offsetWidth) / 2) + "px";
      } else if (sub) {
        node.className += " ankink-script-sub-only"; node.appendChild(sub.content);
      } else if (sup) {
        node.className += " ankink-script-sup-only"; node.appendChild(sup.content);
      }
    }
  }
  function repairKindleFractions(root) {
    var live = root.getElementsByClassName("mfrac"), nodes = [], i, j, node, vlists;
    var positions, line, numerator, denominator, wrapper, width;
    for (i = 0; i < live.length; ++i) nodes.push(live[i]);
    for (i = 0; i < nodes.length; ++i) {
      node = nodes[i]; vlists = node.getElementsByClassName("vlist");
      if (!vlists.length) continue;
      positions = positionedContents(vlists[0]); line = null; numerator = null; denominator = null;
      for (j = 0; j < positions.length; ++j) {
        if ((" " + positions[j].content.className + " ").indexOf(" frac-line ") >= 0) {
          line = positions[j]; break;
        }
      }
      if (!line) continue;
      for (j = 0; j < positions.length; ++j) if (positions[j] !== line) {
        if (positions[j].top < line.top) numerator = positions[j]; else denominator = positions[j];
      }
      if (!numerator || !denominator) continue;
      clear(node); node.className += " ankink-fraction";
      wrapper = document.createElement("span"); wrapper.className = "ankink-frac-numerator";
      wrapper.appendChild(numerator.content); node.appendChild(wrapper); numerator = wrapper;
      wrapper = document.createElement("span"); wrapper.className = "ankink-frac-rule";
      node.appendChild(wrapper);
      wrapper = document.createElement("span"); wrapper.className = "ankink-frac-denominator";
      wrapper.appendChild(denominator.content); node.appendChild(wrapper); denominator = wrapper;
      width = Math.max(numerator.offsetWidth, denominator.offsetWidth); node.style.width = width + "px";
    }
  }
  function repairKindleMath(root) {
    if (!window.kindle) return;
    repairKindleScripts(root); repairKindleFractions(root);
  }
  function renderKindleLimitedSum(node, expression, boxed, display) {
    var sum = limitedSum(expression), table, row1, row2, row3;
    var left, upper, operator, lower, right;
    if (!sum || !window.kindle) return false;
    clear(node);
    table = document.createElement("table");
    table.className = "ankink-sum-table" + (boxed ? " ankink-sum-boxed" : "");
    row1 = document.createElement("tr"); row2 = document.createElement("tr");
    row3 = document.createElement("tr");
    left = document.createElement("td"); left.className = "ankink-sum-side"; left.rowSpan = 3;
    upper = document.createElement("td"); upper.className = "ankink-sum-limit";
    operator = document.createElement("td"); operator.className = "ankink-sum-operator";
    lower = document.createElement("td"); lower.className = "ankink-sum-limit";
    right = document.createElement("td"); right.className = "ankink-sum-side"; right.rowSpan = 3;
    renderMathPart(left, sum.before, true); renderMathPart(upper, sum.upper, false);
    renderMathPart(operator, "\\sum\\nolimits", true); renderMathPart(lower, sum.lower, false);
    renderMathPart(right, sum.after, true);
    row1.appendChild(left); row1.appendChild(upper); row1.appendChild(right);
    row2.appendChild(operator); row3.appendChild(lower);
    table.appendChild(row1); table.appendChild(row2); table.appendChild(row3); node.appendChild(table);
    node.className += " ankink-limited-sum" + (display ? " ankink-display-math" : "");
    return true;
  }
  function renderMath(root) {
    var delimiters = [
      { left: "\\[", right: "\\]", display: true },
      { left: "[$$]", right: "[/$$]", display: true },
      { left: "[latex]", right: "[/latex]", display: true },
      { left: "$$", right: "$$", display: true },
      { left: "\\(", right: "\\)", display: false },
      { left: "[$]", right: "[/$]", display: false }
    ];
    var nodes = [];
    function collect(node) {
      var child, className;
      if (node.nodeType === 3) { nodes.push(node); return; }
      if (node.nodeType !== 1) return;
      className = typeof node.className === "string" ? node.className : "";
      if (node.tagName === "SCRIPT" || node.tagName === "STYLE" || node.tagName === "TEXTAREA" ||
          node.tagName === "PRE" || node.tagName === "CODE" || className.indexOf("katex") >= 0) return;
      child = node.firstChild;
      while (child) { collect(child); child = child.nextSibling; }
    }
    function findOpening(text, start) {
      var best = null, i, position;
      for (i = 0; i < delimiters.length; ++i) {
        position = text.indexOf(delimiters[i].left, start);
        if (position >= 0 && (!best || position < best.position))
          best = { delimiter: delimiters[i], position: position };
      }
      return best;
    }
    function replace(node) {
      var text = node.nodeValue, fragment = document.createDocumentFragment();
      var cursor = 0, opening, contentStart, closing, span, expression, boxed;
      while ((opening = findOpening(text, cursor))) {
        contentStart = opening.position + opening.delimiter.left.length;
        closing = text.indexOf(opening.delimiter.right, contentStart);
        if (closing < 0) break;
        if (opening.position > cursor) fragment.appendChild(document.createTextNode(text.substring(cursor, opening.position)));
        expression = text.substring(contentStart, closing);
        span = document.createElement("span");
        boxed = window.kindle ? unboxMath(expression) : null;
        if (boxed !== null) {
          expression = boxed;
          span.className = "ankink-boxed-math" +
            (opening.delimiter.display ? " ankink-display-math" : "");
        }
        try {
          if (!renderKindleLimitedSum(span, expression, boxed !== null, opening.delimiter.display)) {
            window.katex.render(expression, span, {
              displayMode: opening.delimiter.display,
              throwOnError: false,
              strict: "ignore",
              trust: false
            });
            repairKindleMath(span);
          }
        } catch (error) {
          span.appendChild(document.createTextNode(opening.delimiter.left + expression + opening.delimiter.right));
        }
        fragment.appendChild(span);
        cursor = closing + opening.delimiter.right.length;
      }
      if (cursor === 0) return;
      if (cursor < text.length) fragment.appendChild(document.createTextNode(text.substring(cursor)));
      node.parentNode.replaceChild(fragment, node);
    }
    collect(root);
    if (katexState !== 2) {
      for (var pending = 0; pending < nodes.length; ++pending) {
        var opening = findOpening(nodes[pending].nodeValue, 0);
        if (opening && nodes[pending].nodeValue.indexOf(opening.delimiter.right,
            opening.position + opening.delimiter.left.length) >= 0) {
          loadKatex(function () { renderMath(root); }); return;
        }
      }
      return;
    }
    for (var i = 0; i < nodes.length; ++i) replace(nodes[i]);
  }
  function applySettings(settings) {
    var parsed;
    settings = settings || {};
    pendingReviews = Math.max(0, parseInt(settings.pendingReviews || "0", 10) || 0);
    fontScale = nearestFontScale(parseFloat(settings.fontScale || "1"));
    cardFont = cardFonts[settings.cardFont] ? settings.cardFont : "Bookerly";
    nightMode = settings.nightMode === true;
    nightCardMode = settings.nightCardMode;
    if (nightCardMode !== "palette" && nightCardMode !== "palette-images")
      nightCardMode = "standard";
    pageButtonMode = settings.pageButtonMode === "reversed" ? "reversed" : "normal";
    fullRefreshMode = settings.fullRefreshMode;
    if (fullRefreshMode !== "every-card" && fullRefreshMode !== "every-five")
      fullRefreshMode = "manual";
    reviewsSinceFullRefresh = Math.max(0,
      parseInt(settings.reviewsSinceFullRefresh || "0", 10) || 0);
    try { parsed = JSON.parse(settings.collapsedDecks || "{}"); }
    catch (ignored) { parsed = {}; }
    collapsedDecks = parsed && typeof parsed === "object" ? parsed : {};
    applyFontScale(false); applyCardFont(false); applyNightMode(false);
    updateSyncStatus();
  }
  function loadSettings() {
    request("GET", "/api/settings", null, function (error, settings) {
      if (error) warning(error);
      applySettings(settings);
      startApplication();
    }, 12);
  }
  // A normal launch always gets one chance to synchronize before any deck is
  // interactive.  Reviewing remains offline-first after that; the only other
  // automatic synchronization is the existing close path.
  function startApplication() {
    hide(byId("decks-view")); hide(byId("review-view")); hide(byId("sync"));
    byId("status").innerHTML = "Connecting to AnkINK engine...";
    request("GET", "/api/status", null, function (error, status) {
      if (error) { warning(error); byId("status").innerHTML = "Engine unavailable"; return; }
      if (!status.authenticated) {
        byId("status").innerHTML = "Sign in to AnkiWeb"; showLogin(); return;
      }
      syncNow(function () {
        // A full-sync or sign-in prompt owns the screen; otherwise make the
        // locally available collection usable even if startup sync failed.
        if (byId("full-sync-panel").className.indexOf("hidden") >= 0 &&
            byId("auth-panel").className.indexOf("hidden") >= 0)
          loadDecks();
      });
    }, 12);
  }
  function loadDecks() {
    warning(""); byId("status").innerHTML = "Connecting to AnkINK engine...";
    request("GET", "/api/status", null, function (error, status) {
      if (error) { warning(error); byId("status").innerHTML = "Engine unavailable"; return; }
      if (!status.authenticated) {
        byId("status").innerHTML = "Sign in to AnkiWeb"; showLogin(); return;
      }
      if (typeof status.pendingReviews === "number")
        storePendingReviews(status.pendingReviews);
      hide(byId("auth-panel")); hide(byId("review-view")); show(byId("decks-view"));
      show(byId("sync"));
      updateSyncStatus();
      request("GET", "/api/decks", null, function (deckError, message) {
        var list = byId("decks"), tree, i;
        clear(list); byId("collection-path").innerHTML = "";
        byId("collection-path").appendChild(document.createTextNode((message && message.path) || status.collection || ""));
        deferKatexLoad();
        if (deckError || !message || !message.decks || !message.decks.length) {
          var empty = document.createElement("div"); empty.className = "empty-state";
          empty.appendChild(document.createTextNode(deckError || "This collection contains no decks.")); list.appendChild(empty);
          if (deckError) warning(deckError); return;
        }
        tree = buildDeckTree(message.decks);
        for (i = 0; i < tree.length; ++i) renderDeckNode(tree[i], list);
        scheduleScrollButtonUpdate();
        clearPhysicalInput(resumePageButtonInput);
      }, 0);
    }, 12);
  }
  function applyFontScale(persist) {
    byId("front").style.fontSize = Math.round(32 * fontScale) + "px";
    byId("back-face").style.fontSize = Math.round(26 * fontScale) + "px";
    if (persist !== false) saveSetting("fontScale", String(fontScale));
    updateFontSizeChoices();
    scheduleScrollButtonUpdate();
  }
  function updateFontSizeChoices() {
    var buttons = document.getElementsByName("card-font-size"), family = cardFonts[cardFont], i, scale;
    for (i = 0; i < buttons.length; ++i) {
      scale = parseFloat(buttons[i].getAttribute("data-scale"));
      buttons[i].className = Math.abs(scale - fontScale) < 0.001 ? "selected" : "";
      if (buttons[i].style.setProperty) buttons[i].style.setProperty("font-family", family, "important");
      else buttons[i].style.fontFamily = family;
    }
  }
  function changeFontScale(direction) {
    var index = 0, i;
    for (i = 0; i < fontScales.length; ++i)
      if (Math.abs(fontScales[i] - fontScale) < 0.001) { index = i; break; }
    index = Math.max(0, Math.min(fontScales.length - 1, index + direction));
    fontScale = fontScales[index]; applyFontScale();
  }
  function applyCardFont(persist) {
    var family = cardFonts[cardFont];
    restoreNightPalette(byId("front"));
    restoreNightPalette(byId("back-face"));
    if (byId("front").style.setProperty) {
      byId("front").style.setProperty("font-family", family, "important");
      byId("back-face").style.setProperty("font-family", family, "important");
    } else {
      byId("front").style.fontFamily = family;
      byId("back-face").style.fontFamily = family;
    }
    if (persist !== false) saveSetting("cardFont", cardFont);
    updateFontSizeChoices();
    applyNightCardAppearance();
    scheduleScrollButtonUpdate();
  }
  function applyNightMode(persist) {
    var html = document.documentElement;
    if (nightMode) {
      if (html.className.indexOf("night-mode") < 0) html.className += " night-mode";
      byId("night-mode").innerHTML = "&#9788;";
      byId("night-mode").title = "Day mode";
    } else {
      html.className = html.className.replace(/(^|\s)night-mode(?=\s|$)/g, "");
      byId("night-mode").innerHTML = "&#9789;";
      byId("night-mode").title = "Night mode";
    }
    if (persist !== false) saveSetting("nightMode", nightMode ? "1" : "0");
    applyNightCardAppearance();
  }
  function updateCounts(counts) {
    counts = counts || { "new": 0, learning: 0, review: 0 };
    byId("session-count").innerHTML = counts["new"] + " new &middot; " +
      counts.learning + " learn &middot; " + counts.review + " review";
  }
  function openDeck(deck) {
    if (state.syncInFlight || state.cardLoading || state.answerInFlight || state.undoInFlight) return;
    state.deck = deck; state.reviewed = 0; byId("review-title").innerHTML = "";
    byId("review-title").appendChild(document.createTextNode(deckName(deck.name)));
    updateCounts(deck); hide(byId("decks-view")); hide(byId("sync")); show(byId("review-view")); nextCard(); scheduleScrollButtonUpdate();
  }
  function showLogin() {
    state.inputEpoch += 1; state.deck = null; state.card = null; warning("");
    hide(byId("review-view")); hide(byId("decks-view")); hide(byId("sync")); hide(byId("account-dialog"));
    show(byId("auth-panel")); hide(byId("full-sync-panel"));
    byId("ankiweb-password").value = ""; window.scrollTo(0, 0); scheduleScrollButtonUpdate();
  }
  function syncNow(done) {
    if (state.syncInFlight) { if (done) done(false); return; }
    if (state.deck || state.cardLoading || state.answerInFlight || state.undoInFlight) {
      warning("Return to Decks before synchronizing.");
      if (done) done(false);
      return;
    }
    state.syncInFlight = true;
    state.inputEpoch += 1;
    warning(""); byId("status").innerHTML = "Synchronizing with AnkiWeb...";
    request("POST", "/api/sync", "", function (error, result) {
      if (error || !result || result.type === "error") {
        var message = error || (result && result.message) || "Sync failed.";
        state.syncInFlight = false;
        byId("status").innerHTML = "Sync unavailable";
        if (message.indexOf("Sign in") >= 0) showLogin(); else warning(message);
        updateSyncStatus(); if (done) done(false);
        return;
      }
      if (result.required === "full") {
        state.syncInFlight = false;
        byId("status").innerHTML = "Full sync required";
        if (result.downloadAllowed && !result.uploadAllowed) {
          downloadFromAnkiWeb(done); return;
        }
        if (result.downloadAllowed) show(byId("full-sync-panel"));
        else warning("AnkiWeb requires a full sync, but download is not currently allowed.");
        window.scrollTo(0, 0); if (done) done(false); return;
      }
      if (result.statePersisted === false) {
        state.syncInFlight = false; storePendingFromResult(result, pendingReviews);
        warning("Sync succeeded, but AnkINK could not save its local state.");
        if (done) done(false); return;
      }
      state.syncInFlight = false;
      storePendingFromResult(result, 0);
      if (result.message) warning(result.message);
      if (done) done(true); else loadDecks();
    }, 0);
  }
  function nextCard() {
    var loadEpoch;
    if (!state.deck || state.syncInFlight) return;
    state.inputEpoch += 1;
    loadEpoch = state.inputEpoch;
    state.cardLoading = true;
    state.card = null;
    state.answerShown = false;
    resetCardScroll();
    byId("front").innerHTML = "Loading..."; hide(byId("back-face")); hide(byId("answer-divider"));
    hide(byId("rating-controls")); show(byId("show-controls"));
    request("GET", "/api/decks/" + state.deck.id + "/next", null, function (error, card) {
      if (loadEpoch !== state.inputEpoch) return;
      clearPhysicalInput(function () {
        if (loadEpoch !== state.inputEpoch) return;
        state.cardLoading = false;
        if (error) { warning(error); resumePageButtonInput(); return; }
        if (!card || card.type === "error") {
          warning((card && card.message) || "Unable to load card."); resumePageButtonInput(); return;
        }
        if (card.type === "complete") {
          applyCardCss("");
          state.card = null; byId("front").innerHTML = "<h2>Session complete</h2><p>You reviewed " + state.reviewed + " cards.</p>";
          hide(byId("show-controls")); runScheduledRefresh(); scheduleScrollButtonUpdate();
          resumePageButtonInput(); return;
        }
        state.card = card; applyCardCss(card.css); safeHtml(byId("front"), card.front, "Empty front field");
        safeHtml(byId("back-face"), answerOnly(card.back), "No additional fields"); applyNightCardAppearance(); resetCardScroll();
        updateCounts(card.counts);
        if (card.buttons && card.buttons.length === 4) {
          for (var i = 0; i < 4; ++i) {
            byId("rating-" + (i + 1)).getElementsByTagName("small")[0].innerHTML = card.buttons[i].interval;
          }
        }
        runScheduledRefresh();
        scheduleScrollButtonUpdate();
        resumePageButtonInput();
      });
    }, 1);
  }
  function isReviewStateError(message) {
    return message === "No queued card is awaiting an answer" ||
      message.indexOf("Stale review command") === 0 ||
      message.indexOf("not the current queued card") >= 0;
  }
  function answer(rating) {
    var cardId, reviewToken, answerEpoch;
    if (!state.card || state.cardLoading || state.answerInFlight || state.undoInFlight || state.syncInFlight) return;
    cardId = state.card.id; reviewToken = state.card.reviewToken;
    if (!reviewToken) { warning("Reloading card..."); nextCard(); return; }
    state.inputEpoch += 1;
    answerEpoch = state.inputEpoch;
    state.answerInFlight = true;
    request("POST", "/api/answer", "card=" + encodeURIComponent(cardId) +
      "&token=" + encodeURIComponent(reviewToken) + "&rating=" + rating,
      function (error, result) {
        var message;
        if (answerEpoch !== state.inputEpoch) return;
        if (error || !result || result.type === "error") {
          message = error || (result && result.message) || "Answer failed.";
          clearPhysicalInput(function () {
            if (answerEpoch !== state.inputEpoch) return;
            state.answerInFlight = false;
            if (isReviewStateError(message)) { warning("Reloading card..."); nextCard(); }
            else { warning(message); resumePageButtonInput(); }
          });
          return;
        }
        state.answerInFlight = false;
        state.reviewed += 1; storePendingFromResult(result, pendingReviews + 1);
        if (result.statePersisted === false)
          warning("Review saved, but AnkINK could not persist its local state.");
        scheduleAutomaticRefresh(); nextCard();
      }, 0);
  }
  function showAnswer() {
    if (state.card && !state.cardLoading && !state.answerInFlight && !state.undoInFlight && !state.syncInFlight) {
      state.answerShown = true; show(byId("back-face")); show(byId("answer-divider"));
      hide(byId("show-controls")); show(byId("rating-controls"));
      if (byId("answer-divider").scrollIntoView) byId("answer-divider").scrollIntoView(true);
      scheduleScrollButtonUpdate();
    }
  }
  function undoAnswer() {
    var undoEpoch;
    if (state.cardLoading || state.answerInFlight || state.undoInFlight || state.syncInFlight) return;
    state.inputEpoch += 1;
    undoEpoch = state.inputEpoch;
    state.undoInFlight = true;
    request("POST", "/api/undo", "", function (error, result) {
      if (undoEpoch !== state.inputEpoch) return;
      clearPhysicalInput(function () {
        if (undoEpoch !== state.inputEpoch) return;
        state.undoInFlight = false;
        if (error || !result) { warning(error || "Undo failed."); resumePageButtonInput(); return; }
        if (result.type === "undo-empty") { resumePageButtonInput(); return; }
        if (result.type === "error") { warning(result.message); resumePageButtonInput(); return; }
        if (state.reviewed > 0) state.reviewed -= 1;
        storePendingFromResult(result, pendingReviews - 1);
        if (result.statePersisted === false)
          warning("Undo succeeded, but AnkINK could not persist its local state.");
        else warning("");
        nextCard();
      });
    }, 0);
  }
  function cardCanScrollDown() {
    var card = byId("card");
    return card.scrollTop + card.clientHeight < card.scrollHeight - 8;
  }
  function scrollCardForward() {
    var card = byId("card");
    card.scrollTop = Math.min(card.scrollHeight - card.clientHeight,
      card.scrollTop + Math.max(1, Math.floor(card.clientHeight * 0.85)));
  }
  function fullRefresh() {
    if (state.refreshInFlight) return;
    state.refreshInFlight = true;
    request("POST", "/api/refresh", "", function (error) {
      state.refreshInFlight = false;
      if (error) warning(error);
    }, 0);
  }
  function scheduleAutomaticRefresh() {
    if (fullRefreshMode === "manual") return;
    if (fullRefreshMode === "every-card") {
      state.refreshAfterCard = true;
      return;
    }
    saveFullRefreshProgress(reviewsSinceFullRefresh + 1);
    if (reviewsSinceFullRefresh >= 5) {
      saveFullRefreshProgress(0);
      state.refreshAfterCard = true;
    }
  }
  function runScheduledRefresh() {
    if (!state.refreshAfterCard) return;
    state.refreshAfterCard = false;
    fullRefresh();
  }
  var inputWaitTimer = null;
  function reviewerReadyForInput() {
    return !!state.deck && byId("review-view").className.indexOf("hidden") < 0 &&
      byId("settings-dialog").className.indexOf("hidden") >= 0 &&
      byId("about-dialog").className.indexOf("hidden") >= 0 &&
      !state.cardLoading && !state.answerInFlight && !state.undoInFlight && !state.syncInFlight;
  }
  function deckListReadyForInput() {
    return !state.deck && byId("decks-view").className.indexOf("hidden") < 0 &&
      byId("settings-dialog").className.indexOf("hidden") >= 0 &&
      byId("about-dialog").className.indexOf("hidden") >= 0 &&
      byId("auth-panel").className.indexOf("hidden") >= 0 &&
      byId("full-sync-panel").className.indexOf("hidden") >= 0 &&
      !state.cardLoading && !state.answerInFlight && !state.undoInFlight && !state.syncInFlight;
  }
  function pageButtonInputReady() {
    return reviewerReadyForInput() || deckListReadyForInput();
  }
  function resumePageButtonInput(delay) {
    if (!pageButtonInputReady() || state.inputBusy || inputWaitTimer !== null) return;
    inputWaitTimer = window.setTimeout(function () {
      inputWaitTimer = null; pollPageButtons();
    }, delay || 0);
  }
  function pollPageButtons() {
    var pollEpoch;
    if (state.inputBusy || !pageButtonInputReady()) return;
    pollEpoch = state.inputEpoch;
    state.inputBusy = true;
    request("GET", "/api/input", null, function (error, input) {
      var advance;
      state.inputBusy = false;
      if (pollEpoch !== state.inputEpoch) { resumePageButtonInput(); return; }
      if (!pageButtonInputReady()) return;
      if (error || !input) { resumePageButtonInput(500); return; }
      if (!input.action) { resumePageButtonInput(); return; }
      if (deckListReadyForInput()) {
        pageScroll(input.action === "forward" ? 1 : -1);
        resumePageButtonInput();
        return;
      }
      advance = (pageButtonMode === "normal" && input.action === "forward") ||
        (pageButtonMode === "reversed" && input.action === "backward");
      if (advance) {
        if (!state.card) { resumePageButtonInput(); return; }
        if (cardCanScrollDown()) scrollCardForward();
        else if (state.answerShown) answer(3); else showAnswer();
      } else undoAnswer();
      resumePageButtonInput();
    }, 0);
  }
  byId("show-answer").onclick = showAnswer;
  byId("rating-1").onclick = function () { answer(1); }; byId("rating-2").onclick = function () { answer(2); };
  byId("rating-3").onclick = function () { answer(3); }; byId("rating-4").onclick = function () { answer(4); };
  byId("back").onclick = function () {
    if (state.cardLoading || state.answerInFlight || state.undoInFlight || state.syncInFlight) {
      warning("Please wait for the current operation to finish."); return;
    }
    state.inputEpoch += 1;
    hide(byId("review-view")); show(byId("decks-view")); state.deck = null; state.card = null;
    show(byId("sync"));
    byId("decks-view").scrollTop = 0;
    scheduleScrollButtonUpdate();
    clearPhysicalInput(function () { loadDecks(); });
  };
  byId("refresh").onclick = function () {
    fullRefresh();
  };
  byId("sync").onclick = function () { syncNow(); };
  byId("font-plus").onclick = function () { changeFontScale(1); };
  byId("font-minus").onclick = function () { changeFontScale(-1); };
  byId("night-mode").onclick = function () { nightMode = !nightMode; applyNightMode(); };
  byId("scroll-up").onclick = function () { pageScroll(-1); };
  byId("scroll-down").onclick = function () { pageScroll(1); };
  byId("decks-view").onscroll = updateScrollButtons;
  byId("card").onscroll = updateScrollButtons;
  window.onresize = scheduleScrollButtonUpdate;
  byId("settings").onclick = function () {
    selectedRadio("page-buttons", pageButtonMode);
    selectedRadio("full-refresh", fullRefreshMode);
    selectedRadio("night-card-mode", nightCardMode);
    byId("card-font").value = cardFont;
    hideSettingsTooltip();
    show(byId("settings-dialog"));
  };
  byId("settings-close").onclick = function () {
    hideSettingsTooltip(); hide(byId("settings-dialog")); resumePageButtonInput();
  };
  byId("settings-about").onclick = openAbout;
  byId("about").onclick = openAbout;
  byId("about-done").onclick = closeAbout;
  byId("settings-logout").onclick = function () {
    hide(byId("settings-dialog"));
    show(byId("account-dialog"));
  };
  byId("card-font").onchange = function () {
    if (!cardFonts[this.value]) return;
    cardFont = this.value;
    applyCardFont();
  };
  (function () {
    var fontSizeButtons = document.getElementsByName("card-font-size"), i;
    for (i = 0; i < fontSizeButtons.length; ++i) fontSizeButtons[i].onclick = function () {
      fontScale = parseFloat(this.getAttribute("data-scale"));
      applyFontScale();
    };
  }());
  (function () {
    var helpButtons = document.getElementsByClassName("setting-help"), i;
    for (i = 0; i < helpButtons.length; ++i) helpButtons[i].onclick = function (event) {
      event = event || window.event;
      if (event.stopPropagation) event.stopPropagation();
      else event.cancelBubble = true;
      showSettingsTooltip(this);
    };
    byId("settings-tooltip").onclick = hideSettingsTooltip;
    byId("settings-dialog").onscroll = hideSettingsTooltip;
    document.onclick = hideSettingsTooltip;
  }());
  (function () {
    var pageButtons = document.getElementsByName("page-buttons");
    var fullRefreshButtons = document.getElementsByName("full-refresh");
    var nightCardModes = document.getElementsByName("night-card-mode");
    var i;
    for (i = 0; i < pageButtons.length; ++i) pageButtons[i].onclick = function () {
      pageButtonMode = this.value;
      saveSetting("pageButtonMode", pageButtonMode);
    };
    for (i = 0; i < fullRefreshButtons.length; ++i) fullRefreshButtons[i].onclick = function () {
      fullRefreshMode = this.value;
      saveSetting("fullRefreshMode", fullRefreshMode);
      saveFullRefreshProgress(0);
    };
    for (i = 0; i < nightCardModes.length; ++i) nightCardModes[i].onclick = function () {
      nightCardMode = this.value;
      saveSetting("nightCardMode", nightCardMode);
      applyNightCardAppearance();
    };
  }());
  byId("account-cancel").onclick = function () { hide(byId("account-dialog")); };
  byId("account-logout").onclick = function () {
    byId("status").innerHTML = "Logging out...";
    request("POST", "/api/auth/logout", "", function (error, result) {
      if (error || !result || result.type === "error") {
        hide(byId("account-dialog")); warning(error || (result && result.message) || "Logout failed."); return;
      }
      byId("status").innerHTML = "Sign in to AnkiWeb"; showLogin();
    }, 0);
  };
  byId("auth-cancel").onclick = closeApplication;
  byId("auth-submit").onclick = function () {
    var username = byId("ankiweb-username").value;
    var password = byId("ankiweb-password").value;
    if (!username || !password) { warning("Enter your AnkiWeb email and password."); return; }
    byId("status").innerHTML = "Signing into AnkiWeb...";
    request("POST", "/api/auth/login", "username=" + encodeURIComponent(username) + "&password=" + encodeURIComponent(password),
      function (error, result) {
        byId("ankiweb-password").value = "";
        if (error || !result || result.type === "error") { warning(error || (result && result.message) || "Sign-in failed."); return; }
        hide(byId("auth-panel")); syncNow();
      }, 0);
  };
  byId("full-sync-cancel").onclick = function () {
    state.syncInFlight = false; hide(byId("full-sync-panel")); updateSyncStatus();
  };
  function downloadFromAnkiWeb(done) {
    if (state.syncInFlight) { if (done) done(false); return; }
    state.syncInFlight = true;
    state.inputEpoch += 1;
    hide(byId("full-sync-panel")); byId("status").innerHTML = "Downloading collection from AnkiWeb...";
    request("POST", "/api/sync/full-download", "", function (error, result) {
      state.syncInFlight = false;
      if (error || !result || result.type === "error") {
        warning(error || (result && result.message) || "Download failed.");
        updateSyncStatus(); if (done) done(false); return;
      }
      if (result.statePersisted === false) {
        warning("Download succeeded, but AnkINK could not save its local state.");
        storePendingFromResult(result, pendingReviews);
        if (done) done(false); return;
      }
      storePendingFromResult(result, 0);
      if (done) done(true); else loadDecks();
    }, 0);
  }
  byId("full-download").onclick = function () { downloadFromAnkiWeb(); };
  byId("close").onclick = closeApplication;
  byId("about-logo").onload = prepareAboutLogo;
  loadSettings();
}());
