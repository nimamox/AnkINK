/* Optional metadata is intentionally retained even when only dimensions are
 * used today, so future hardware/input emulation need not infer a model from
 * resolution alone. */
window.ANKINK_KINDLE_DEVICES = [
  ["kindle-1", "Kindle 1st Generation", 600, 800],
  ["kindle-2", "Kindle 2nd Generation", 600, 800],
  ["kindle-dx", "Kindle DX", 824, 1200],
  ["kindle-dx-graphite", "Kindle DX Graphite", 824, 1200],
  ["kindle-keyboard-3", "Kindle Keyboard 3rd Generation", 600, 800],
  ["kindle-4", "Kindle 4th Generation", 600, 800],
  ["kindle-touch-4", "Kindle Touch 4th Generation", 600, 800],
  ["kindle-5", "Kindle 5th Generation", 600, 800],
  ["kindle-paperwhite-5", "Kindle Paperwhite 1st Generation (5th Gen)", 758, 1024],
  ["kindle-paperwhite-6", "Kindle Paperwhite 2nd Generation (6th Gen)", 758, 1024],
  ["kindle-7", "Kindle 7th Generation", 600, 800],
  ["kindle-voyage-7", "Kindle Voyage 7th Generation", 1072, 1448],
  ["kindle-paperwhite-7", "Kindle Paperwhite 3rd Generation (7th Gen)", 1072, 1448],
  ["kindle-oasis-8", "Kindle Oasis 8th Generation", 1080, 1440],
  ["kindle-8", "Kindle 8th Generation", 600, 800],
  ["kindle-oasis-9", "Kindle Oasis 9th Generation", 1264, 1680],
  ["kindle-paperwhite-10", "Kindle Paperwhite 4th Generation (10th Gen)", 1072, 1448],
  ["kindle-10", "Kindle 10th Generation", 600, 800],
  ["kindle-kids-10", "Kindle Kids 10th Generation", 600, 800],
  ["kindle-oasis-10", "Kindle Oasis 10th Generation", 1264, 1680],
  ["kindle-paperwhite-11", "Kindle Paperwhite 5th Generation (11th Gen)", 1236, 1648],
  ["kindle-paperwhite-signature-11", "Kindle Paperwhite Signature Edition 11th Generation", 1236, 1648],
  ["kindle-paperwhite-kids-11", "Kindle Paperwhite Kids 11th Generation", 1236, 1648],
  ["kindle-11", "Kindle 11th Generation (2022)", 1072, 1448],
  ["kindle-kids-11", "Kindle Kids 11th Generation", 1072, 1448],
  ["kindle-scribe-1", "Kindle Scribe 1st Generation (2022)", 1860, 2480],
  ["kindle-11-2024", "Kindle 11th Generation (2024)", 1072, 1448],
  ["kindle-kids-11-2024", "Kindle Kids 11th Generation (2024)", 1072, 1448],
  ["kindle-paperwhite-12", "Kindle Paperwhite 6th Generation (12th Gen)", 1264, 1680],
  ["kindle-paperwhite-signature-12", "Kindle Paperwhite Signature Edition 12th Generation", 1264, 1680],
  ["kindle-paperwhite-kids-12", "Kindle Paperwhite Kids 12th Generation", 1264, 1680],
  ["kindle-colorsoft-signature-1", "Kindle Colorsoft Signature Edition 1st Generation", 1264, 1680],
  ["kindle-scribe-2024", "Kindle Scribe (2024)", 1860, 2480],
  ["kindle-colorsoft-1", "Kindle Colorsoft 1st Generation", 1264, 1680],
  ["kindle-colorsoft-kids-1", "Kindle Colorsoft Kids", 1264, 1680],
  ["kindle-scribe-3", "Kindle Scribe 3rd Generation (11-inch)", 1980, 2640],
  ["kindle-scribe-3-no-frontlight", "Kindle Scribe 3rd Generation without Front Light", 1980, 2640],
  ["kindle-scribe-colorsoft-1", "Kindle Scribe Colorsoft 1st Generation", 1980, 2640]
].map(function (item) {
  var id = item[0];
  var match = id.match(/-(\d+)(?:-|$)/);
  var family = id.indexOf("paperwhite") >= 0 ? "paperwhite" :
    id.indexOf("oasis") >= 0 ? "oasis" :
    id.indexOf("scribe") >= 0 ? "scribe" :
    id.indexOf("voyage") >= 0 ? "voyage" :
    id.indexOf("dx") >= 0 ? "dx" : "kindle";
  return {
    id: id,
    label: item[1],
    width: item[2],
    height: item[3],
    family: family,
    generation: match ? parseInt(match[1], 10) : null,
    touch: id !== "kindle-1" && id !== "kindle-2" && id.indexOf("dx") < 0 &&
      id.indexOf("keyboard") < 0 && id !== "kindle-4" && id !== "kindle-5",
    color: id.indexOf("colorsoft") >= 0,
    screenSize: null,
    ppi: null,
    statusBarHeight: item[2] <= 824 ? 28 : (item[2] >= 1800 ? 50 : 40)
  };
});
