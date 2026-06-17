#include "../dom_bindings.hpp"
#include "../vm.hpp"
#include "../gc.hpp"
#include "../../net/storage.hpp"

namespace browser::js::dom_bindings {

struct StorageCtx {
    net::Storage* storage;
    VM* vm;
};

static JSValue storage_get_item(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<StorageCtx*>(context);
    if (args.size() < 2) return JSValue::null();
    std::string key = args[1].to_string();
    auto val = ctx->storage->get_item(key);
    if (val) return JSValue::string(*val);
    return JSValue::null();
}

static JSValue storage_set_item(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<StorageCtx*>(context);
    if (args.size() < 3) return JSValue::undefined();
    std::string key = args[1].to_string();
    std::string value = args[2].to_string();
    ctx->storage->set_item(key, value);
    return JSValue::undefined();
}

static JSValue storage_remove_item(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<StorageCtx*>(context);
    if (args.size() < 2) return JSValue::undefined();
    std::string key = args[1].to_string();
    ctx->storage->remove_item(key);
    return JSValue::undefined();
}

static JSValue storage_clear(const std::vector<JSValue>&, void* context) {
    auto* ctx = static_cast<StorageCtx*>(context);
    ctx->storage->clear();
    return JSValue::undefined();
}

static JSValue storage_key(const std::vector<JSValue>& args, void* context) {
    auto* ctx = static_cast<StorageCtx*>(context);
    if (args.size() < 2) return JSValue::null();
    size_t index = static_cast<size_t>(args[1].to_number());
    std::string k = ctx->storage->key(index);
    if (k.empty()) return JSValue::null();
    return JSValue::string(k);
}

static JSValue storage_get_length(const std::vector<JSValue>&, void* context) {
    auto* ctx = static_cast<StorageCtx*>(context);
    return JSValue::number(static_cast<f64>(ctx->storage->length()));
}

void register_storage_bindings(VM* vm, const std::string& origin) {
    auto& local_storage = net::Storage::local_storage(origin);

    auto* storage_gc = vm->heap()->alloc_object();
    auto* storage_obj = &storage_gc->obj;

    auto* ctx = new StorageCtx{&local_storage, vm};

    storage_obj->set("getItem", JSValue::function(vm->create_native_fn(storage_get_item, false, ctx)));
    storage_obj->set("setItem", JSValue::function(vm->create_native_fn(storage_set_item, false, ctx)));
    storage_obj->set("removeItem", JSValue::function(vm->create_native_fn(storage_remove_item, false, ctx)));
    storage_obj->set("clear", JSValue::function(vm->create_native_fn(storage_clear, false, ctx)));
    storage_obj->set("key", JSValue::function(vm->create_native_fn(storage_key, false, ctx)));
    storage_obj->set("length", JSValue::function(vm->create_native_fn(storage_get_length, false, ctx)));

    auto window_val = vm->global_object()->get("window");
    if (window_val.type == JSValue::Type::OBJECT && window_val.object_val) {
        window_val.object_val->set("localStorage", JSValue::object(storage_obj));
    }
}

} // namespace browser::js::dom_bindings
