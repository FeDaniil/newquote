#include <napi.h>
#include <iostream>
#include <vips/vips8>
#include <chrono>
#include <optional>
#include <tuple>


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
        Napi::TypeError::New(env, "wrong arg type for " + std::to_string(i) + " arg").ThrowAsJavaScriptException();
        return {};
    }
    return make_tuple_finally<Types...>(info, std::index_sequence_for<Types...>{});
}

std::string pick_font(const std::string& text) {
    std::cout << text.size() << '\n';
    if (text.size() < 500) {
        return "Play bold italic 70";
    } else if (text.size() < 2000) {
        return "Play bold italic 60";
    } else {
        return "Play bold italic 45";
    }
}

std::string span_wrap(const std::string& text, const std::string& span_props_string) {
    return "<span " + span_props_string + ">" + text + "</span>";
}

std::string span_wrap(const std::string& text, std::initializer_list<std::pair<std::string, std::string>> span_props) {
    std::string span_props_string;
    for (auto [property, value] : span_props) {
        span_props_string += property + "=\"" + value + "\" ";
    }
    span_props_string.pop_back();  // redundant space
    return span_wrap(text, span_props_string);
}

std::string color(const std::string& text, const std::string& hex_color) {
    return span_wrap(text, "color=\"" + hex_color + "\"");
}

std::string encode(const std::string& text) {
    std::string buffer;
    buffer.reserve(text.size());
    for (const char& c : text) {
        switch(c) {
            case '&':  buffer.append("&amp;");    break;
            case '\"': buffer.append("&quot;");   break;
            case '\'': buffer.append("&apos;");   break;
            case '<':  buffer.append("&lt;");     break;
            case '>':  buffer.append("&gt;");     break;
            default:   buffer.append(&c, 1);    break;
        }
    }
    return buffer;
}

std::string quote(const std::string& text) {
    return "«" + text + "»";
}

std::string prepare_quote(const std::string& user_text, const std::string& hex_color) {
    return color(quote(encode(user_text)), hex_color);
}



