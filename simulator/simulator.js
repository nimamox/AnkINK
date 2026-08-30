(function () {
  "use strict";
  var DEFAULT_DEVICE = "kindle-oasis-8";
  var devices = window.ANKINK_KINDLE_DEVICES || [];
  var selector = document.getElementById("device");
  var orientationSelector = document.getElementById("orientation");
  var frame = document.getElementById("device-frame");
  var screen = document.getElementById("device-screen");
  var iframe = document.getElementById("ankink");
  var details = document.getElementById("details");
  var fit = true;
  var orientation = "portrait";
  var current;

  function findDevice(id) {
    var i;
    for (i = 0; i < devices.length; ++i) if (devices[i].id === id) return devices[i];
    for (i = 0; i < devices.length; ++i) if (devices[i].id === DEFAULT_DEVICE) return devices[i];
    return devices[0];
  }
  function scaleFor(device) {
    var size = dimensions();
    if (!fit) return 1;
    return Math.min(1,
      (window.innerWidth - 250) / size.width,
      (window.innerHeight - 118) / size.height);
  }
  function dimensions() {
    return orientation === "landscape" ?
      { width: current.height, height: current.width } :
      { width: current.width, height: current.height };
  }
  function layout() {
    if (!current) return;
    var scale = Math.max(0.1, scaleFor(current));
    var size = dimensions();
    var statusHeight = current.statusBarHeight || 40;
    iframe.style.width = size.width + "px";
    iframe.style.height = (size.height - statusHeight) + "px";
    document.getElementById("kindle-status").style.height = statusHeight + "px";
    screen.style.width = size.width + "px";
    screen.style.height = size.height + "px";
    screen.style.transform = "scale(" + scale + ")";
    frame.style.width = Math.round(size.width * scale) + 18 + "px";
    frame.style.height = Math.round(size.height * scale) + 18 + "px";
    details.innerHTML = size.width + " &times; " + size.height +
      " &middot; " + orientation + " &middot; " + current.family +
      " &middot; " + Math.round(scale * 100) + "%";
    document.getElementById("fit").innerHTML = fit ? "100%" : "Fit window";
  }
  function selectDevice(id) {
    current = findDevice(id);
    selector.value = current.id;
    window.localStorage.setItem("ankink_simulator_device", current.id);
    layout();
  }
  function notifyOrientation() {
    var view, documentInFrame, event;
    try {
      view = iframe.contentWindow;
      documentInFrame = iframe.contentDocument || view.document;
    } catch (ignored) { return; }
    try { view.orientation = orientation === "landscape" ? 90 : 0; }
    catch (ignoredOrientation) {}
    try {
      if (documentInFrame && documentInFrame.createEvent) {
        event = documentInFrame.createEvent("Event");
        event.initEvent("orientationchange", false, false);
        view.dispatchEvent(event);
      }
    } catch (ignoredEvent) {}
  }
  function selectOrientation(value) {
    orientation = value === "landscape" ? "landscape" : "portrait";
    orientationSelector.value = orientation;
    layout();
    window.setTimeout(notifyOrientation, 0);
  }
  function sendInput(action) {
    var xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/simulator/input", true);
    xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
    xhr.send("action=" + encodeURIComponent(action));
  }
  function installHostFonts() {
    var documentInFrame;
    var link;
    try {
      documentInFrame = iframe.contentDocument || iframe.contentWindow.document;
      if (!documentInFrame || !documentInFrame.head ||
          documentInFrame.getElementById("ankink-simulator-host-fonts")) return;
      link = documentInFrame.createElement("link");
      link.id = "ankink-simulator-host-fonts";
      link.rel = "stylesheet";
      link.href = "/simulator/host-fonts.css";
      documentInFrame.head.appendChild(link);
    } catch (ignored) {
      /* The normal fallback fonts remain usable if iframe access is unavailable. */
    }
  }
  function reload() { iframe.src = "/?simulator=" + new Date().getTime(); }

  devices.forEach(function (device) {
    var option = document.createElement("option");
    option.value = device.id;
    option.appendChild(document.createTextNode(device.label));
    selector.appendChild(option);
  });
  selector.onchange = function () { selectDevice(selector.value); };
  orientationSelector.onchange = function () { selectOrientation(orientationSelector.value); };
  document.getElementById("backward").onclick = function () { sendInput("backward"); };
  document.getElementById("forward").onclick = function () { sendInput("forward"); };
  document.getElementById("reload").onclick = reload;
  iframe.onload = function () { installHostFonts(); notifyOrientation(); };
  document.getElementById("fit").onclick = function () { fit = !fit; layout(); };
  window.onresize = layout;
  document.onkeydown = function (event) {
    event = event || window.event;
    if (event.target && /input|select|textarea/i.test(event.target.tagName)) return;
    if (event.key === "ArrowLeft" || event.key === "PageUp" || event.keyCode === 37 || event.keyCode === 33) {
      sendInput("backward"); event.preventDefault();
    } else if (event.key === "ArrowRight" || event.key === "PageDown" || event.keyCode === 39 || event.keyCode === 34) {
      sendInput("forward"); event.preventDefault();
    }
  };
  selectDevice(window.localStorage.getItem("ankink_simulator_device") || DEFAULT_DEVICE);
  selectOrientation("portrait");
  reload();
}());
