// Copyright: AnkINK contributors
// License: GNU AGPL, version 3 or later

use std::env;
use std::process::ExitCode;

use anki::collection::CollectionBuilder;
use anki::scheduler::answering::{CardAnswer, Rating};
use anki::timestamp::TimestampMillis;

fn run() -> anki::error::Result<()> {
    let collection_path = env::args()
        .nth(1)
        .unwrap_or_else(|| "collection.anki2".to_owned());

    println!("opening={collection_path}");
    let mut collection = CollectionBuilder::new(&collection_path).build()?;

    let decks = collection.get_all_deck_names(false)?;
    println!("decks={}", decks.len());
    for (id, name) in decks {
        println!("deck id={id:?} name={name}");
    }

    let answer_good = env::args().any(|arg| arg == "--answer-good");
    match collection.get_next_card()? {
        Some(queued) => {
            let intervals = collection.describe_next_states(&queued.states)?;
            println!("next_card={:?}", queued.card.id());
            println!("counts=again:{} hard:{} good:{} easy:{}",
                intervals[0], intervals[1], intervals[2], intervals[3]);
            if answer_good {
                collection.answer_card(&mut CardAnswer {
                    card_id: queued.card.id(),
                    current_state: queued.states.current,
                    new_state: queued.states.good,
                    rating: Rating::Good,
                    answered_at: TimestampMillis::now(),
                    milliseconds_taken: 0,
                    custom_data: None,
                    from_queue: true,
                })?;
                println!("answered=good card={:?}", queued.card.id());
                println!("changed_rows={}", collection.changes_since_open()?);
            }
        }
        None => println!("next_card=none"),
    }

    collection.close(None)?;
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("ankink-rslib-smoke: {error:#}");
            ExitCode::FAILURE
        }
    }
}