static Napi::Value gen_quote(const Napi::CallbackInfo& info) {
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    Napi::Env env = info.Env();

    auto args_opt = parse_args<std::string, std::string, Napi::ArrayBuffer>(info);
    if (!args_opt.has_value()) return env.Null();
    auto [text, name, ava_buf] = args_opt.value();

    if (text.size() > 20000) {
        Napi::TypeError::New(env, "text too big").ThrowAsJavaScriptException();
        return env.Null();
    }

    int W = 1200, MIN_H = 740,
        TOP_H = 150, TOP_HM = W / 20, TOP_VM = 30,
        DOWN_H = 250, DOWN_VM = 30,
        AVA_LEFT = W / 20, AVA_SZ = 150,
        DOWN_TEXT_LEFT = AVA_LEFT + AVA_SZ + W / 20, DOWN_HRM = W / 20,
        BODY_LEFT_M = 2 * AVA_LEFT, BODY_RIGHT_M = BODY_LEFT_M,
        VK_LIMIT = 14000;

    // prepare for composite pipeline
    std::vector<vips::VImage> comp_images;
    std::vector<int> comp_modes;
    std::vector<int> comp_x, comp_y;
 
    // render main text
    vips::VImage main_text = vips::VImage::text(prepare_quote(text, "#ded7d7").c_str(), vips::VImage::option()
            ->set("font", pick_font(text).c_str())
            ->set("align", VIPS_ALIGN_LOW)
            ->set("width", W - BODY_LEFT_M - BODY_RIGHT_M)
            ->set("rgba", true)
    );
    if (main_text.height() + TOP_H + DOWN_H > VK_LIMIT - W) {
        vips::VImage::text(prepare_quote(text, "#ded7d7").c_str(), vips::VImage::option()
                ->set("font", pick_font(text).c_str())
                ->set("align", VIPS_ALIGN_LOW)
                ->set("width", W - BODY_LEFT_M - BODY_RIGHT_M)
                ->set("height", VK_LIMIT - W - TOP_H - DOWN_H)
                ->set("rgba", true)
        );
    }

    // load avatar
    auto ava = vips::VImage::new_from_buffer(ava_buf.Data(), ava_buf.ByteLength(), "");
    if (ava.bands() == 4) {  // has transparency
        auto bands = ava.bandsplit();
        bands.pop_back();
        ava = vips::VImage::bandjoin(bands);
    }
    auto ava_bands = ava.bandsplit();
    std::vector<double> ava_avg = {ava_bands[0].avg() * 0.5, ava_bands[1].avg() * 0.5, ava_bands[2].avg() * 0.5, 255};

    // make background image
    vips::VImage bgnd = vips::VImage::black(W, std::max(MIN_H, main_text.height() + TOP_H + DOWN_H))
            .linear({0, 0, 0, 0}, ava_avg)
            //.cast(main_text.format())
            .copy(vips::VImage::option()
                          -> set("interpretation", VIPS_INTERPRETATION_sRGB)
    );
    const int COMP_H = bgnd.height(), COMP_W = bgnd.width();

    // add to pipeline in correct order. bgnd -> main_text
    comp_images.emplace_back(std::move(bgnd));

    comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    comp_x.emplace_back(std::max(BODY_LEFT_M, (W - main_text.width()) / 2) - 5);  // -5: human perception moment
    comp_y.emplace_back(TOP_H + std::max(0, (COMP_H - TOP_H - DOWN_H - main_text.height()) / 2));
    comp_images.emplace_back(std::move(main_text));

    // add header
    //comp_images.emplace_back(vips::VImage::new_from_file("verkhniy_shablon.png", vips::VImage::option()->set("access", "sequential")));
    //comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    //comp_x.emplace_back(0);
    //comp_y.emplace_back(0);

    // render and add header text
    comp_images.emplace_back(vips::VImage::text(span_wrap("Золотой Фонд Цитат", {
            {"color", "#fff"},
            {"underline", "single"}
    }).c_str(), vips::VImage::option()
            ->set("font", "Play 70")
            ->set("align", VIPS_ALIGN_CENTRE)
            ->set("width", W - BODY_LEFT_M - BODY_RIGHT_M)
            ->set("rgba", true)
    ));
    comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    comp_x.emplace_back(std::max(BODY_LEFT_M, (W - comp_images[comp_images.size() - 1].width()) / 2));
    comp_y.emplace_back(TOP_VM);

    // add avatar
    // prepare transparency mask
    auto crude = vips::VImage::black(ava.width(), ava.height());
    crude.draw_circle({255}, ava.width() / 2, ava.height() / 2, ava.width() / 2 - 2, vips::VImage::option()
            ->set("fill", true)
    );
    auto alpha = vips::VImage::mask_butterworth_ring(ava.width(), ava.height(), 2, 0.98, 0.001, 0.2, vips::VImage::option()
            ->set("nodc", true)
            ->set("optical", true)
    ).linear({255}, {0}) | crude;
    // join transparency mask to avatar
    ava = ava.bandjoin(alpha);
    if (ava.width() != AVA_SZ) ava = ava.resize(static_cast<double>(AVA_SZ) / ava.width());
    comp_images.emplace_back(std::move(ava));
    comp_modes.emplace_back(VIPS_BLEND_MODE_ATOP);
    comp_x.emplace_back(AVA_LEFT);
    comp_y.emplace_back(COMP_H - (DOWN_H - DOWN_VM + AVA_SZ) / 2);

    // render and add nickname
    auto wrapped_copyright = span_wrap("© ", {
            {"font", "sans bold 50"},
            {"color", "#ded7d7"}
    });
    comp_images.emplace_back(vips::VImage::text((wrapped_copyright + color(encode(name), "#ded7d7")).c_str(), vips::VImage::option()
            ->set("font", "Play bold italic 65")
            ->set("align", VIPS_ALIGN_LOW)
            ->set("width", W - DOWN_TEXT_LEFT - DOWN_HRM)
            ->set("rgba", true)
    ));
    comp_modes.emplace_back(VIPS_BLEND_MODE_OVER);
    comp_x.emplace_back(DOWN_TEXT_LEFT);
    comp_y.emplace_back(COMP_H - (DOWN_H - DOWN_VM + comp_images[comp_images.size() - 1].height()) / 2);

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
