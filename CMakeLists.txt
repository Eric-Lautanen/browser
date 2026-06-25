cmake_minimum_required(VERSION 3.20)
project(browser VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror -fno-exceptions")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
endif()

# Test Framework Library
add_library(test_framework STATIC
    tests/test_framework.cpp
    tests/test_framework.hpp
)
target_include_directories(test_framework PUBLIC tests)

# Async Library (header-only)
add_library(async INTERFACE
    async/task.hpp
    async/executor.hpp
    async/when_all.hpp
    async/when_any.hpp
    async/channel.hpp
    async/mutex.hpp
    async/scoped_lock.hpp
    async/thread_pool.hpp
    async/memory.hpp
    async/this_thread.hpp
)
target_include_directories(async INTERFACE .)

# Platform Library
add_library(platform STATIC
    platform/window.cpp
    platform/window_win32.cpp
    platform/opengl.cpp
    platform/audio.cpp
    platform/window.hpp
    platform/window_win32.hpp
    platform/opengl.hpp
    platform/audio.hpp
)
target_include_directories(platform PUBLIC .)
if(WIN32)
    target_link_libraries(platform gdi32 user32 opengl32 comctl32 winmm)
endif()

# Network Library
add_library(net STATIC
    net/socket/types.cpp
    net/socket/tcp.cpp
    net/socket/udp.cpp
    net/dns.cpp
    net/connection.cpp
    net/url.cpp
    net/origin.cpp
    net/csp.cpp
    net/hsts.cpp
    net/http.cpp
    net/http2/connection.cpp
    net/http2/frames.cpp
    net/http2/hpack.cpp
    net/http_client.cpp
    net/tracker_blocker.cpp
    net/deflate.cpp
    net/iocp.hpp
    net/iocp.cpp
    net/crypto/chacha20.cpp
    net/crypto/poly1305.cpp
    net/crypto/aes.cpp
    net/crypto/sha.cpp
    net/crypto/bignum.cpp
    net/crypto/ecc.cpp
    net/crypto/x25519.cpp
    net/tls/connection.cpp
    net/tls/cipher.cpp
    net/tls/record.cpp
    net/tls/handshake.cpp
    net/tls/cert_verify.cpp
    net/websocket.cpp
    net/http_cache.cpp
    net/cookie_jar.hpp
    net/cookie_jar.cpp
    net/storage.hpp
    net/storage.cpp
    net/origin.hpp
    net/csp.hpp
    net/hsts.hpp
    net/tls/cert_verify.hpp
    net/socket.hpp
    net/socket_win32.hpp
    net/dns.hpp
    net/connection.hpp
    net/url.hpp
    net/http.hpp
    net/http2.hpp
    net/http2/connection.hpp
    net/http2/internal.hpp
    net/http_client.hpp
    net/deflate.hpp
    net/websocket.hpp
    net/http_cache.hpp
)
target_include_directories(net PUBLIC .)
if(WIN32)
    target_link_libraries(net ws2_32 bcrypt mswsock crypt32)
endif()
target_link_libraries(net platform)

# Image Library
add_library(image STATIC
    image/format.hpp
    image/decoder.hpp
    image/decoder.cpp
    image/decoder_bmp.cpp
    image/decoder_png.cpp
    image/decoder_gif.cpp
    image/decoder_jpeg.cpp
)
target_include_directories(image PUBLIC .)
if(WIN32)
endif()
target_link_libraries(image net)

# HTML Library
add_library(html STATIC
    html/utf8.cpp
    html/utf8.hpp
    html/token.hpp
    html/entities.hpp
    html/entities.cpp
    html/tokenizer.hpp
    html/tokenizer/tokenizer.hpp
    html/tokenizer/tokenizer.cpp
    html/tokenizer/states_data.cpp
    html/tokenizer/states_tag.cpp
    html/tokenizer/states_rcdata.cpp
    html/tokenizer/states_foreign.cpp
    html/dom.hpp
    html/dom.cpp
    html/parser.hpp
    html/parser/parser.hpp
    html/parser/parser.cpp
    html/parser/foster_parenting.cpp
    html/parser/adoption_agency.cpp
    html/parser/template.cpp
    html/parser/initial.cpp
    html/parser/before_html.cpp
    html/parser/head.cpp
    html/parser/body.cpp
    html/parser/frameset.cpp
    html/parser/after_body.cpp
    html/traversal.hpp
    html/traversal.cpp
    html/preload_scanner.hpp
    html/preload_scanner.cpp
    html/resource_loader.hpp
    html/resource_loader.cpp
    html/hit_test.hpp
    html/hit_test.cpp
    html/form_state.hpp
    html/form_state.cpp
    html/form_submission.hpp
    html/form_submission.cpp
)
target_include_directories(html PUBLIC .)
target_link_libraries(html net)

# CSS Library (depends on html for DOM types)
add_library(css STATIC
    css/css_values.hpp
    css/css_values.cpp
    css/tokenizer.hpp
    css/tokenizer.cpp
    css/parser.hpp
    css/parser/parser.hpp
    css/parser/parser.cpp
    css/parser/selector.cpp
    css/parser/declaration.cpp
    css/parser/at_rule.cpp
    css/parser/property.cpp
    css/specificity.hpp
    css/selector_match.hpp
    css/selector_match.cpp
    css/cascade.hpp
    css/cascade/engine.hpp
    css/cascade/engine.cpp
    css/cascade/specificity.cpp
    css/cascade/important.cpp
    css/cascade/inheritance.cpp
    css/cascade/media.cpp
    css/layout.hpp
    css/layout/types.hpp
    css/layout/engine.cpp
    css/layout/resolve.cpp
    css/layout/type_check.cpp
    css/layout/flex.cpp
    css/layout/grid.cpp
    css/layout/block.cpp
    css/layout/inline.cpp
    css/layout/table.cpp
    css/layout/positioning.cpp
    css/grid.hpp
    css/grid.cpp
    css/animation.hpp
    css/animation.cpp
    css/transition.cpp
)
target_include_directories(css PUBLIC .)
target_link_libraries(css html)

# JavaScript Library (depends on html for DOM bindings)
add_library(js STATIC
    js/token.hpp
    js/lexer.hpp
    js/lexer.cpp
    js/ast.hpp
    js/parser.hpp
    js/parser/parser.hpp
    js/parser/parser.cpp
    js/parser/expression.cpp
    js/parser/statement.cpp
    js/parser/declaration.cpp
    js/parser/pattern.cpp
    js/bytecode.hpp
    js/compiler.hpp
    js/value.hpp
    js/gc.hpp
    js/vm.hpp
    js/vm/vm.hpp
    js/vm/vm.cpp
    js/vm/ops.cpp
    js/vm/builtins.cpp
    js/vm/gc.hpp
    js/vm/gc.cpp
    js/vm/compiler/compiler.hpp
    js/vm/compiler/compiler.cpp
    js/vm/compiler/expr.cpp
    js/vm/compiler/stmt.cpp
    js/jit.hpp
    js/jit.cpp
    js/dom_bindings.hpp
    js/dom_bindings.cpp
    js/dom_bindings/storage.cpp
    js/dom_bindings/document.cpp
    js/dom_bindings/fetch.cpp
    js/dom_bindings/xhr.cpp
    js/builtins/builtins.hpp
    js/builtins/string.cpp
    js/builtins/array.cpp
    js/builtins/object.cpp
    js/builtins/math.cpp
    js/builtins/number.cpp
    js/builtins/symbol.cpp
    js/builtins/json.cpp
    js/builtins/date.cpp
    js/builtins/regexp.cpp
    js/builtins/error.cpp
    js/builtins/console.cpp
    js/builtins/timers.cpp
    js/builtins/promise.cpp
    js/builtins/performance.cpp
    js/script_runner.hpp
    js/script_runner.cpp
    js/module_loader.hpp
    js/module_loader.cpp
)
target_include_directories(js PUBLIC .)
target_link_libraries(js html)

# Render Library (depends on platform for OpenGL, css/html for paint)
add_library(render STATIC
    render/shader_program.cpp
    render/mesh.cpp
    render/renderer.cpp
    render/texture.cpp
    render/font/font.cpp
    render/font/truetype.cpp
    render/font/rasterizer.cpp
    render/font/atlas.cpp
    render/font/embedded.cpp
    render/paint/gradient.cpp
    render/paint/shadow.cpp
    render/paint/painter.cpp
    render/paint/executor.cpp
    render/shaders.hpp
    render/shader_program.hpp
    render/mesh.hpp
    render/renderer.hpp
    render/texture.hpp
    render/font.hpp
    render/embedded_font.hpp
    render/text_renderer.hpp
    render/paint.hpp
    render/painter.hpp
    render/paint_executor.hpp
    render/form_controls.hpp
    render/form_controls.cpp
    render/svg_renderer.hpp
    render/svg_renderer.cpp
    render/mathml_stub.hpp
    render/mathml_stub.cpp
    render/canvas.hpp
    render/canvas.cpp
    render/audio_element.hpp
    render/audio_element.cpp
    render/video_element.hpp
    render/video_element.cpp
    render/canvas_bindings.cpp
)
target_include_directories(render PUBLIC .)
target_link_libraries(render platform css html image js)

# Browser Library
add_library(browser_lib STATIC
    browser/chrome/window.cpp
    browser/chrome/navigator.cpp
    browser/chrome/titlebar.cpp
    browser/chrome/toolbar.cpp
    browser/chrome/page_view.cpp
    browser/chrome/event_handler.cpp
    browser/page_loader.cpp
    browser/history.cpp
    browser/bookmarks.cpp
    browser/telemetry.cpp
    browser/perf_counter.cpp
    browser/settings.cpp
    browser/download_manager.cpp
    browser/find_bar.cpp
    browser/devtools.cpp
    browser/session.cpp
    browser/browser_window.hpp
    browser/types.hpp
    browser/escape.hpp
    browser/history.hpp
    browser/bookmarks.hpp
    browser/telemetry.hpp
    browser/settings.hpp
    browser/theme.hpp
    browser/page_loader.hpp
    browser/chrome/window.hpp
    browser/paths.hpp
)
target_include_directories(browser_lib PUBLIC .)
target_link_libraries(browser_lib net platform render js html image psapi)

# --- Test Runner ---
add_executable(test_runner
    tests/main.cpp
    tests/test_framework_impl_test.cpp
    tests/utility_test.cpp
)
target_link_libraries(test_runner test_framework)

# Individual test executables
add_executable(test_framework_test tests/main.cpp tests/test_framework_test.cpp)
target_link_libraries(test_framework_test test_framework)

add_executable(test_framework_impl_test tests/main.cpp tests/test_framework_impl_test.cpp)
target_link_libraries(test_framework_impl_test test_framework)

add_executable(utility_test tests/main.cpp tests/utility_test.cpp)
target_link_libraries(utility_test test_framework)

# --- Async tests ---
add_executable(iocp_test tests/main.cpp tests/iocp_test.cpp)
target_link_libraries(iocp_test test_framework net)

add_executable(async_test tests/main.cpp tests/async_test.cpp)
target_link_libraries(async_test test_framework async)

add_executable(channel_test tests/main.cpp tests/channel_test.cpp)
target_link_libraries(channel_test test_framework async)

add_executable(thread_pool_test tests/main.cpp tests/thread_pool_test.cpp)
target_link_libraries(thread_pool_test test_framework async)

# --- Network tests ---
add_executable(net_test tests/main.cpp tests/net_test.cpp)
target_link_libraries(net_test test_framework net)

add_executable(tls_test tests/main.cpp tests/tls_test.cpp)
target_link_libraries(tls_test test_framework net)

add_executable(tracker_test tests/main.cpp tests/tracker_test.cpp net/tracker_blocker.cpp)
target_link_libraries(tracker_test test_framework net)

# --- HTML tests ---
add_executable(html_test tests/main.cpp tests/html_test.cpp)
target_link_libraries(html_test test_framework html)

# --- CSS tests ---
add_executable(css_test tests/main.cpp tests/css_test.cpp)
target_link_libraries(css_test test_framework css)

add_executable(flex_test tests/main.cpp tests/flex_test.cpp)
target_link_libraries(flex_test test_framework css html)

add_executable(grid_test tests/main.cpp tests/grid_test.cpp)
target_link_libraries(grid_test test_framework css html)

add_executable(layout_test tests/main.cpp tests/layout_test.cpp)
target_link_libraries(layout_test test_framework render css html)

add_executable(css_animation_test tests/main.cpp tests/css_animation_test.cpp)
target_link_libraries(css_animation_test test_framework css html)

# --- JavaScript tests ---
add_executable(js_test tests/main.cpp tests/js_test.cpp)
target_link_libraries(js_test test_framework js)

add_executable(parser_test tests/main.cpp tests/parser_test.cpp)
target_link_libraries(parser_test test_framework js)

add_executable(compiler_test tests/main.cpp tests/compiler_test.cpp)
target_link_libraries(compiler_test test_framework js)

add_executable(vm_test tests/main.cpp tests/vm_test.cpp)
target_link_libraries(vm_test test_framework js)

add_executable(gc_test tests/main.cpp tests/gc_test.cpp)
target_link_libraries(gc_test test_framework js)

add_executable(jit_test tests/main.cpp tests/jit_test.cpp)
target_link_libraries(jit_test test_framework js)

add_executable(dom_bindings_test tests/main.cpp tests/dom_bindings_test.cpp)
target_link_libraries(dom_bindings_test test_framework js html)

add_executable(web_api_test tests/main.cpp tests/web_api_test.cpp)
target_link_libraries(web_api_test test_framework js html)

# --- Render tests ---
add_executable(mesh_test tests/main.cpp tests/mesh_test.cpp)
target_link_libraries(mesh_test test_framework render)

add_executable(font_test tests/main.cpp tests/font_test.cpp)
target_link_libraries(font_test test_framework render)

add_executable(paint_test tests/main.cpp tests/paint_test.cpp)
target_link_libraries(paint_test test_framework render css html)

# Window smoke test (requires interactive desktop; gracefully handles failure)
add_executable(window_smoke_test tests/main.cpp tests/window_smoke_test.cpp)
target_link_libraries(window_smoke_test test_framework platform)

# GPU smoke test (full render pipeline with OpenGL context)
add_executable(gpu_smoke_test tests/main.cpp tests/gpu_smoke_test.cpp)
target_link_libraries(gpu_smoke_test test_framework platform render)

# --- Image tests ---
add_executable(image_test tests/main.cpp tests/image_test.cpp)
target_link_libraries(image_test test_framework image)

# --- Browser/chrome tests ---
add_executable(websocket_test tests/main.cpp tests/websocket_test.cpp)
target_link_libraries(websocket_test test_framework net)

add_executable(http_cache_test tests/main.cpp tests/http_cache_test.cpp)
target_link_libraries(http_cache_test test_framework net)

add_executable(telemetry_test tests/main.cpp tests/telemetry_test.cpp)
target_link_libraries(telemetry_test test_framework browser_lib)

add_executable(settings_test tests/main.cpp tests/settings_test.cpp)
target_link_libraries(settings_test test_framework browser_lib)

add_executable(chrome_test tests/main.cpp tests/chrome_test.cpp)
target_link_libraries(chrome_test test_framework browser_lib platform render net)

add_executable(history_test tests/main.cpp tests/history_test.cpp)
target_link_libraries(history_test test_framework browser_lib)

add_executable(bookmark_test tests/main.cpp tests/bookmark_test.cpp)
target_link_libraries(bookmark_test test_framework browser_lib)

add_executable(cookie_test tests/main.cpp tests/cookie_test.cpp)
target_link_libraries(cookie_test test_framework net)

add_executable(storage_test tests/main.cpp tests/storage_test.cpp)
target_link_libraries(storage_test test_framework net)

# --- Browser executable ---
add_executable(browser src/main.cpp)
target_link_libraries(browser net platform render js browser_lib)

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND test_runner
    DEPENDS test_runner
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)



