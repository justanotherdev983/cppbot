#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <regex>
#include <sstream>
#include <algorithm>

#include <boost/json.hpp>
#include <boost/json/src.hpp>
namespace json = boost::json;

#ifdef _WEB_BUILD
#include <SDL_opengles2.h>
#include <emscripten.h>
#include <emscripten/fetch.h>
#else
#include <SDL2/SDL_opengl.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;
#endif

struct ChatMessage {
    std::string role;
    std::string content;
};

struct CodeBlock {
    std::string language;
    std::string code;
};

struct HighlightRule {
    std::regex pattern;
    ImVec4 color;
};

struct Highlight {
    int start;
    int end;
    ImVec4 color;
    
    bool operator<(const Highlight& other) const {
        return start < other.start;
    }
};

struct AppContext {
    std::vector<ChatMessage> history;
    std::mutex historyMutex;
    char inputBuffer[2048];
    char apiKeyBuffer[128];
    bool isWaiting;
    bool scrollToBottom;
    
    AppContext() : isWaiting(false), scrollToBottom(false) {
        memset(inputBuffer, 0, sizeof(inputBuffer));
        memset(apiKeyBuffer, 0, sizeof(apiKeyBuffer));
    }
    
    void AddMessage(std::string role, std::string content) {
        std::lock_guard<std::mutex> lock(historyMutex);
        history.push_back({role, content});
        scrollToBottom = true;
    }
};

#ifdef _WEB_BUILD
// Global context pointer for web callbacks (required by Emscripten's C API)
static AppContext* g_webContext = nullptr;
#endif

std::vector<CodeBlock> ExtractCodeBlocks(const std::string& markdown) {
    std::vector<CodeBlock> blocks;
    
    std::regex pattern(R"(```(\w+)?\s*\n([\s\S]*?)```)");
    
    auto words_begin = std::sregex_iterator(markdown.begin(), markdown.end(), pattern);
    auto words_end = std::sregex_iterator();
    
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        CodeBlock block;
        block.language = match[1].matched ? match[1].str() : "plaintext";
        block.code = match[2].str();
        
        size_t end = block.code.find_last_not_of(" \n\r\t");
        if (end != std::string::npos) {
            block.code = block.code.substr(0, end + 1);
        }
        
        blocks.push_back(block);
    }
    
    return blocks;
}

