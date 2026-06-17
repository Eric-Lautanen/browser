#include "../../net/http_client.hpp"
#include "../../net/origin.hpp"
#include "../../net/url.hpp"
#include "../dom_bindings.hpp"
#include "../gc.hpp"
#include "../vm.hpp"

#include <cctype>

namespace browser::js::dom_bindings {

    struct XHRCtx {
        VM *vm;
        DOMBindings *bindings;
        std::string page_url;
    };

    struct XHRInstance {
        int readyState = 0;
        int status = 0;
        std::string statusText;
        std::string responseText;
        std::string url;
        std::string method;
        net::http::Headers requestHeaders;
        net::http::Headers responseHeaders;
        bool aborted = false;
        JSValue onreadystatechange;
        VM *vm = nullptr;
        std::string page_url;
    };

    static JSValue xhr_open(const std::vector<JSValue> &args, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        if (args.size() < 3)
            return JSValue::undefined();
        xhr->method = args[1].to_string();
        xhr->url = args[2].to_string();
        xhr->readyState = 1;
        xhr->status = 0;
        xhr->statusText.clear();
        xhr->responseText.clear();
        xhr->aborted = false;
        return JSValue::undefined();
    }

    static bool check_cors_xhr(const std::string &page_url,
                               const std::string &target_url_str,
                               const net::http::Response &resp,
                               std::string &error) {
        if (page_url.empty())
            return true;
        auto page_origin = net::Origin::from_url_str(page_url);
        auto target_r = net::URL::parse(target_url_str);
        if (target_r.is_err())
            return true;
        auto target_origin = net::Origin::from_url(target_r.unwrap());
        if (page_origin.is_same_origin(target_origin))
            return true;

        std::string aao;
        for (auto &[hk, hv] : resp.headers.all()) {
            std::string lk;
            for (char ch : hk) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (lk == "access-control-allow-origin") {
                aao = hv;
                break;
            }
        }
        if (aao.empty()) {
            error = "No 'Access-Control-Allow-Origin' header present";
            return false;
        }
        if (aao == "*")
            return true;
        if (aao == page_origin.to_string())
            return true;
        error = "Origin not allowed by Access-Control-Allow-Origin";
        return false;
    }

    static JSValue xhr_send(const std::vector<JSValue> &args, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        if (xhr->url.empty() || xhr->method.empty())
            return JSValue::undefined();

        std::string resolved_url = xhr->url;
        net::URL target_url;
        if (!xhr->page_url.empty()) {
            auto base_r = net::URL::parse(xhr->page_url);
            if (base_r.is_ok()) {
                auto resolved_r = base_r.unwrap().resolve(xhr->url);
                if (resolved_r.is_ok()) {
                    target_url = resolved_r.unwrap();
                    resolved_url = target_url.to_string();
                }
            }
        }
        if (target_url.host.empty()) {
            auto parsed = net::URL::parse(xhr->url);
            if (parsed.is_err())
                return JSValue::undefined();
            target_url = parsed.unwrap();
        }

        xhr->readyState = 2;

        net::http::Request req;
        std::string method_upper;
        for (char ch : xhr->method) method_upper += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (method_upper == "GET")
            req.method = net::http::Method::GET;
        else if (method_upper == "POST")
            req.method = net::http::Method::POST;
        else if (method_upper == "HEAD")
            req.method = net::http::Method::HEAD;
        else if (method_upper == "PUT")
            req.method = net::http::Method::PUT;
        else if (method_upper == "DELETE")
            req.method = net::http::Method::DELETE;
        else
            req.method = net::http::Method::GET;
        req.url = target_url;
        {
            std::string host_hdr = target_url.host;
            if (target_url.port != 0 && target_url.port != target_url.default_port())
                host_hdr += ":" + std::to_string(target_url.port);
            req.headers.set("Host", host_hdr);
        }
        req.headers.set("User-Agent", "Browser/0.1");
        req.headers.set("Accept", "*/*");

        for (auto &[hk, hv] : xhr->requestHeaders.all()) {
            req.headers.set(hk, hv);
        }

        auto page_origin = net::Origin::from_url_str(xhr->page_url);
        auto target_origin = net::Origin::from_url(target_url);
        bool cross_origin = !page_origin.is_same_origin(target_origin);

        if (cross_origin && !page_origin.host.empty()) {
            req.headers.set("Origin", page_origin.to_string());
            req.headers.set("Referer", xhr->page_url);
        }

        if (args.size() > 1 && args[1].type != JSValue::Type::UNDEFINED) {
            std::string body = args[1].to_string();
            req.body.assign(body.begin(), body.end());
        }

        xhr->readyState = 3;

        net::HTTPClient http;
        if (!xhr->page_url.empty()) {
            http.set_page_url(xhr->page_url);
        }
        auto resp_r = http.fetch(req);
        if (resp_r.is_err()) {
            xhr->readyState = 4;
            xhr->status = 0;
            if (xhr->onreadystatechange.type == JSValue::Type::FUNCTION && xhr->onreadystatechange.function_val &&
                xhr->onreadystatechange.function_val->native_fn) {
                xhr->onreadystatechange.function_val->native_fn({},
                                                                xhr->onreadystatechange.function_val->native_context);
            }
            return JSValue::undefined();
        }
        auto resp = resp_r.unwrap();

        if (cross_origin) {
            std::string cors_error;
            if (!check_cors_xhr(xhr->page_url, resolved_url, resp, cors_error)) {
                xhr->readyState = 4;
                xhr->status = 0;
                if (xhr->onreadystatechange.type == JSValue::Type::FUNCTION && xhr->onreadystatechange.function_val &&
                    xhr->onreadystatechange.function_val->native_fn) {
                    xhr->onreadystatechange.function_val->native_fn(
                        {}, xhr->onreadystatechange.function_val->native_context);
                }
                return JSValue::undefined();
            }
        }

        xhr->readyState = 4;
        xhr->status = resp.status.code;
        xhr->statusText = resp.status.reason;
        xhr->responseText.assign(reinterpret_cast<const char *>(resp.body.data()), resp.body.size());
        for (auto &[hk, hv] : resp.headers.all()) {
            xhr->responseHeaders.set(hk, hv);
        }

        if (xhr->onreadystatechange.type == JSValue::Type::FUNCTION && xhr->onreadystatechange.function_val &&
            xhr->onreadystatechange.function_val->native_fn) {
            xhr->onreadystatechange.function_val->native_fn({}, xhr->onreadystatechange.function_val->native_context);
        }
        return JSValue::undefined();
    }

