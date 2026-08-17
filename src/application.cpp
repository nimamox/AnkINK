#include "ankink/application.hpp"

#include "ankink/collection.hpp"
#include "ankink/fbink_presenter.hpp"
#include "ankink/input.hpp"
#include "ankink/wpe_view.hpp"

#include <glib-unix.h>
#include <wpe/webkit.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ankink {

namespace {

std::string require_value(int &index, int argc, char **argv,
                          const char *option) {
  if (++index >= argc)
    throw std::invalid_argument(std::string(option) + " requires a value");
  return argv[index];
}

std::vector<std::string> split(const std::string &value, char separator) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const auto end = value.find(separator, begin);
    result.push_back(value.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin));
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return result;
}

std::string mime_type(const std::filesystem::path &path) {
  const auto extension = path.extension().string();
  if (extension == ".html")
    return "text/html; charset=utf-8";
  if (extension == ".css")
    return "text/css; charset=utf-8";
  if (extension == ".js")
    return "application/javascript; charset=utf-8";
  if (extension == ".svg")
    return "image/svg+xml";
  if (extension == ".png")
    return "image/png";
  if (extension == ".jpg" || extension == ".jpeg")
    return "image/jpeg";
  return "application/octet-stream";
}

} // namespace

Options parse_options(int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--collection")
      options.collection_path =
          require_value(index, argc, argv, "--collection");
    else if (argument == "--assets")
      options.assets_path = require_value(index, argc, argv, "--assets");
    else if (argument == "--full-refresh-every") {
      const auto value =
          require_value(index, argc, argv, "--full-refresh-every");
      const auto parsed = std::stoul(value);
      if (parsed == 0 || parsed > 1000)
        throw std::invalid_argument(
            "--full-refresh-every must be between 1 and 1000");
      options.full_refresh_every = static_cast<std::uint32_t>(parsed);
    } else if (argument == "--render-scale") {
      const auto value = require_value(index, argc, argv, "--render-scale");
      const auto parsed = std::stoul(value);
      if (parsed == 0 || parsed > 4)
        throw std::invalid_argument("--render-scale must be between 1 and 4");
      options.render_scale = static_cast<std::uint32_t>(parsed);
    } else if (argument == "--no-input")
      options.input_enabled = false;
    else if (argument == "--help" || argument == "-h")
      throw std::invalid_argument("help");
    else
      throw std::invalid_argument("unknown option: " + argument);
  }
  return options;
}

std::string usage(const char *executable) {
  return std::string("Usage: ") + executable + R"( [options]
  --collection PATH         Read cards from this Anki collection
  --assets PATH             Serve the AnkINK web UI from this directory
  --full-refresh-every N    Flash after N partial updates (default: 20)
  --render-scale N          Render at 1/N resolution and scale to e-ink (1-4)
  --no-input                Do not open /dev/input touch devices
  -h, --help                Show this help
)";
}

class Application::Impl {
public:
  explicit Impl(Options options) : options_(std::move(options)) {
    resolve_assets();
    presenter_ = std::make_unique<FBInkPresenter>(options_.full_refresh_every);

    std::string collection_error;
    if (!collection_.open(options_.collection_path, collection_error))
      startup_error_ = "Could not open " + options_.collection_path + ": " +
                       collection_error;

    const auto &display = presenter_->display_info();
    const auto render_width =
        std::max(1U, display.width / options_.render_scale);
    const auto render_height =
        std::max(1U, display.height / options_.render_scale);
    wpe_ = std::make_unique<WPEView>(*presenter_, render_width, render_height);

    loop_ = g_main_loop_new(nullptr, FALSE);
    context_ = webkit_web_context_new();
    manager_ = webkit_user_content_manager_new();
    settings_ = webkit_settings_new();
    if (!loop_ || !context_ || !manager_ || !settings_)
      throw std::runtime_error(
          "initializing the WPE application objects failed");

    webkit_settings_set_enable_javascript(settings_, TRUE);
    webkit_settings_set_enable_html5_local_storage(settings_, FALSE);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings_,
                                                                TRUE);
    webkit_settings_set_default_font_size(settings_, 24);
    webkit_settings_set_default_monospace_font_size(settings_, 22);

