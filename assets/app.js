(function () {
  "use strict";
  var API = "http://127.0.0.1:8765";
  var state = { deck: null, card: null, reviewed: 0 };
  function byId(id) { return document.getElementById(id); }
  function hide(element) { if (element.className.indexOf("hidden") < 0) element.className += " hidden"; }
  function show(element) { element.className = element.className.replace(/(^|\s)hidden(?=\s|$)/g, ""); }
  function clear(element) { while (element.firstChild) element.removeChild(element.firstChild); }
  function deckName(name) { return (name || "").split("\u001f").join("::"); }
  function warning(message) {
    byId("warning").innerHTML = "";
    byId("warning").appendChild(document.createTextNode(message || ""));
    if (message) show(byId("warning")); else hide(byId("warning"));
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
              if (name.indexOf("on") === 0 || name === "style" || name === "srcset" ||
                  ((name === "src" || name === "href") && value.indexOf("data:image/") !== 0 && value.indexOf("#") !== 0))
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
  }
  function loadDecks() {
    warning(""); byId("status").innerHTML = "Connecting to AnkINK engine...";
    request("GET", "/api/status", null, function (error, status) {
      if (error) { warning(error); byId("status").innerHTML = "Engine unavailable"; return; }
      byId("status").innerHTML = "Engine v" + status.version + (status.collectionOpen ? " ready" : " - collection error");
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
          count = document.createElement("span"); count.className = "deck-count"; count.appendChild(document.createTextNode(deck.cards + " cards"));
          arrow = document.createElement("span"); arrow.className = "deck-arrow"; arrow.innerHTML = "&rsaquo;";
          button.appendChild(name); button.appendChild(count); button.appendChild(arrow);
          button.onclick = (function (selected) { return function () { openDeck(selected); }; }(deck)); list.appendChild(button);
        }
      }, 0);
    }, 12);
  }
  function openDeck(deck) {
    state.deck = deck; state.reviewed = 0; byId("review-title").innerHTML = "";
    byId("review-title").appendChild(document.createTextNode(deckName(deck.name)));
    byId("session-count").innerHTML = "0 reviewed"; hide(byId("decks-view")); show(byId("review-view")); nextCard();
  }
  function nextCard() {
    byId("front").innerHTML = "Loading..."; hide(byId("back-face")); hide(byId("answer-divider"));
    hide(byId("rating-controls")); show(byId("show-controls"));
    request("GET", "/api/decks/" + state.deck.id + "/next", null, function (error, card) {
      if (error) { warning(error); return; }
      if (card.type === "error") { warning(card.message); return; }
      if (card.type === "complete") {
        state.card = null; byId("front").innerHTML = "<h2>Session complete</h2><p>You reviewed " + state.reviewed + " cards.</p>";
        hide(byId("show-controls")); return;
      }
      state.card = card; safeHtml(byId("front"), card.front, "Empty front field");
      safeHtml(byId("back-face"), card.back, "No additional fields"); window.scrollTo(0, 0);
    }, 1);
  }
  function answer(rating) {
    if (!state.card) return;
    request("POST", "/api/answer", "card=" + encodeURIComponent(state.card.id) + "&rating=" + rating,
      function (error, result) { if (error || (result && result.type === "error")) warning(error || result.message); else { state.reviewed += 1; byId("session-count").innerHTML = state.reviewed + " reviewed"; nextCard(); } }, 0);
  }
  byId("show-answer").onclick = function () { if (state.card) { show(byId("back-face")); show(byId("answer-divider")); hide(byId("show-controls")); show(byId("rating-controls")); } };
  byId("rating-1").onclick = function () { answer(1); }; byId("rating-2").onclick = function () { answer(2); };
  byId("rating-3").onclick = function () { answer(3); }; byId("rating-4").onclick = function () { answer(4); };
  byId("back").onclick = function () { hide(byId("review-view")); show(byId("decks-view")); state.deck = null; state.card = null; window.scrollTo(0, 0); };
  byId("refresh").onclick = loadDecks;
  loadDecks();
}());