std::vector<HighlightRule> GetRulesForLanguage(const std::string& lang) {
    std::vector<HighlightRule> rules;
    
    if (lang == "cpp" || lang == "c" || lang == "c++" || lang == "cc" || lang == "cxx") {
        rules.push_back({
            std::regex(R"(\b(alignas|alignof|and|and_eq|asm|auto|bitand|bitor|bool|break|case|catch|char|char8_t|char16_t|char32_t|class|compl|concept|const|consteval|constexpr|constinit|const_cast|continue|co_await|co_return|co_yield|decltype|default|delete|do|double|dynamic_cast|else|enum|explicit|export|extern|false|float|for|friend|goto|if|inline|int|long|mutable|namespace|new|noexcept|not|not_eq|nullptr|operator|or|or_eq|private|protected|public|register|reinterpret_cast|requires|return|short|signed|sizeof|static|static_assert|static_cast|struct|switch|template|this|thread_local|throw|true|try|typedef|typeid|typename|union|unsigned|using|virtual|void|volatile|wchar_t|while|xor|xor_eq)\b)"),
            ImVec4(0.86f, 0.47f, 0.86f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(^\s*#\s*\w+)"),
            ImVec4(0.7f, 0.7f, 0.4f, 1.0f)
        });
        rules.push_back({
            std::regex(R"("(?:[^"\\]|\\.)*")"),
            ImVec4(0.9f, 0.7f, 0.4f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(//[^\n]*)"),
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(\b\d+\.?\d*f?\b)"),
            ImVec4(0.6f, 0.85f, 0.6f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(\b\w+(?=\s*\())"),
            ImVec4(0.8f, 0.8f, 0.5f, 1.0f)
        });
    } 
    else if (lang == "python" || lang == "py") {
        rules.push_back({
            std::regex(R"(\b(False|None|True|and|as|assert|async|await|break|class|continue|def|del|elif|else|except|finally|for|from|global|if|import|in|is|lambda|nonlocal|not|or|pass|raise|return|try|while|with|yield)\b)"),
            ImVec4(0.86f, 0.47f, 0.86f, 1.0f)
        });
        rules.push_back({
            std::regex(R"((?:"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'))"),
            ImVec4(0.9f, 0.7f, 0.4f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(#[^\n]*)"),
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(\b\d+\.?\d*\b)"),
            ImVec4(0.6f, 0.85f, 0.6f, 1.0f)
        });
        rules.push_back({
            std::regex(R"((?<=def\s)\w+)"),
            ImVec4(0.8f, 0.8f, 0.5f, 1.0f)
        });
    }
    else if (lang == "javascript" || lang == "js" || lang == "typescript" || lang == "ts") {
        rules.push_back({
            std::regex(R"(\b(async|await|break|case|catch|class|const|continue|debugger|default|delete|do|else|enum|export|extends|false|finally|for|function|if|import|in|instanceof|let|new|null|return|super|switch|this|throw|true|try|typeof|var|void|while|with|yield)\b)"),
            ImVec4(0.86f, 0.47f, 0.86f, 1.0f)
        });
        rules.push_back({
            std::regex(R"((?:"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|`(?:[^`\\]|\\.)*`))"),
            ImVec4(0.9f, 0.7f, 0.4f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(//[^\n]*)"),
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(\b\d+\.?\d*\b)"),
            ImVec4(0.6f, 0.85f, 0.6f, 1.0f)
        });
    }
    else if (lang == "java") {
        rules.push_back({
            std::regex(R"(\b(abstract|assert|boolean|break|byte|case|catch|char|class|const|continue|default|do|double|else|enum|extends|final|finally|float|for|goto|if|implements|import|instanceof|int|interface|long|native|new|package|private|protected|public|return|short|static|strictfp|super|switch|synchronized|this|throw|throws|transient|try|void|volatile|while)\b)"),
            ImVec4(0.86f, 0.47f, 0.86f, 1.0f)
        });
        rules.push_back({
            std::regex(R"("(?:[^"\\]|\\.)*")"),
            ImVec4(0.9f, 0.7f, 0.4f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(//[^\n]*)"),
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(\b\d+\.?\d*[fFdDlL]?\b)"),
            ImVec4(0.6f, 0.85f, 0.6f, 1.0f)
        });
    }
    else if (lang == "rust" || lang == "rs") {
        rules.push_back({
            std::regex(R"(\b(as|async|await|break|const|continue|crate|dyn|else|enum|extern|false|fn|for|if|impl|in|let|loop|match|mod|move|mut|pub|ref|return|self|Self|static|struct|super|trait|true|type|unsafe|use|where|while)\b)"),
            ImVec4(0.86f, 0.47f, 0.86f, 1.0f)
        });
        rules.push_back({
            std::regex(R"("(?:[^"\\]|\\.)*")"),
            ImVec4(0.9f, 0.7f, 0.4f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(//[^\n]*)"),
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
        });
        rules.push_back({
            std::regex(R"(\b\d+\.?\d*\b)"),
            ImVec4(0.6f, 0.85f, 0.6f, 1.0f)
        });
    }
    
    return rules;
}

void RenderHighlightedCode(const std::string& code, const std::string& lang) {
    auto rules = GetRulesForLanguage(lang);
    
    std::vector<std::string> lines;
    std::stringstream ss(code);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    
    for (const auto& codeLine : lines) {
        if (codeLine.empty()) {
            ImGui::Text("");
            continue;
        }
        
        std::vector<Highlight> highlights;
        
        for (const auto& rule : rules) {
            auto begin = std::sregex_iterator(codeLine.begin(), codeLine.end(), rule.pattern);
            auto end = std::sregex_iterator();
            
            for (auto it = begin; it != end; ++it) {
                Highlight h;
                h.start = it->position();
                h.end = it->position() + it->length();
                h.color = rule.color;
                highlights.push_back(h);
            }
        }
        
        std::sort(highlights.begin(), highlights.end());
        
        std::vector<Highlight> filtered;
        int lastEnd = 0;
        for (const auto& h : highlights) {
            if (h.start >= lastEnd) {
                filtered.push_back(h);
                lastEnd = h.end;
            }
        }
        
        int pos = 0;
        for (const auto& h : filtered) {
            if (h.start > pos) {
                ImGui::SameLine(0, 0);
                std::string segment = codeLine.substr(pos, h.start - pos);
                ImGui::Text("%s", segment.c_str());
            }
            
            ImGui::SameLine(0, 0);
            ImGui::PushStyleColor(ImGuiCol_Text, h.color);
            std::string segment = codeLine.substr(h.start, h.end - h.start);
            ImGui::Text("%s", segment.c_str());
            ImGui::PopStyleColor();
            
            pos = h.end;
        }
        
        if (pos < (int)codeLine.length()) {
            ImGui::SameLine(0, 0);
            std::string segment = codeLine.substr(pos);
            ImGui::Text("%s", segment.c_str());
        }
        
        if (pos == 0 && filtered.empty()) {
            ImGui::Text("%s", codeLine.c_str());
        }
    }
}

#ifndef _WEB_BUILD
void DesktopAPICall(AppContext* ctx, std::string message, std::string apiKey) {
    try {
        json::array messages;
        {
            std::lock_guard<std::mutex> lock(ctx->historyMutex);
            messages.push_back({{"role", "system"},
                                {"content", "You are a helpful assistant."}});
            int start = (ctx->history.size() > 4) ? ctx->history.size() - 4 : 0;
            for (size_t i = start; i < ctx->history.size(); i++) {
                messages.push_back({{"role", ctx->history[i].role},
                                    {"content", ctx->history[i].content}});
            }
        }

        json::object payload;
        payload["model"] = "mistralai/mistral-7b-instruct:free";
        payload["messages"] = messages;
        std::string requestBody = json::serialize(payload);

        const std::string host = "openrouter.ai";
        const std::string port = "443";

        net::io_context ioc;
        ssl::context sslCtx(ssl::context::tlsv12_client);

        sslCtx.set_default_verify_paths();
        sslCtx.set_verify_mode(ssl::verify_peer);

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, sslCtx);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()),
                                 net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        auto const results = resolver.resolve(host, port);
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req{http::verb::post,
                                             "/api/v1/chat/completions", 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(http::field::content_type, "application/json");
        req.set(http::field::authorization, "Bearer " + apiKey);
        req.body() = requestBody;
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        json::value jv = json::parse(res.body());
        std::string reply = json::value_to<std::string>(
            jv.at("choices").at(0).at("message").at("content"));

        ctx->AddMessage("assistant", reply);

        beast::error_code ec;
        stream.shutdown(ec);
    } catch (std::exception const &e) {
        ctx->AddMessage("system", std::string("Error: ") + e.what());
    }
    ctx->isWaiting = false;
}
#endif

#ifdef _WEB_BUILD
void onFetchSuccess(emscripten_fetch_t *fetch) {
    std::string response(fetch->data, fetch->numBytes);
    emscripten_fetch_close(fetch);

    try {
        json::value jv = json::parse(response);
        std::string reply = json::value_to<std::string>(
            jv.at("choices").at(0).at("message").at("content"));
        g_webContext->AddMessage("assistant", reply);
    } catch (...) {
        g_webContext->AddMessage("system", "Error parsing JSON response");
    }
    g_webContext->isWaiting = false;
}

void onFetchError(emscripten_fetch_t *fetch) {
    emscripten_fetch_close(fetch);
    g_webContext->AddMessage("system", "Network Error (Check console)");
    g_webContext->isWaiting = false;
}

void WebAPICall(std::string message, std::string apiKey) {
    json::array messages;
    messages.push_back({{"role", "user"}, {"content", message}});

    json::object payload;
    payload["model"] = "mistralai/mistral-7b-instruct:free";
    payload["messages"] = messages;
    std::string requestBody = json::serialize(payload);

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "POST");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = onFetchSuccess;
    attr.onerror = onFetchError;

    static std::vector<const char *> headers;
    headers.clear();
    static std::string authHeader = "Bearer " + apiKey;
    headers.push_back("Content-Type");
    headers.push_back("application/json");
    headers.push_back("Authorization");
    headers.push_back(authHeader.c_str());
    headers.push_back("HTTP-Referer");
    headers.push_back("http://localhost:8000");
    headers.push_back(NULL);

    attr.requestHeaders = headers.data();
    attr.requestData = requestBody.c_str();
    attr.requestDataSize = requestBody.size();

    emscripten_fetch(&attr, "https://openrouter.ai/api/v1/chat/completions");
}
#endif

void SendMessage(AppContext* ctx) {
    std::string msg = ctx->inputBuffer;
    std::string key = ctx->apiKeyBuffer;
    if (msg.empty())
        return;
    if (key.empty()) {
        ctx->AddMessage("system", "Please enter API Key first.");
        return;
    }

    ctx->AddMessage("user", msg);
    memset(ctx->inputBuffer, 0, sizeof(ctx->inputBuffer));
    ctx->isWaiting = true;

#ifdef _WEB_BUILD
    WebAPICall(msg, key);
#else
    std::thread([ctx, msg, key]() { DesktopAPICall(ctx, msg, key); }).detach();
#endif
}

void ApplyCoolStyle() {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 6.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.4f, 1.00f);
}

void RenderMessage(const ChatMessage& m) {
    if (m.role == "user") {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
        ImGui::Text("> YOU");
        ImGui::PopStyleColor();
    } else if (m.role == "assistant") {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
        ImGui::Text("> BOT");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("> SYSTEM");
        ImGui::PopStyleColor();
    }
    
    ImGui::Indent(10);
    
    auto codeBlocks = ExtractCodeBlocks(m.content);
    
    if (codeBlocks.empty()) {
        ImGui::TextWrapped("%s", m.content.c_str());
    } else {
        std::regex pattern(R"(```\w*\s*\n[\s\S]*?```)");
        
        std::string remaining = m.content;
        auto searchStart = remaining.cbegin();
        std::smatch match;
        int blockIdx = 0;
        
        while (std::regex_search(searchStart, remaining.cend(), match, pattern)) {
            std::string before(searchStart, match[0].first);
            if (!before.empty() && before != "\n") {
                ImGui::TextWrapped("%s", before.c_str());
            }
            
            if (blockIdx < (int)codeBlocks.size()) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
                
                int numLines = std::count(codeBlocks[blockIdx].code.begin(), 
                                         codeBlocks[blockIdx].code.end(), '\n') + 3;
                float height = numLines * ImGui::GetTextLineHeightWithSpacing() + 20;
                
                ImGui::BeginChild(("code_" + std::to_string((size_t)&m) + "_" + std::to_string(blockIdx)).c_str(), 
                                ImVec2(0, height), true);
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::Text("[%s]", codeBlocks[blockIdx].language.c_str());
                ImGui::PopStyleColor();
                
                ImGui::Separator();
                
                RenderHighlightedCode(codeBlocks[blockIdx].code, codeBlocks[blockIdx].language);
                
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Spacing();
                
                blockIdx++;
            }
            
            searchStart = match.suffix().first;
        }
        
        if (searchStart != remaining.cend()) {
            std::string after(searchStart, remaining.cend());
            if (!after.empty() && after != "\n") {
                ImGui::TextWrapped("%s", after.c_str());
            }
        }
    }
    
    ImGui::Unindent(10);
    ImGui::Spacing();
    ImGui::Separator();
}

void Render(AppContext* ctx) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    ImGui::TextDisabled("OpenRouter C++ Client (Boost + ImGui)");
    ImGui::Separator();

    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##key", "API Key (Required)", ctx->apiKeyBuffer,
                             sizeof(ctx->apiKeyBuffer),
                             ImGuiInputTextFlags_Password);

    ImGui::Spacing();
    ImGui::BeginChild("History", ImVec2(0, -50), true);
    {
        std::lock_guard<std::mutex> lock(ctx->historyMutex);
        for (const auto &m : ctx->history) {
            RenderMessage(m);
        }
        if (ctx->scrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            ctx->scrollToBottom = false;
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    bool submit = false;
    ImGui::PushItemWidth(-80);
    if (ImGui::InputText("##input", ctx->inputBuffer, sizeof(ctx->inputBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue))
        submit = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (ctx->isWaiting) {
        ImGui::Button("Fetching...", ImVec2(70, 0));
    } else {
        if (ImGui::Button("SEND", ImVec2(70, 0)))
            submit = true;
    }

    if (submit && !ctx->isWaiting) {
        SendMessage(ctx);
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
}

int main(int, char **) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return -1;

#ifdef _WEB_BUILD
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

    SDL_Window *window = SDL_CreateWindow(
        "SchoolBot", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 600, 800,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GL_CreateContext(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ApplyCoolStyle();
    ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext());
    ImGui_ImplOpenGL3_Init("#version 100");

    AppContext ctx;
    
#ifdef _WEB_BUILD
    g_webContext = &ctx;
#endif

    bool done = false;

    auto main_loop_iteration = [&]() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        Render(&ctx);

        ImGui::Render();
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x,
                   (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(0.08f, 0.08f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    };

#ifdef _WEB_BUILD
    emscripten_set_main_loop_arg(
        [](void *arg) {
            auto loop_func = static_cast<decltype(main_loop_iteration) *>(arg);
            (*loop_func)();
        },
        &main_loop_iteration, 0, 1);
#else
    while (!done) {
        main_loop_iteration();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(SDL_GL_GetCurrentContext());
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}