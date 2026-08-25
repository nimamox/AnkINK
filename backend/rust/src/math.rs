use katex::{render_to_string, KatexContext, OutputFormat, Settings};
use std::collections::HashMap;
use std::ffi::{c_char, CStr, CString};
use std::hash::{Hash, Hasher};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::{Mutex, OnceLock};

const MAX_ENTRIES: usize = 192;
const MAX_BYTES: usize = 384 * 1024;

#[derive(Clone, Eq)]
struct CacheKey {
    expression: String,
    display_mode: bool,
}

impl PartialEq for CacheKey {
    fn eq(&self, other: &Self) -> bool {
        self.display_mode == other.display_mode && self.expression == other.expression
    }
}

impl Hash for CacheKey {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.display_mode.hash(state);
        self.expression.hash(state);
    }
}

struct CacheEntry {
    html: String,
    last_used: u64,
    bytes: usize,
}

struct MathCache {
    entries: HashMap<CacheKey, CacheEntry>,
    clock: u64,
    bytes: usize,
    hits: u64,
    misses: u64,
    evictions: u64,
    max_entries: usize,
    max_bytes: usize,
}

impl MathCache {
    fn new(max_entries: usize, max_bytes: usize) -> Self {
        Self {
            entries: HashMap::new(),
            clock: 0,
            bytes: 0,
            hits: 0,
            misses: 0,
            evictions: 0,
            max_entries,
            max_bytes,
        }
    }

    fn get(&mut self, key: &CacheKey) -> Option<String> {
        self.clock = self.clock.wrapping_add(1);
        if let Some(entry) = self.entries.get_mut(key) {
            self.hits += 1;
            entry.last_used = self.clock;
            return Some(entry.html.clone());
        }
        self.misses += 1;
        None
    }

    fn insert(&mut self, key: CacheKey, html: String) {
        let bytes = key.expression.len() + html.len() + 1;
        if self.max_entries == 0 || bytes > self.max_bytes {
            return;
        }
        self.clock = self.clock.wrapping_add(1);
        if let Some(old) = self.entries.remove(&key) {
            self.bytes -= old.bytes;
        }
        while self.entries.len() >= self.max_entries || self.bytes + bytes > self.max_bytes {
            let Some(oldest) = self
                .entries
                .iter()
                .min_by_key(|(_, value)| value.last_used)
                .map(|(key, _)| key.clone())
            else {
                break;
            };
            if let Some(old) = self.entries.remove(&oldest) {
                self.bytes -= old.bytes;
                self.evictions += 1;
            }
        }
        self.bytes += bytes;
        self.entries.insert(
            key,
            CacheEntry {
                html,
                last_used: self.clock,
                bytes,
            },
        );
    }
}

static CONTEXT: OnceLock<KatexContext> = OnceLock::new();
static CACHE: OnceLock<Mutex<MathCache>> = OnceLock::new();

fn cache() -> &'static Mutex<MathCache> {
    CACHE.get_or_init(|| Mutex::new(MathCache::new(MAX_ENTRIES, MAX_BYTES)))
}

fn render_uncached(expression: &str, display_mode: bool) -> Result<String, String> {
    let settings = Settings::builder()
        .display_mode(display_mode)
        .output(OutputFormat::Html)
        .throw_on_error(true)
        .build();
    render_to_string(
        CONTEXT.get_or_init(KatexContext::default),
        expression,
        &settings,
    )
    .map_err(|error| error.to_string())
}

fn render_cached(expression: &str, display_mode: bool) -> Result<String, String> {
    let key = CacheKey {
        expression: expression.to_owned(),
        display_mode,
    };
    if let Some(html) = cache()
        .lock()
        .unwrap_or_else(|value| value.into_inner())
        .get(&key)
    {
        return Ok(html);
    }
    let html = render_uncached(expression, display_mode)?;
    cache()
        .lock()
        .unwrap_or_else(|value| value.into_inner())
        .insert(key, html.clone());
    Ok(html)
}

fn html_escape(value: &str) -> String {
    let mut output = String::with_capacity(value.len());
    for character in value.chars() {
        match character {
            '&' => output.push_str("&amp;"),
            '<' => output.push_str("&lt;"),
            '>' => output.push_str("&gt;"),
            '"' => output.push_str("&quot;"),
            '\'' => output.push_str("&#39;"),
            _ => output.push(character),
        }
    }
    output
}

