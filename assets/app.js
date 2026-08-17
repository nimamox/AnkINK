(function () {
  "use strict";
  var API = "http://127.0.0.1:8765";
  var MEDIA_VERSION = String(new Date().getTime());
  var state = { deck: null, card: null, reviewed: 0, answerShown: false, inputBusy: false };
  var warningTimer = null;
  var initialSyncAttempted = false;
  var pendingReviews = parseInt(window.localStorage.getItem("ankink_pending_reviews") || "0", 10) || 0;
  var fontScale = parseFloat(window.localStorage.getItem("ankink_font_scale") || "1");
  var nightMode = window.localStorage.getItem("ankink_night_mode") === "1";
  function byId(id) { return document.getElementById(id); }
  function hide(element) { if (element.className.indexOf("hidden") < 0) element.className += " hidden"; }
  function show(element) { element.className = element.className.replace(/(^|\s)hidden(?=\s|$)/g, ""); }
  function clear(element) { while (element.firstChild) element.removeChild(element.firstChild); }
  function deckName(name) { return (name || "").split("\u001f").join("::"); }
  function resetCardScroll() { byId("card").scrollTop = 0; }
  function updateSyncStatus() {
    byId("status").innerHTML = pendingReviews > 0
      ? "&#9679; " + pendingReviews + " review" + (pendingReviews === 1 ? "" : "s") + " not synced"
      : "&#10003; Synced";
  }
  function storePendingReviews(value) {
    pendingReviews = Math.max(0, value);
    window.localStorage.setItem("ankink_pending_reviews", String(pendingReviews));
    updateSyncStatus();
  }
  function closeInterface() {
    if (window.kindle && window.kindle.appmgr && window.kindle.appmgr.back) window.kindle.appmgr.back();
    else window.close();
  }
  function closeApplication() {
    if (state.closing) return;
    state.closing = true;
    if (pendingReviews > 0) {
      request("POST", "/api/sync", "", function (error, result) {
        if (!error && result && result.type !== "error" && result.required === "none") storePendingReviews(0);
        request("POST", "/api/quit", "", function () {}, 0);
      }, 0);
    } else request("POST", "/api/quit", "", function () {}, 0);
    window.setTimeout(closeInterface, 750);
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
  function prepareImages(root) {
    var images = root.getElementsByTagName("img"), i, filename;
    for (i = 0; i < images.length; ++i) {
      filename = images[i].getAttribute("data-ankink-media");
      if (filename) loadMediaImage(images[i], filename);
      images[i].onclick = function () {
        if (this.className.indexOf("image-expanded") >= 0)
          this.className = this.className.replace(/(^|\s)image-expanded(?=\s|$)/g, "");
        else {
          this.className += " image-expanded";
          if (this.scrollIntoView) this.scrollIntoView(true);
        }
      };
    }
  }
  function loadMediaImage(image, filename) {
    request("GET", "/api/media-data/" + encodeURIComponent(filename) + "?v=" + MEDIA_VERSION,
      null, function (error, data) {
        if (!error && data && typeof data.data === "string" && data.data.indexOf("data:image/") === 0)
          image.setAttribute("src", data.data);
        else
          image.setAttribute("src", API + "/api/media/" + encodeURIComponent(filename) + "?v=" + MEDIA_VERSION);
      }, 0);
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
      var cursor = 0, opening, contentStart, closing, span, expression;
      while ((opening = findOpening(text, cursor))) {
        contentStart = opening.position + opening.delimiter.left.length;
        closing = text.indexOf(opening.delimiter.right, contentStart);
        if (closing < 0) break;
        if (opening.position > cursor) fragment.appendChild(document.createTextNode(text.substring(cursor, opening.position)));
        expression = text.substring(contentStart, closing);
        span = document.createElement("span");
        try {
          window.katex.render(expression, span, {
            displayMode: opening.delimiter.display,
            throwOnError: false,
            strict: "ignore",
            trust: false
          });
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
    if (!window.katex || !window.katex.render) return;
    collect(root);
    for (var i = 0; i < nodes.length; ++i) replace(nodes[i]);
  }
  function loadDecks(skipInitialSync) {
    warning(""); byId("status").innerHTML = "Connecting to AnkINK engine...";
    request("GET", "/api/status", null, function (error, status) {
      if (error) { warning(error); byId("status").innerHTML = "Engine unavailable"; return; }
      if (!status.authenticated) {
        byId("status").innerHTML = "Sign in to AnkiWeb"; showLogin(); return;
      }
      hide(byId("auth-panel")); hide(byId("review-view")); show(byId("decks-view"));
      updateSyncStatus();
      request("GET", "/api/decks", null, function (deckError, message) {
        var list = byId("decks"), i, button, name, count, arrow, deck;
        clear(list); byId("collection-path").innerHTML = "";
        byId("collection-path").appendChild(document.createTextNode((message && message.path) || status.collection || ""));
        if (deckError || !message || !message.decks || !message.decks.length) {
          var empty = document.createElement("div"); empty.className = "empty-state";
          empty.appendChild(document.createTextNode(deckError || "This collection contains no decks.")); list.appendChild(empty);
          if (deckError) warning(deckError); return;
        }
        for (i = 0; i < message.decks.length; ++i) {
          deck = message.decks[i]; button = document.createElement("button"); button.className = "deck"; button.type = "button";
          name = document.createElement("span"); name.className = "deck-name"; name.appendChild(document.createTextNode(deckName(deck.name)));
          count = document.createElement("span"); count.className = "deck-count"; count.appendChild(document.createTextNode(deck.cards + " due"));
          arrow = document.createElement("span"); arrow.className = "deck-arrow"; arrow.innerHTML = "&rsaquo;";
          button.appendChild(name); button.appendChild(count); button.appendChild(arrow);
          button.onclick = (function (selected) { return function () { openDeck(selected); }; }(deck)); list.appendChild(button);
        }
        if (!skipInitialSync && !initialSyncAttempted) {
          initialSyncAttempted = true;
          syncNow(function (success) { if (success) loadDecks(true); });
        }
      }, 0);
    }, 12);
  }
  function applyFontScale() {
    byId("front").style.fontSize = Math.round(32 * fontScale) + "px";
    byId("back-face").style.fontSize = Math.round(26 * fontScale) + "px";
    window.localStorage.setItem("ankink_font_scale", String(fontScale));
  }
  function applyNightMode() {
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
    window.localStorage.setItem("ankink_night_mode", nightMode ? "1" : "0");
  }
  function updateCounts(counts) {
    counts = counts || { "new": 0, learning: 0, review: 0 };
    byId("session-count").innerHTML = counts["new"] + " new &middot; " +
      counts.learning + " learn &middot; " + counts.review + " review";
  }
  function openDeck(deck) {
    state.deck = deck; state.reviewed = 0; byId("review-title").innerHTML = "";
    byId("review-title").appendChild(document.createTextNode(deckName(deck.name)));
    updateCounts(deck); hide(byId("decks-view")); show(byId("review-view")); nextCard();
  }
  function showLogin() {
    state.deck = null; state.card = null; warning("");
    hide(byId("review-view")); hide(byId("decks-view")); hide(byId("account-dialog"));
    show(byId("auth-panel")); hide(byId("full-sync-panel"));
    byId("ankiweb-password").value = ""; window.scrollTo(0, 0);
  }
  function syncNow(done) {
    warning(""); byId("status").innerHTML = "Synchronizing with AnkiWeb...";
    request("POST", "/api/sync", "", function (error, result) {
      if (error || !result || result.type === "error") {
        var message = error || (result && result.message) || "Sync failed.";
        byId("status").innerHTML = "Sync unavailable";
        if (message.indexOf("Sign in") >= 0) showLogin(); else warning(message);
        updateSyncStatus(); if (done) done(false);
        return;
      }
      if (result.required === "full") {
        byId("status").innerHTML = "Full sync required";
        if (result.downloadAllowed && !result.uploadAllowed) {
          downloadFromAnkiWeb(done); return;
        }
        if (result.downloadAllowed) show(byId("full-sync-panel"));
        else warning("AnkiWeb requires a full sync, but download is not currently allowed.");
        window.scrollTo(0, 0); if (done) done(false); return;
      }
      storePendingReviews(0);
      if (result.message) warning(result.message);
      if (done) done(true); else loadDecks(true);
    }, 0);
  }
  function nextCard() {
    state.answerShown = false;
    resetCardScroll();
    byId("front").innerHTML = "Loading..."; hide(byId("back-face")); hide(byId("answer-divider"));
    hide(byId("rating-controls")); show(byId("show-controls"));
    request("GET", "/api/decks/" + state.deck.id + "/next", null, function (error, card) {
      if (error) { warning(error); return; }
      if (card.type === "error") { warning(card.message); return; }
      if (card.type === "complete") {
        applyCardCss("");
        state.card = null; byId("front").innerHTML = "<h2>Session complete</h2><p>You reviewed " + state.reviewed + " cards.</p>";
        hide(byId("show-controls")); return;
      }
      state.card = card; applyCardCss(card.css); safeHtml(byId("front"), card.front, "Empty front field");
      safeHtml(byId("back-face"), answerOnly(card.back), "No additional fields"); resetCardScroll();
      updateCounts(card.counts);
      if (card.buttons && card.buttons.length === 4) {
        for (var i = 0; i < 4; ++i) {
          byId("rating-" + (i + 1)).getElementsByTagName("small")[0].innerHTML = card.buttons[i].interval;
        }
      }
    }, 1);
  }
  function answer(rating) {
    if (!state.card) return;
    request("POST", "/api/answer", "card=" + encodeURIComponent(state.card.id) + "&rating=" + rating,
      function (error, result) { if (error || (result && result.type === "error")) warning(error || result.message); else { state.reviewed += 1; storePendingReviews(pendingReviews + 1); nextCard(); } }, 0);
  }
  function showAnswer() {
    if (state.card) {
      state.answerShown = true; show(byId("back-face")); show(byId("answer-divider"));
      hide(byId("show-controls")); show(byId("rating-controls"));
      if (byId("answer-divider").scrollIntoView) byId("answer-divider").scrollIntoView(true);
    }
  }
  function undoAnswer() {
    request("POST", "/api/undo", "", function (error, result) {
      if (error || (result && result.type === "error")) warning(error || result.message);
      else {
        if (state.reviewed > 0) state.reviewed -= 1;
        storePendingReviews(pendingReviews > 0 ? pendingReviews - 1 : 1);
        warning(""); nextCard();
      }
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
  function pollPageButtons() {
    if (state.inputBusy) return;
    state.inputBusy = true;
    request("GET", "/api/input", null, function (error, input) {
      state.inputBusy = false;
      if (error || !input || !state.deck || !state.card || !input.action) return;
      if (input.action === "forward") {
        if (cardCanScrollDown()) scrollCardForward();
        else if (state.answerShown) answer(3); else showAnswer();
      } else if (input.action === "backward") {
        if (state.answerShown) answer(1); else undoAnswer();
      }
    }, 0);
  }
  byId("show-answer").onclick = showAnswer;
  byId("rating-1").onclick = function () { answer(1); }; byId("rating-2").onclick = function () { answer(2); };
  byId("rating-3").onclick = function () { answer(3); }; byId("rating-4").onclick = function () { answer(4); };
  byId("back").onclick = function () {
    hide(byId("review-view")); show(byId("decks-view")); state.deck = null; state.card = null;
    byId("decks-view").scrollTop = 0;
    if (pendingReviews > 0) syncNow(function () { loadDecks(true); });
  };
  byId("refresh").onclick = function () {
    request("POST", "/api/refresh", "", function (error) { if (error) warning(error); }, 0);
  };
  byId("sync").onclick = function () { syncNow(); };
  byId("font-plus").onclick = function () { fontScale = Math.min(1.6, fontScale + 0.1); applyFontScale(); };
  byId("font-minus").onclick = function () { fontScale = Math.max(0.7, fontScale - 0.1); applyFontScale(); };
  byId("night-mode").onclick = function () { nightMode = !nightMode; applyNightMode(); };
  byId("account").onclick = function () { show(byId("account-dialog")); };
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
  byId("full-sync-cancel").onclick = function () { hide(byId("full-sync-panel")); };
  function downloadFromAnkiWeb(done) {
    hide(byId("full-sync-panel")); byId("status").innerHTML = "Downloading collection from AnkiWeb...";
    request("POST", "/api/sync/full-download", "", function (error, result) {
      if (error || !result || result.type === "error") {
        warning(error || (result && result.message) || "Download failed.");
        updateSyncStatus(); if (done) done(false); return;
      }
      storePendingReviews(0);
      if (done) done(true); else loadDecks(true);
    }, 0);
  }
  byId("full-download").onclick = function () { downloadFromAnkiWeb(); };
  byId("close").onclick = closeApplication;
  applyFontScale(); applyNightMode(); loadDecks(); window.setInterval(pollPageButtons, 250);
}());
