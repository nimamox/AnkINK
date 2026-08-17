(() => {
  "use strict";

  const state = { deck: null, card: null, reviewed: 0 };
  const demoMode = new URLSearchParams(window.location.search).has("demo");
  const $ = (id) => document.getElementById(id);
  const decksView = $("decks-view");
  const reviewView = $("review-view");

  function native(message) {
    if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ankink) {
      window.webkit.messageHandlers.ankink.postMessage(message);
    } else if (demoMode) {
      const [action, id, rating] = message.split(":");
      if (action === "ready") {
        setTimeout(() => window.AnkINK.receive({type: "status", version: "demo", width: 1080, height: 1440, touch: true, warning: ""}), 0);
        setTimeout(() => window.AnkINK.receive({type: "decks", path: "/mnt/us/ankink/collection.anki2", decks: [
          {id: 1, name: "Languages::French", cards: 184},
          {id: 2, name: "Computer Science", cards: 72},
          {id: 3, name: "Art & Architecture", cards: 39}
        ]}), 0);
      } else if (action === "next") {
        setTimeout(() => window.AnkINK.receive({type: "card", deckId: Number(id), id: 101, front: "What does <strong>bonjour</strong> mean?", back: "Hello<br><small>A greeting used during the day.</small>"}), 0);
      } else if (action === "answer") {
        setTimeout(() => window.AnkINK.receive({type: "answered", id: Number(id), rating: Number(rating)}), 0);
      }
    }
  }

  function showWarning(message) {
    const warning = $("warning");
    warning.textContent = message || "";
    warning.classList.toggle("hidden", !message);
  }

  function renderDecks(message) {
    $("collection-path").textContent = message.path || "";
    const list = $("decks");
    list.replaceChildren();
    if (!message.decks || message.decks.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-state";
      empty.innerHTML = message.error
        ? "No collection is available.<br>Copy a modern <code>collection.anki2</code> to <code>/mnt/us/ankink/</code>."
        : "This collection contains no decks.";
      list.appendChild(empty);
      return;
    }
    message.decks.forEach((deck) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "deck";
      button.innerHTML = `<span class="deck-name"></span><span class="deck-count"></span><span class="deck-arrow">›</span>`;
      button.querySelector(".deck-name").textContent = deck.name.replaceAll("\u001f", "::");
      button.querySelector(".deck-count").textContent = `${deck.cards} cards`;
      button.addEventListener("click", () => openDeck(deck));
      list.appendChild(button);
    });
  }

  function openDeck(deck) {
    state.deck = deck;
    state.reviewed = 0;
    $("review-title").textContent = deck.name.replaceAll("\u001f", "::");
    $("session-count").textContent = "0 reviewed";
    decksView.classList.add("hidden");
    reviewView.classList.remove("hidden");
    requestNext();
  }

  function requestNext() {
    setCardLoading();
    native(`next:${state.deck.id}`);
  }

  function setCardLoading() {
    $("front").textContent = "Loading…";
    $("back-face").replaceChildren();
    $("back-face").classList.add("hidden");
    $("answer-divider").classList.add("hidden");
    $("rating-controls").classList.add("hidden");
    $("show-controls").classList.remove("hidden");
  }

  function setSafeCardHtml(element, html, emptyText) {
    const template = document.createElement("template");
    template.innerHTML = html || `<em>${emptyText}</em>`;
    template.content.querySelectorAll("script, style, iframe, object, embed, link, meta").forEach((node) => node.remove());
    template.content.querySelectorAll("*").forEach((node) => {
      Array.from(node.attributes).forEach((attribute) => {
        const name = attribute.name.toLowerCase();
        if (name.startsWith("on") || name === "style" || name === "srcset") node.removeAttribute(attribute.name);
        if (["src", "href", "xlink:href"].includes(name)) {
          const value = attribute.value.trim().toLowerCase();
          if (!value.startsWith("data:image/") && !value.startsWith("#")) node.removeAttribute(attribute.name);
        }
      });
    });
    element.replaceChildren(template.content.cloneNode(true));
  }

  function renderCard(card) {
    state.card = card;
    setSafeCardHtml($("front"), card.front, "Empty front field");
    setSafeCardHtml($("back-face"), card.back, "No additional fields");
    $("back-face").classList.add("hidden");
    $("answer-divider").classList.add("hidden");
    $("rating-controls").classList.add("hidden");
    $("show-controls").classList.remove("hidden");
    window.scrollTo(0, 0);
  }

  function renderComplete() {
    state.card = null;
    $("front").innerHTML = `<h2>Session complete</h2><p>You reviewed ${state.reviewed} card${state.reviewed === 1 ? "" : "s"}.</p>`;
    $("back-face").classList.add("hidden");
    $("answer-divider").classList.add("hidden");
    $("show-controls").classList.add("hidden");
    $("rating-controls").classList.add("hidden");
  }

  window.AnkINK = {
    receive(message) {
      switch (message.type) {
        case "status":
          $("status").textContent = `v${message.version} · ${message.width}×${message.height} · ${message.touch ? "touch ready" : "no touch"}`;
          showWarning(message.warning);
          break;
        case "decks": renderDecks(message); break;
        case "card": renderCard(message); break;
        case "answered":
          state.reviewed += 1;
          $("session-count").textContent = `${state.reviewed} reviewed`;
          requestNext();
          break;
        case "complete": renderComplete(); break;
        case "error": showWarning(message.message); break;
      }
    }
  };

  $("show-answer").addEventListener("click", () => {
    if (!state.card) return;
    $("back-face").classList.remove("hidden");
    $("answer-divider").classList.remove("hidden");
    $("show-controls").classList.add("hidden");
    $("rating-controls").classList.remove("hidden");
  });

  $("rating-controls").addEventListener("click", (event) => {
    const button = event.target.closest("button[data-rating]");
    if (button && state.card) native(`answer:${state.card.id}:${button.dataset.rating}`);
  });

  $("back").addEventListener("click", () => {
    reviewView.classList.add("hidden");
    decksView.classList.remove("hidden");
    state.deck = null;
    state.card = null;
    window.scrollTo(0, 0);
  });
  $("refresh").addEventListener("click", () => native("refresh"));
  $("quit").addEventListener("click", () => native("quit"));

  document.addEventListener("keydown", (event) => {
    const forward = ["PageDown", "ArrowDown", "ArrowRight"].includes(event.key);
    const backward = ["PageUp", "ArrowUp", "ArrowLeft"].includes(event.key);
    if (!forward && !backward) return;
    event.preventDefault();
    if (!reviewView.classList.contains("hidden") && state.card) {
      if ($("back-face").classList.contains("hidden")) {
        $("show-answer").click();
      } else {
        const rating = backward ? "1" : "3";
        $("rating-controls").querySelector(`[data-rating="${rating}"]`).click();
      }
      return;
    }
    const decks = Array.from(document.querySelectorAll(".deck"));
    if (decks.length) {
      const current = Math.max(0, decks.indexOf(document.activeElement));
      decks[Math.max(0, Math.min(decks.length - 1, current + (forward ? 1 : -1)))].focus();
    }
  });

  native("ready");
})();