    webkit_web_context_register_uri_scheme(
        context_, "ankink", &Impl::on_uri_request, this, nullptr);
    auto *security = webkit_web_context_get_security_manager(context_);
    webkit_security_manager_register_uri_scheme_as_local(security, "ankink");
    webkit_security_manager_register_uri_scheme_as_secure(security, "ankink");
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security,
                                                                "ankink");

    if (!webkit_user_content_manager_register_script_message_handler(
            manager_, "ankink"
#ifdef ANKINK_WPE_API_2
            , nullptr
#endif
            ))
      throw std::runtime_error(
          "registering the AnkINK JavaScript bridge failed");
    g_signal_connect(manager_, "script-message-received::ankink",
                     G_CALLBACK(&Impl::on_script_message), this);

    view_ = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW, "backend",
        webkit_web_view_backend_new(wpe_->backend(), nullptr, nullptr),
        "web-context", context_, "settings", settings_, "user-content-manager",
        manager_, nullptr));
    if (!view_)
      throw std::runtime_error("creating the WPE WebView failed");

    const float scale = std::clamp(
        static_cast<float>(display.dpi) /
            (160.0F * static_cast<float>(options_.render_scale)),
        1.0F, 2.0F);
    wpe_->activate(scale);

    if (options_.input_enabled)
      input_ = std::make_unique<InputManager>(
          wpe_->backend(),
          DisplayInfo{render_width, render_height, display.dpi,
                      display.touch_swap_axes, display.touch_mirror_x,
                      display.touch_mirror_y});
    webkit_web_view_load_uri(view_, "ankink://app/index.html");
    signal_sources_.push_back(
        g_unix_signal_add(SIGINT, &Impl::on_signal, this));
    signal_sources_.push_back(
        g_unix_signal_add(SIGTERM, &Impl::on_signal, this));
  }

  ~Impl() {
    for (const auto source : signal_sources_) {
      if (source)
        g_source_remove(source);
    }
    input_.reset();
    if (view_)
      g_object_unref(view_);
    if (manager_) {
      webkit_user_content_manager_unregister_script_message_handler(
          manager_, "ankink"
#ifdef ANKINK_WPE_API_2
          , nullptr
#endif
          );
      g_object_unref(manager_);
    }
    if (settings_)
      g_object_unref(settings_);
    if (context_)
      g_object_unref(context_);
    if (loop_)
      g_main_loop_unref(loop_);
    wpe_.reset();
    presenter_.reset();
  }

  void resolve_assets() {
    if (options_.assets_path.empty()) {
      if (const char *environment = std::getenv("ANKINK_ASSET_DIR"))
        options_.assets_path = environment;
      else if (std::filesystem::exists("assets/index.html"))
        options_.assets_path = "assets";
      else
        options_.assets_path = ANKINK_INSTALL_ASSET_DIR;
    }
    if (!std::filesystem::exists(std::filesystem::path(options_.assets_path) /
                                 "index.html"))
      throw std::runtime_error("cannot find AnkINK assets at " +
                               options_.assets_path);
  }

  void send(const std::string &json) {
    if (!view_)
      return;
    const std::string script =
        "window.AnkINK&&window.AnkINK.receive(" + json + ");";
    evaluate_javascript(script.c_str());
  }

  void evaluate_javascript(const char *script) {
#ifdef ANKINK_WPE_API_2
    webkit_web_view_evaluate_javascript(view_, script, -1, nullptr, nullptr,
                                        nullptr, nullptr, nullptr);
#else
    webkit_web_view_run_javascript(view_, script, nullptr, nullptr, nullptr);
#endif
  }

  void handle_message(const std::string &message) {
    const auto fields = split(message, ':');
    try {
      if (fields.empty())
        return;
      if (fields[0] == "ready") {
        const auto &display = presenter_->display_info();
        send(std::string(R"({"type":"status","version":)") +
             json_string(ANKINK_VERSION) + R"(,"width":)" +
             std::to_string(display.width) + R"(,"height":)" +
             std::to_string(display.height) + R"(,"dpi":)" +
             std::to_string(display.dpi) + R"(,"touch":)" +
             (input_ && input_->has_touchscreen() ? "true" : "false") +
             R"(,"warning":)" + json_string(startup_error_) + "}");
        send(collection_.decks_json());
      } else if (fields[0] == "decks") {
        send(collection_.decks_json());
      } else if (fields[0] == "next" && fields.size() == 2) {
        send(collection_.next_card_json(std::stoll(fields[1])));
      } else if (fields[0] == "answer" && fields.size() == 3) {
        send(collection_.answer_json(std::stoll(fields[1]),
                                     std::stoi(fields[2])));
      } else if (fields[0] == "refresh") {
        presenter_->force_full_refresh();
        evaluate_javascript(
            "document.documentElement.dataset.refresh=Date.now()");
      } else if (fields[0] == "quit") {
        std::_Exit(EXIT_SUCCESS);
      } else {
        send(R"({"type":"error","message":"Unknown native request"})");
      }
    } catch (const std::exception &error) {
      send(std::string(R"({"type":"error","message":)") +
           json_string(error.what()) + "}");
    }
  }

  static void on_script_message(WebKitUserContentManager *,
#ifdef ANKINK_WPE_API_2
                                JSCValue *value,
#else
                                WebKitJavascriptResult *result,
#endif
                                gpointer data) {
    auto &self = *static_cast<Impl *>(data);
#ifndef ANKINK_WPE_API_2
    JSCValue *value = webkit_javascript_result_get_js_value(result);
#endif
    if (!jsc_value_is_string(value)) {
      self.send(
          R"({"type":"error","message":"Native messages must be strings"})");
      return;
    }
    char *message = jsc_value_to_string(value);
    if (message) {
      self.handle_message(message);
      g_free(message);
    }
  }

  static void on_uri_request(WebKitURISchemeRequest *request, gpointer data) {
    auto &self = *static_cast<Impl *>(data);
    const char *path_text = webkit_uri_scheme_request_get_path(request);
    std::filesystem::path relative = path_text ? path_text : "/index.html";
    relative = relative.relative_path().lexically_normal();
    if (relative.empty())
      relative = "index.html";
    if (relative.string().rfind("..", 0) == 0) {
      GError *error = g_error_new_literal(
          G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "invalid asset path");
      webkit_uri_scheme_request_finish_error(request, error);
      g_error_free(error);
      return;
    }
    const auto file =
        std::filesystem::path(self.options_.assets_path) / relative;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
      GError *error = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                  "asset not found: %s", file.c_str());
      webkit_uri_scheme_request_finish_error(request, error);
      g_error_free(error);
      return;
    }
    std::string contents{std::istreambuf_iterator<char>(stream), {}};
    void *copy = g_memdup2(contents.data(), contents.size());
    GInputStream *input =
        g_memory_input_stream_new_from_data(copy, contents.size(), g_free);
    const auto mime = mime_type(file);
    webkit_uri_scheme_request_finish(
        request, input, static_cast<gint64>(contents.size()), mime.c_str());
    g_object_unref(input);
  }

  static gboolean on_signal(gpointer data) {
    (void)data;
    // WPE WebKit 2.48's process-wide teardown is not safe on the Kindle's
    // Linux 3.0 userspace combination. At this point the OS can reclaim every
    // descriptor and subprocess; avoid unloading WebKit/backend DSOs.
    std::_Exit(EXIT_SUCCESS);
  }

  int run() {
    g_main_loop_run(loop_);
    return EXIT_SUCCESS;
  }

  Options options_;
  Collection collection_;
  std::string startup_error_;
  std::unique_ptr<FBInkPresenter> presenter_;
  std::unique_ptr<WPEView> wpe_;
  std::unique_ptr<InputManager> input_;
  GMainLoop *loop_{};
  WebKitWebContext *context_{};
  WebKitUserContentManager *manager_{};
  WebKitSettings *settings_{};
  WebKitWebView *view_{};
  std::vector<guint> signal_sources_;
};

Application::Application(Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
Application::~Application() = default;
int Application::run() { return impl_->run(); }

} // namespace ankink
