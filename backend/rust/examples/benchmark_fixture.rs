// Developer-only generated collection: no account or personal collection input.
use anki::collection::CollectionBuilder;
use anki::decks::{Deck, NativeDeckName};
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = std::env::args()
        .nth(1)
        .ok_or("expected disposable collection path")?;
    if std::path::Path::new(&path).exists() {
        return Err("refusing to overwrite collection".into());
    }
    let mut col = CollectionBuilder::new(&path).build()?;
    let nt = col
        .get_notetype_by_name("Basic")?
        .ok_or("missing stock Basic notetype")?;
    for deck_index in 0..5 {
        let mut deck = Deck::new_normal();
        deck.name = NativeDeckName::from_human_name(&format!("Benchmark {}", deck_index));
        col.add_deck(&mut deck)?;
        if deck_index == 0 {
            println!("{}", deck.id.0);
        }
        for index in 0..40 {
            let mut note = nt.new_note();
            note.set_field(0,format!(r#"<h2>Review {index}</h2><p>Explain energy and momentum with <b>formatted text</b>.</p>\[\frac{{x_{{{index}}}^2 + 1}}{{1+x}} + \sum_{{k=1}}^{{10}} k\]<img src="benchmark.png">"#))?;
            note.set_field(1,format!(r#"<p>The result for item {index} is:</p>\[\sqrt{{a^2+b^2}} + \int_0^1 x^{{{index}}}\,dx\]<ul><li>Compare dimensions</li><li>Check limits</li></ul>"#))?;
            col.add_note(&mut note, deck.id)?;
        }
    }
    col.close(None)?;
    Ok(())
}
