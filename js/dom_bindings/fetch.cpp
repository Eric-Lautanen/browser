#include "../../net/http_client.hpp"
#include "../../net/origin.hpp"
#include "../../net/url.hpp"
#include "../dom_bindings.hpp"
#include "../gc.hpp"
#include "../vm.hpp"

#include <cctype>

namespace browser::js::dom_bindings {

    struct FetchCtx {
        VM *vm;
        DOMBindings *bindings;
        std::string page_url;
    };

    static bool check_cors(const std::string &page_url,
                           const net::URL &target_url,
                           const net::http::Response &resp,
                           std::string &error) {
        if (page_url.empty())
            return true;
        auto page_origin = net::Origin::from_url_str(page_url);
        auto target_origin = net::Origin::from_url(target_url);
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
        error = "Origin '" + page_origin.to_string() + "' not allowed by Access-Control-Allow-Origin";
        return false;
    }

    static JSValue create_response_object(VM *vm, const net::http::Response &resp, const std::string &url_str) {
        auto *obj_gc = vm->heap()->alloc_object();
        auto &obj = obj_gc->obj;
        obj.set("status", JSValue::number(static_cast<f64>(resp.status.code)));
        obj.set("ok", JSValue::boolean(resp.status.code >= 200 && resp.status.code < 300));
        obj.set("statusText", JSValue::string(resp.status.reason));
        obj.set("url", JSValue::string(url_str));
        std::string body_str(reinterpret_cast<const char *>(resp.body.data()), resp.body.size());
        obj.set("bodyText", JSValue::string(body_str));
        auto *headers_gc = vm->heap()->alloc_object();
        for (auto &[hk, hv] : resp.headers.all()) {
            std::string lk;
            for (char ch : hk) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            headers_gc->obj.set(lk, JSValue::string(hv));
        }
        obj.set("headers", JSValue::object(&headers_gc->obj));

        auto *resp_obj_ptr = &obj_gc->obj;
        auto *text_fn = vm->create_native_fn(
            [](const std::vector<JSValue> &, void *ctx) -> JSValue {
                auto *ro = static_cast<JSObject *>(ctx);
                return ro->get("bodyText");
            },
            false,
            resp_obj_ptr);
        obj.set("text", JSValue::function(text_fn));

        auto *json_fn = vm->create_native_fn(
            [](const std::vector<JSValue> &, void *ctx) -> JSValue {
                auto *ro = static_cast<JSObject *>(ctx);
                return ro->get("bodyText");
            },
            false,
            resp_obj_ptr);
        obj.set("json", JSValue::function(json_fn));

        return JSValue::object(&obj_gc->obj);
    }

    static JSValue fetch_native(const std::vector<JSValue> &args, void *context) {
        auto *ctx = static_cast<FetchCtx *>(context);
        VM *vm = ctx->vm;

        if (args.size() < 2) {
            auto *rejected = vm->heap()->alloc_object();
            rejected->obj.set("[[PromiseState]]", JSValue::string("rejected"));
            rejected->obj.set("[[PromiseResult]]", JSValue::string("fetch: missing url argument"));
            return JSValue::object(&rejected->obj);
        }

        std::string url_str = args[1].to_string();
        std::string resolved_url;
        net::URL target_url;
        if (!ctx->page_url.empty()) {
            auto base_r = net::URL::parse(ctx->page_url);
            if (base_r.is_ok()) {
                auto resolved_r = base_r.unwrap().resolve(url_str);
                if (resolved_r.is_ok()) {
                    target_url = resolved_r.unwrap();
                    resolved_url = target_url.to_string();
                }
            }
        }
        if (resolved_url.empty()) {
            auto parsed = net::URL::parse(url_str);
            if (parsed.is_err()) {
                auto *rejected = vm->heap()->alloc_object();
                rejected->obj.set("[[PromiseState]]", JSValue::string("rejected"));
                rejected->obj.set("[[PromiseResult]]", JSValue::string("fetch: invalid url"));
                return JSValue::object(&rejected->obj);
            }
            target_url = parsed.unwrap();
            resolved_url = url_str;
        }

        net::http::Request req;
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

        auto page_origin = net::Origin::from_url_str(ctx->page_url);
        auto target_origin = net::Origin::from_url(target_url);
        bool cross_origin = !page_origin.is_same_origin(target_origin);

        if (cross_origin && !page_origin.host.empty()) {
            req.headers.set("Origin", page_origin.to_string());
            req.headers.set("Referer", ctx->page_url);
        }

        net::HTTPClient http;
        if (!ctx->page_url.empty()) {
            http.set_page_url(ctx->page_url);
        }
        auto resp_r = http.fetch(req);
        if (resp_r.is_err()) {
            auto *rejected = vm->heap()->alloc_object();
            rejected->obj.set("[[PromiseState]]", JSValue::string("rejected"));
            rejected->obj.set("[[PromiseResult]]", JSValue::string("fetch: " + resp_r.unwrap_err()));
            return JSValue::object(&rejected->obj);
        }
        auto resp = resp_r.unwrap();

        if (cross_origin) {
            std::string cors_error;
            if (!check_cors(ctx->page_url, target_url, resp, cors_error)) {
                auto *rejected = vm->heap()->alloc_object();
                rejected->obj.set("[[PromiseState]]", JSValue::string("rejected"));
                rejected->obj.set("[[PromiseResult]]", JSValue::string(cors_error));
                return JSValue::object(&rejected->obj);
            }
        }

        JSValue response_obj = create_response_object(vm, resp, resolved_url);
        auto *promise = vm->heap()->alloc_object();
        promise->obj.set("[[PromiseState]]", JSValue::string("fulfilled"));
        promise->obj.set("[[PromiseResult]]", response_obj);
        return JSValue::object(&promise->obj);
    }

    void register_fetch_bindings(VM *vm, DOMBindings *bindings, const std::string &page_url) {
        auto *ctx = new FetchCtx{vm, bindings, page_url};
        JSValue fetch_fn = JSValue::function(vm->create_native_fn(fetch_native, false, ctx));
        vm->global_object()->set("fetch", fetch_fn);
    }

}  // namespace browser::js::dom_bindings