fn html_entity_decode(value: &str) -> String {
    let bytes = value.as_bytes();
    let mut output = String::with_capacity(value.len());
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] != b'&' {
            let character = value[index..].chars().next().unwrap();
            output.push(character);
            index += character.len_utf8();
            continue;
        }
        let Some(relative_end) = value[index..].find(';') else {
            output.push('&');
            index += 1;
            continue;
        };
        let end = index + relative_end;
        let entity = &value[index + 1..end];
        let decoded = match entity {
            "amp" => Some('&'),
            "lt" => Some('<'),
            "gt" => Some('>'),
            "quot" => Some('"'),
            "apos" | "#39" => Some('\''),
            _ if entity.starts_with("#x") || entity.starts_with("#X") => {
                u32::from_str_radix(&entity[2..], 16)
                    .ok()
                    .and_then(char::from_u32)
            }
            _ if entity.starts_with('#') => {
                entity[1..].parse::<u32>().ok().and_then(char::from_u32)
            }
            _ => None,
        };
        if let Some(character) = decoded {
            output.push(character);
            index = end + 1;
        } else {
            output.push('&');
            index += 1;
        }
    }
    output
}

fn unbox(expression: &str) -> Option<&str> {
    let value = expression.trim();
    if !value.starts_with(r"\boxed{") || !value.ends_with('}') {
        return None;
    }
    let mut depth = 0_i32;
    for (offset, character) in value[6..].char_indices() {
        match character {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 && 6 + offset + 1 != value.len() {
                    return None;
                }
            }
            _ => {}
        }
    }
    (depth == 0).then(|| &value[7..value.len() - 1])
}

fn rendered_math(expression: &str, display_mode: bool) -> String {
    let expression = html_entity_decode(expression);
    let boxed = unbox(&expression);
    let source = boxed.unwrap_or(&expression);
    let html = match render_cached(source, display_mode) {
        Ok(value) => value,
        Err(error) => {
            eprintln!("AnkINK math render failed: {error}");
            format!(
                "<span class=\"math-error\">{}</span>",
                html_escape(&expression)
            )
        }
    };
    let classes = if display_mode {
        "math ankink-display-math"
    } else {
        "math"
    };
    if boxed.is_some() {
        format!("<span class=\"{classes} ankink-boxed-math\">{html}</span>")
    } else {
        format!("<span class=\"{classes}\">{html}</span>")
    }
}

const DELIMITERS: [(&str, &str, bool); 6] = [
    (r"\[", r"\]", true),
    ("[$$]", "[/$$]", true),
    ("[latex]", "[/latex]", true),
    ("$$", "$$", true),
    (r"\(", r"\)", false),
    ("[$]", "[/$]", false),
];

fn render_text_math(text: &str) -> String {
    let mut output = String::with_capacity(text.len());
    let mut cursor = 0;
    loop {
        let mut opening: Option<(usize, usize, &str, bool)> = None;
        for (left, right, display) in DELIMITERS {
            if let Some(relative) = text[cursor..].find(left) {
                let position = cursor + relative;
                if opening.is_none_or(|current| position < current.0) {
                    opening = Some((position, left.len(), right, display));
                }
            }
        }
        let Some((position, left_len, right, display)) = opening else {
            output.push_str(&text[cursor..]);
            break;
        };
        let content_start = position + left_len;
        let Some(relative_end) = text[content_start..].find(right) else {
            output.push_str(&text[cursor..]);
            break;
        };
        let end = content_start + relative_end;
        output.push_str(&text[cursor..position]);
        output.push_str(&rendered_math(&text[content_start..end], display));
        cursor = end + right.len();
    }
    output
}

fn tag_name(tag: &str) -> (&str, bool, bool) {
    let trimmed = tag.trim_start_matches('<').trim_start();
    let closing = trimmed.starts_with('/');
    let name_start = usize::from(closing);
    let end = trimmed[name_start..]
        .find(|character: char| character.is_whitespace() || character == '>' || character == '/')
        .map_or(trimmed.len(), |value| name_start + value);
    (
        &trimmed[name_start..end],
        closing,
        tag.trim_end().ends_with("/>"),
    )
}

pub(crate) fn render_card_html(source: &str) -> String {
    let mut output = String::with_capacity(source.len());
    let mut cursor = 0;
    let mut protected: Vec<String> = Vec::new();
    while let Some(relative_open) = source[cursor..].find('<') {
        let open = cursor + relative_open;
        if protected.is_empty() {
            output.push_str(&render_text_math(&source[cursor..open]));
        } else {
            output.push_str(&source[cursor..open]);
        }
        let Some(relative_close) = source[open..].find('>') else {
            output.push_str(&source[open..]);
            return output;
        };
        let close = open + relative_close + 1;
        let tag = &source[open..close];
        let (name, closing, self_closing) = tag_name(tag);
        let lower = name.to_ascii_lowercase();
        if matches!(
            lower.as_str(),
            "script" | "style" | "textarea" | "pre" | "code"
        ) {
            if closing {
                if protected.last().is_some_and(|value| value == &lower) {
                    protected.pop();
                }
            } else if !self_closing {
                protected.push(lower);
            }
        }
        output.push_str(tag);
        cursor = close;
    }
    if protected.is_empty() {
        output.push_str(&render_text_math(&source[cursor..]));
    } else {
        output.push_str(&source[cursor..]);
    }
    output
}

