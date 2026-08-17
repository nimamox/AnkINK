#include <curl/curl.h>
#include <sqlite3.h>
#include <zstd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr char kSyncBase[] = "https://sync.ankiweb.net/sync/";
constexpr char kHeaderClientVersion[] = "26.08.1,dev,darwin";
constexpr char kMetaClientVersion[] = "anki,26.08.1 (dev),darwin";

struct Config { std::string username, password; };
struct Reply { long status{}; std::string body, location, final_url; };

void fail(const std::string& message) { throw std::runtime_error(message); }

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) fail("could not open " + path);
    return {std::istreambuf_iterator<char>(input), {}};
}

std::string json_value(const std::string& json, const std::string& key) {
    const std::regex field("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (!std::regex_search(json, match, field)) fail("missing " + key + " in config.json");
    return match[1].str();
}

std::string json_escape(const std::string& value) {
    std::string out;
    for (char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch;
        }
    }
    return out;
}

size_t append_body(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

size_t read_headers(char* data, size_t size, size_t count, void* user) {
    std::string line(data, size * count);
    constexpr char prefix[] = "Location:";
    if (line.size() >= sizeof(prefix) - 1 &&
        std::equal(line.begin(), line.begin() + sizeof(prefix) - 1, prefix,
                   [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
        auto value = line.substr(sizeof(prefix) - 1);
        const auto first = value.find_first_not_of(" \t");
        const auto last = value.find_last_not_of("\r\n \t");
        *static_cast<std::string*>(user) = first == std::string::npos ? "" : value.substr(first, last - first + 1);
    }
    return size * count;
}

std::string zstd_compress(const std::string& input) {
    std::string output(ZSTD_compressBound(input.size()), '\0');
    const size_t size = ZSTD_compress(output.data(), output.size(), input.data(), input.size(), 3);
    if (ZSTD_isError(size)) fail(std::string("zstd compression: ") + ZSTD_getErrorName(size));
    output.resize(size);
    return output;
}

std::string zstd_decompress(const std::string& input) {
    ZSTD_DCtx* decoder = ZSTD_createDCtx();
    if (!decoder) fail("creating zstd decoder");
    ZSTD_inBuffer source{input.data(), input.size(), 0};
    std::vector<char> buffer(ZSTD_DStreamOutSize());
    std::string output;
    while (source.pos < source.size) {
        ZSTD_outBuffer destination{buffer.data(), buffer.size(), 0};
        const size_t result = ZSTD_decompressStream(decoder, &destination, &source);
        if (ZSTD_isError(result)) fail(std::string("zstd decompression: ") + ZSTD_getErrorName(result));
        output.append(buffer.data(), destination.pos);
    }
    ZSTD_freeDCtx(decoder);
    return output;
}

Reply post(const std::string& url, const std::string& compressed, const std::string& sync_header) {
    CURL* curl = curl_easy_init();
    if (!curl) fail("initializing curl");
    Reply reply;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("anki-sync: " + sync_header).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "User-Agent: anki-cpp-decks/0.1");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, compressed.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(compressed.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, read_headers);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &reply.location);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    const auto result = curl_easy_perform(curl);
    if (result != CURLE_OK) fail(std::string("request to AnkiWeb: ") + curl_easy_strerror(result));
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &reply.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    reply.final_url = url;
    return reply;
}

bool is_redirect(long status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }

std::string sync_request(std::string url, const std::string& host_key, const std::string& session_key, const std::string& payload, std::string* final_url = nullptr) {
    const std::string compressed = zstd_compress(payload);
    const std::string sync_header = "{\"v\":11,\"k\":\"" + json_escape(host_key) +
        "\",\"c\":\"" + kHeaderClientVersion + "\",\"s\":\"" + json_escape(session_key) + "\"}";
    for (int redirects = 0; redirects < 6; ++redirects) {
        Reply reply = post(url, compressed, sync_header);
        if (!is_redirect(reply.status)) {
            if (reply.status < 200 || reply.status >= 300)
                fail("AnkiWeb returned HTTP " + std::to_string(reply.status) + " from " + url + ": " + reply.body);
            if (final_url) *final_url = url;
            return zstd_decompress(reply.body);
        }
        if (reply.location.empty()) fail("AnkiWeb returned HTTP " + std::to_string(reply.status) + " without a redirect location");
        if (reply.location.rfind("https://", 0) != 0) fail("AnkiWeb redirected to a non-HTTPS URL");
        // Shard redirects provide a base URL; sync endpoints must be retained.
        const auto endpoint = url.find("/sync/");
        const std::string sync_endpoint = endpoint == std::string::npos ? "" : url.substr(endpoint + 1);
        url = reply.location;
        if (!url.empty() && url.back() == '/' && !sync_endpoint.empty())
            url += sync_endpoint;
    }
    fail("AnkiWeb exceeded five redirects");
}

std::string random_session_key() {
    constexpr char hex[] = "0123456789abcdef";
    std::random_device random;
    std::string result;
    for (int i = 0; i < 32; ++i) result += hex[random() & 15];
    return result;
}

std::vector<std::string> deck_names(const std::string& collection) {
	if (collection.size() < 16 || collection.compare(0, 15, "SQLite format 3\0", 15) != 0)
        fail("AnkiWeb download was not a SQLite collection");
    char path[] = "/tmp/anki-collection-XXXXXX";
    const int fd = mkstemp(path);
    if (fd == -1) fail("creating temporary collection");
    fchmod(fd, 0600);
    FILE* file = fdopen(fd, "wb");
    if (!file || fwrite(collection.data(), 1, collection.size(), file) != collection.size()) fail("writing temporary collection");
    fclose(file);
    sqlite3* db = nullptr;
    // SQLite may need a journal next to this disposable, owner-only copy.
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
        fail("opening downloaded collection at " + std::string(path) + ": " + sqlite3_errmsg(db));
    sqlite3_stmt* statement = nullptr;
    const int prepared = sqlite3_prepare_v2(db, "SELECT name FROM decks", -1, &statement, nullptr);
    if (prepared != SQLITE_OK) fail("reading decks: " + std::string(sqlite3_errmsg(db)));
    std::vector<std::string> decks;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const std::string stored(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
        std::string name;
        for (char ch : stored) ch == '\x1f' ? name += "::" : name += ch;
        decks.push_back(name);
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
    std::remove(path);
    std::sort(decks.begin(), decks.end());
    return decks;
}
} // namespace

int main() {
    try {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        const auto config_text = read_file("config.json");
        const Config config{json_value(config_text, "username"), json_value(config_text, "password")};
        const auto session = random_session_key();
        std::cout << "Logging in to AnkiWeb...\n";
        const auto login = sync_request(std::string(kSyncBase) + "hostKey", "", session,
            "{\"u\":\"" + json_escape(config.username) + "\",\"p\":\"" + json_escape(config.password) + "\"}");
        const auto host_key = json_value(login, "key");
        std::cout << "Login successful.\nDownloading collection metadata...\n";
        std::string meta_url;
        sync_request(std::string(kSyncBase) + "meta", host_key, session,
            std::string("{\"v\":11,\"cv\":\"") + kMetaClientVersion + "\"}", &meta_url);
        const auto sync_base = meta_url.substr(0, meta_url.rfind("meta"));
        const auto collection = sync_request(sync_base + "download", host_key, session, "{}");
        std::cout << "\nDecks:\n";
        for (const auto& deck : deck_names(collection)) std::cout << "  - " << deck << '\n';
        curl_global_cleanup();
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