    static JSValue xhr_set_request_header(const std::vector<JSValue> &args, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        if (args.size() < 3)
            return JSValue::undefined();
        std::string name = args[1].to_string();
        std::string value = args[2].to_string();
        xhr->requestHeaders.set(name, value);
        return JSValue::undefined();
    }

    static JSValue xhr_get_response_header(const std::vector<JSValue> &args, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        if (args.size() < 2)
            return JSValue::null();
        std::string name = args[1].to_string();
        std::string lk;
        for (char ch : name) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (xhr->responseHeaders.has(lk)) {
            return JSValue::string(xhr->responseHeaders.get(lk));
        }
        return JSValue::null();
    }

    static JSValue xhr_get_all_response_headers(const std::vector<JSValue> &, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        std::string result;
        for (auto &[hk, hv] : xhr->responseHeaders.all()) {
            result += hk + ": " + hv + "\r\n";
        }
        return JSValue::string(result);
    }

    static JSValue xhr_abort(const std::vector<JSValue> &, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        xhr->aborted = true;
        xhr->readyState = 0;
        xhr->status = 0;
        xhr->responseText.clear();
        return JSValue::undefined();
    }

    static JSValue xhr_get_ready_state(const std::vector<JSValue> &, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        return JSValue::number(static_cast<f64>(xhr->readyState));
    }

    static JSValue xhr_get_status(const std::vector<JSValue> &, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        return JSValue::number(static_cast<f64>(xhr->status));
    }

    static JSValue xhr_get_status_text(const std::vector<JSValue> &, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        return JSValue::string(xhr->statusText);
    }

    static JSValue xhr_get_response_text(const std::vector<JSValue> &, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        return JSValue::string(xhr->responseText);
    }

    static JSValue xhr_get_onreadystatechange(const std::vector<JSValue> &, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        return xhr->onreadystatechange;
    }

    static JSValue xhr_set_onreadystatechange(const std::vector<JSValue> &args, void *context) {
        auto *xhr = static_cast<XHRInstance *>(context);
        if (args.size() < 2)
            return JSValue::undefined();
        xhr->onreadystatechange = args[1];
        return JSValue::undefined();
    }

    static JSValue xhr_constructor(const std::vector<JSValue> &, void *context) {
        auto *ctx = static_cast<XHRCtx *>(context);
        auto *xhr = new XHRInstance();
        xhr->vm = ctx->vm;
        xhr->page_url = ctx->page_url;
        xhr->readyState = 0;
        xhr->onreadystatechange = JSValue::undefined();

        auto *obj_gc = ctx->vm->heap()->alloc_object();
        auto &obj = obj_gc->obj;

        obj.set("open", JSValue::function(ctx->vm->create_native_fn(xhr_open, false, xhr)));
        obj.set("send", JSValue::function(ctx->vm->create_native_fn(xhr_send, false, xhr)));
        obj.set("setRequestHeader", JSValue::function(ctx->vm->create_native_fn(xhr_set_request_header, false, xhr)));
        obj.set("getResponseHeader", JSValue::function(ctx->vm->create_native_fn(xhr_get_response_header, false, xhr)));
        obj.set("getAllResponseHeaders",
                JSValue::function(ctx->vm->create_native_fn(xhr_get_all_response_headers, false, xhr)));
        obj.set("abort", JSValue::function(ctx->vm->create_native_fn(xhr_abort, false, xhr)));
        obj.set("readyState", JSValue::number(0));
        obj.set("status", JSValue::number(0));
        obj.set("statusText", JSValue::string(""));
        obj.set("responseText", JSValue::string(""));
        obj.set("onreadystatechange", JSValue::undefined());

        obj.set("get_readyState", JSValue::function(ctx->vm->create_native_fn(xhr_get_ready_state, false, xhr)));
        obj.set("get_status", JSValue::function(ctx->vm->create_native_fn(xhr_get_status, false, xhr)));
        obj.set("get_statusText", JSValue::function(ctx->vm->create_native_fn(xhr_get_status_text, false, xhr)));
        obj.set("get_responseText", JSValue::function(ctx->vm->create_native_fn(xhr_get_response_text, false, xhr)));
        obj.set("get_onreadystatechange",
                JSValue::function(ctx->vm->create_native_fn(xhr_get_onreadystatechange, false, xhr)));
        obj.set("set_onreadystatechange",
                JSValue::function(ctx->vm->create_native_fn(xhr_set_onreadystatechange, false, xhr)));

        return JSValue::object(&obj_gc->obj);
    }

    void register_xhr_bindings(VM *vm, DOMBindings *bindings, const std::string &page_url) {
        auto *ctx = new XHRCtx{vm, bindings, page_url};
        auto *xhr_fn = vm->create_native_fn(xhr_constructor, true, ctx);
        vm->global_object()->set("XMLHttpRequest", JSValue::function(xhr_fn));
    }

}  // namespace browser::js::dom_bindings
