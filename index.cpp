#include <napi.h>
#include <iostream>
#include <vips/vips8>
#include <chrono>
#include <optional>
#include <tuple>


std::string encode_and_color(const std::string& data, const std::string& color, bool need_quotes = true) {
    std::string buffer = "<span color=\"" + color + ">" + (need_quotes ? "«" : "");
    buffer.reserve(2 * data.size());
    for (const char& pos : data) {
        switch(pos) {
            case '&':  buffer.append("&amp;");    break;
            case '\"': buffer.append("&quot;");   break;
            case '\'': buffer.append("&apos;");   break;
            case '<':  buffer.append("&lt;");     break;
            case '>':  buffer.append("&gt;");     break;
            default:   buffer.append(&pos, 1);    break;
        }
    }
    buffer.append(need_quotes ? "»</span>" : "</span>");
    return buffer;
}

template<typename T>
bool type_check(const Napi::Value& val) {
    if constexpr (std::is_same_v<T, std::string>) {
        return val.IsString();
    } else if constexpr (std::is_same_v<T, Napi::ArrayBuffer>) {
        return val.IsArrayBuffer();
    }
}

template<typename T>
T convert(const Napi::Value& val) {
    if constexpr (std::is_same_v<T, std::string>) {
        return val.As<Napi::String>().Utf8Value();
    } else if constexpr (std::is_same_v<T, Napi::ArrayBuffer>) {
        return val.As<Napi::ArrayBuffer>();
    }
}

template<typename ...Types, std::size_t... I>
std::tuple<Types...> make_tuple_finally(const Napi::CallbackInfo& info, std::index_sequence<I...>) {
    return std::make_tuple(convert<Types>(info[I])...);
}

template<typename ...Types>
static std::optional<std::tuple<Types...>> parse_args(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() != sizeof...(Types)) {
        Napi::TypeError::New(env, "wrong count of args").ThrowAsJavaScriptException();
        return {};
    }

    int i = 0;
    if (!(type_check<Types>(info[i++]) && ...)) {
        Napi::TypeError::New(env, "wrong arg type").ThrowAsJavaScriptException();
        return {};
    }
    return make_tuple_finally<Types...>(info, std::index_sequence_for<Types...>{});
}

std::string pick_font(const std::string& text) {
    std::cout << text.size() << '\n';
    if (text.size() < 500) {
        return "Play bold italic 85";
    } else if (text.size() < 2000) {
        return "Play bold italic 65";
    } else {
        return "Play bold italic 45";
    }
}


static Napi::Value gen_quote(const Napi::CallbackInfo& info) {
    // Napi::Env is the opaque data structure containing the environment in which the request is being run.
    // We will need this env when we want to create any new objects inside of the node.js environment
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    Napi::Env env = info.Env();

    auto args_opt = parse_args<std::string, std::string, Napi::ArrayBuffer>(info);
    if (!args_opt.has_value()) return env.Null();
    auto [text, name, ava_buf] = args_opt.value();
    //std::cout << static_cast<int>(text[0]) << '\n';

    if (text.size() > 20000) {
        Napi::TypeError::New(env, "text too big").ThrowAsJavaScriptException();
        return env.Null();
    }

    const int W = 1602, MIN_H = 939, TOP_H = 230, DOWN_H = 230, LEFT_M = 280, RIGHT_M = 280, VK_LIMIT = 14000;

    // Form a composite pipeline
    std::vector<vips::VImage> comp_images;
    std::vector<int> comp_modes;
    std::vector<int> comp_x, comp_y;
 
    // 1. render text
    vips::VImage main_text = vips::VImage::text(encode_and_color(text, "#ded7d7").c_str(), vips::VImage::option()
            ->set("font", pick_font(text).c_str())
            ->set("align", VIPS_ALIGN_LOW)
            ->set("width", W - LEFT_M - RIGHT_M)
            ->set("rgba", true)
    );
    if (main_text.height() + TOP_H + DOWN_H > VK_LIMIT - W) {
        vips::VImage::text(encode_and_color(text, "#ded7d7").c_str(), vips::VImage::option()
                ->set("font", pick_font(text).c_str())
                ->set("align", VIPS_ALIGN_LOW)
                ->set("width", W - LEFT_M - RIGHT_M)
                ->set("height", VK_LIMIT - W - TOP_H - DOWN_H)
                ->set("rgba", true)
        );
    }

    // 2. make main image
    vips::VImage bgnd = vips::VImage::black(W, std::max(MIN_H, main_text.height() + TOP_H + DOWN_H))
            .linear({0, 0, 0, 0}, {14, 11, 11, 255})
            //.cast(main_text.format())
            .copy(vips::VImage::option()
                          -> set("interpretation", VIPS_INTERPRETATION_sRGB)
    );
    const int COMP_H = bgnd.height(), COMP_W = bgnd.width();

    // add to pipeline in correct order. bgnd -> main_text
    comp_images.emplace_back(std::move(bgnd));

    comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    comp_x.emplace_back(std::max(LEFT_M, (W - main_text.width()) / 2));
    comp_y.emplace_back(TOP_H + std::max(0, (COMP_H - TOP_H - DOWN_H - main_text.height()) / 2));
    comp_images.emplace_back(std::move(main_text));

    // 3. attach header
    comp_images.emplace_back(vips::VImage::new_from_file("verkhniy_shablon.png", vips::VImage::option()->set("access", "sequential")));
    comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    comp_x.emplace_back(0);
    comp_y.emplace_back(0);

    // 4. attach avatar
    auto ava = vips::VImage::new_from_buffer(ava_buf.Data(), ava_buf.ByteLength(), "");
    if (ava.width() != 200) ava = ava.resize(200. / ava.width());
    comp_images.emplace_back(std::move(ava));
    comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    comp_x.emplace_back(21);
    comp_y.emplace_back(COMP_H - DOWN_H + 8);

    // 5. attach footer
    comp_images.emplace_back(vips::VImage::new_from_file("nizhniy_shablon.png", vips::VImage::option()->set("access", "sequential")));
    comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    comp_x.emplace_back(0);
    comp_y.emplace_back(COMP_H - DOWN_H);

    // 6. generate and attach nickname
    comp_images.emplace_back(vips::VImage::text(encode_and_color(name, "#ded7d7", false).c_str(), vips::VImage::option()
            ->set("font", "Play bold italic 72")
            ->set("align", VIPS_ALIGN_LOW)
            ->set("width", W - 390)
            ->set("rgba", true)
    ));
    comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    comp_x.emplace_back(390);
    comp_y.emplace_back(COMP_H - DOWN_H + 95);

    // Execute pipeline
    bgnd = vips::VImage::composite(comp_images, comp_modes, vips::VImage::option()
            ->set("x", comp_x)
            ->set("y", comp_y)
    );

    void* buf;
    size_t sz;
    bgnd.write_to_buffer(".jpg", &buf, &sz);
    std::cout << "C++ timing: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count() << "ms\n";
    return Napi::ArrayBuffer::New(env, buf, sz, [](const Napi::Env& env, void* buf) {
        free(buf);
    });
}

static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    if (VIPS_INIT("build-node-addon-api-with-cmake.node")) {
        vips_error_exit(NULL);
    }

    exports.Set(Napi::String::New(env, "gen_quote"),
                Napi::Function::New(env, gen_quote));
    return exports;
}

NODE_API_MODULE(hello, Init)