fn ffi_boundary<F>(operation: F) -> *mut c_char
where
    F: FnOnce() -> Result<String, String>,
{
    match catch_unwind(AssertUnwindSafe(operation)) {
        Ok(Ok(value)) => CString::new(value).map_or(ptr::null_mut(), CString::into_raw),
        Ok(Err(error)) => {
            eprintln!("AnkINK math render failed: {error}");
            ptr::null_mut()
        }
        Err(_) => {
            eprintln!("AnkINK math renderer panicked");
            ptr::null_mut()
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ankink_math_render(latex: *const c_char, display_mode: u8) -> *mut c_char {
    ffi_boundary(|| {
        if latex.is_null() {
            return Err("null LaTeX pointer".to_owned());
        }
        let expression = unsafe { CStr::from_ptr(latex) }
            .to_str()
            .map_err(|_| "LaTeX is not valid UTF-8".to_owned())?;
        render_cached(expression, display_mode != 0)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ankink_math_free(html: *mut c_char) {
    if !html.is_null() {
        drop(unsafe { CString::from_raw(html) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    #[test]
    fn renders_representative_html_only_math() {
        for expression in [
            "x",
            "x^2",
            "x_i^2",
            r"\alpha + \beta",
            r"\frac{1}{2}",
            r"\frac{a+b}{c+d}",
            r"\sqrt{x^2+y^2}",
            r"\sum_{k=1}^{N} x_k",
            r"\int_0^\infty e^{-x}\,dx",
            r"\left(\frac{x}{y}\right)^2",
            r"\operatorname{argmax}_x f(x)",
            r"\begin{bmatrix}a&b\\c&d\end{bmatrix}",
            r"\boxed{x[n]=\sum_m x[m]\delta[n-m]}",
            r"n_{r,\min}=n_{s,\min}+n_{h,\min}",
        ] {
            let html = render_uncached(expression, false).unwrap();
            assert!(html.contains("class=\"katex\"") && !html.contains("<math"));
        }
        assert!(render_uncached(r"\notacommand{", false).is_err());
    }

    #[test]
    fn transforms_card_text_but_not_html_or_code() {
        let html = render_card_html(
            r#"<div title="\(attribute\)">\(x_i^2\)<code>\(raw\)</code><p>$$\frac{1}{2}$$</p></div>"#,
        );
        assert!(html.contains("msupsub") && html.contains("mfrac"));
        assert!(html.contains(r#"title="\(attribute\)""#));
        assert!(html.contains("<code>\\(raw\\)</code>"));
        assert!(!html.contains("katex-mathml"));
    }

    #[test]
    fn cache_is_lru_bounded_mode_sensitive_and_skips_failures() {
        let mut cache = MathCache::new(3, 128);
        let key = |value: &str, display| CacheKey {
            expression: value.into(),
            display_mode: display,
        };
        assert!(cache.get(&key("x", false)).is_none());
        cache.insert(key("x", false), "x".into());
        assert_eq!(cache.get(&key("x", false)).as_deref(), Some("x"));
        assert!(cache.get(&key("x", true)).is_none());
        cache.insert(key("y", false), "y".into());
        cache.insert(key("z", false), "z".into());
        assert_eq!(cache.get(&key("x", false)).as_deref(), Some("x"));
        cache.insert(key("w", false), "w".into());
        assert!(cache.entries.contains_key(&key("x", false)));
        assert!(!cache.entries.contains_key(&key("y", false)));
        assert!(cache.entries.len() <= 3 && cache.bytes <= 128 && cache.evictions == 1);
        let before = cache.entries.len();
        assert!(render_uncached(r"\notacommand{", false).is_err());
        assert_eq!(cache.entries.len(), before);
    }

    #[test]
    fn ffi_contains_panics_and_is_concurrent() {
        assert!(ffi_boundary(|| panic!("contained")).is_null());
        let workers: Vec<_> = (0..8)
            .map(|_| {
                thread::spawn(|| {
                    for _ in 0..32 {
                        let input = CString::new(r"\frac{1}{2}").unwrap();
                        let output = unsafe { ankink_math_render(input.as_ptr(), 0) };
                        assert!(!output.is_null());
                        unsafe { ankink_math_free(output) };
                    }
                })
            })
            .collect();
        for worker in workers {
            worker.join().unwrap();
        }
        let cache = cache().lock().unwrap();
        assert!(cache.entries.len() <= MAX_ENTRIES && cache.bytes <= MAX_BYTES && cache.hits > 0);
    }
}
