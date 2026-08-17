AnkINK for PW2-compatible ARMEL Kindles
=======================================

1. Copy this bundle to /mnt/us/ankink.
2. Copy kual-extension/AnkINK to /mnt/us/extensions/AnkINK.
3. Open KUAL and choose AnkINK > Start AnkINK.
4. On first launch, sign into AnkiWeb. AnkINK stores only
   the returned host key under /var/local/ankink; it does not save the password.

The launcher starts the bundled ankinkd backend on 127.0.0.1:8765, copies the
HTML application to /var/local/mesquite/ankink, registers org.ankink.app, and
launches it through Amazon's app manager. It does not stop the Kindle UI.
The UI removes the otherwise-empty native navigation strip and renders Anki
TeX equations locally using the bundled KaTeX fonts and JavaScript.
Reviews use Anki 26.08's official scheduler and are written to the collection.
Normal collection/media sync and guarded full download are supported. Full
upload is not included in this release.
