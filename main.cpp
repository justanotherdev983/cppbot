#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

std::vector<ChatMessage> history;
std::mutex historyMutex;
char inputBuffer[2048] = "";
char apiKeyBuffer[128] = "";
bool isWaiting = false;
bool scrollToBottom = false;

void AddMessage(std::string role, std::string content) {
    std::lock_guard<std::mutex> lock(historyMutex);
    history.push_back({role, content});
    scrollToBottom = true;
}

#ifndef _WEB_BUILD
void DesktopAPICall(std::string message, std::string apiKey) {
    try {
        json::array messages;
        {
            std::lock_guard<std::mutex> lock(historyMutex);
            messages.push_back({{"role", "system"},
                                {"content", "You are a helpful assistant."}});
            int start = (history.size() > 4) ? history.size() - 4 : 0;
            for (size_t i = start; i < history.size(); i++) {
                messages.push_back({{"role", history[i].role},
                                    {"content", history[i].content}});
            }
        }

        json::object payload;
        payload["model"] = "mistralai/mistral-7b-instruct:free";
        payload["messages"] = messages;
        std::string requestBody = json::serialize(payload);

        const std::string host = "openrouter.ai";
        const std::string port = "443";

        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);

        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

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

        AddMessage("assistant", reply);

        beast::error_code ec;
        stream.shutdown(ec);
    } catch (std::exception const &e) {
        AddMessage("system", std::string("Error: ") + e.what());
    }
    isWaiting = false;
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
        AddMessage("assistant", reply);
    } catch (...) {
        AddMessage("system", "Error parsing JSON response");
    }
    isWaiting = false;
}

void onFetchError(emscripten_fetch_t *fetch) {
    emscripten_fetch_close(fetch);
    AddMessage("system", "Network Error (Check console)");
    isWaiting = false;
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

    // Headers (Must persist until call)
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

void SendMessage() {
    std::string msg = inputBuffer;
    std::string key = apiKeyBuffer;
    if (msg.empty())
        return;
    if (key.empty()) {
        AddMessage("system", "Please enter API Key first.");
        return;
    }

    AddMessage("user", msg);
    memset(inputBuffer, 0, sizeof(inputBuffer));
    isWaiting = true;

#ifdef _WEB_BUILD
    WebAPICall(msg, key);
#else
    std::thread([=]() { DesktopAPICall(msg, key); }).detach();
#endif
}

void ApplyCoolStyle() {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 6.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.25f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.3f, 0.3f, 0.4f, 1.00f);
    // Minimalist Font scale could be added here if font file loaded
}

void Render() {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    ImGui::TextDisabled("OpenRouter C++ Client (Boost + ImGui)");
    ImGui::Separator();

    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##key", "API Key (Required)", apiKeyBuffer,
                             sizeof(apiKeyBuffer),
                             ImGuiInputTextFlags_Password);

    ImGui::Spacing();
    ImGui::BeginChild("History", ImVec2(0, -50), true);
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        for (const auto &m : history) {
            if (m.role == "user") {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(0.5f, 0.8f, 1.0f, 1.0f));
                ImGui::Text("> YOU");
                ImGui::PopStyleColor();
            } else if (m.role == "assistant") {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
                ImGui::Text("> BOT");
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::Text("> SYSTEM");
                ImGui::PopStyleColor();
            }
            ImGui::Indent(10);
            ImGui::TextWrapped("%s", m.content.c_str());
            ImGui::Unindent(10);
            ImGui::Spacing();
            ImGui::Separator();
        }
        if (scrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            scrollToBottom = false;
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    bool submit = false;
    ImGui::PushItemWidth(-80);
    if (ImGui::InputText("##input", inputBuffer, sizeof(inputBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue))
        submit = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (isWaiting) {
        ImGui::Button("Fetching...", ImVec2(70, 0));
    } else {
        if (ImGui::Button("SEND", ImVec2(70, 0)))
            submit = true;
    }

    if (submit && !isWaiting) {
        SendMessage();
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

        Render();

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
