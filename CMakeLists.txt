cmake_minimum_required(VERSION 3.20)
project(browser VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror -fno-exceptions")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
endif()

# Helper for browser test executables - prevents declaration drift (B-2) and registers with CTest (B-3)
function(add_browser_test NAME)
    cmake_parse_arguments(AT "" "" "SOURCES;LIBS" ${ARGN})
    if(NOT AT_SOURCES)
        set(AT_SOURCES "tests/${NAME}.cpp")
    endif()
    add_executable(${NAME} tests/main.cpp ${AT_SOURCES})
    target_link_libraries(${NAME} PRIVATE test_framework ${AT_LIBS})
endfunction()

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
    css/invalidation.hpp
    css/invalidation.cpp
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
    css/cascade/shorthand_box.cpp
    css/cascade/shorthand_box.hpp
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
    render/offscreen_target.cpp
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
    render/offscreen_target.hpp
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
add_browser_test(test_framework_test LIBS )

add_browser_test(test_framework_impl_test LIBS )

add_browser_test(utility_test LIBS )

# --- Async tests ---
add_browser_test(iocp_test LIBS net)

add_browser_test(async_test LIBS async)

add_browser_test(channel_test LIBS async)

add_browser_test(thread_pool_test LIBS async)

# --- Network tests ---
add_browser_test(net_test LIBS net)

add_browser_test(tls_test LIBS net)

add_browser_test(tracker_test LIBS net)

# --- HTML tests ---
add_browser_test(html_test LIBS html)

# --- CSS tests ---
add_browser_test(css_test LIBS css)

add_browser_test(flex_test LIBS css html)

add_browser_test(grid_test LIBS css html)

add_browser_test(layout_test LIBS render css html)

add_browser_test(css_animation_test LIBS css html)

# --- JavaScript tests ---
add_browser_test(js_test LIBS js)

add_browser_test(builtins_test LIBS js)

add_browser_test(parser_test LIBS js)

add_browser_test(compiler_test LIBS js)

add_browser_test(vm_test LIBS js)

add_browser_test(gc_test LIBS js)

add_browser_test(jit_test LIBS js)

add_browser_test(dom_bindings_test LIBS js html)

add_browser_test(web_api_test LIBS js html)

# --- Render tests ---
add_browser_test(mesh_test LIBS render)

add_browser_test(font_test LIBS render)

add_browser_test(canvas_test LIBS render)

add_browser_test(csp_test LIBS net)

add_browser_test(paint_test LIBS render css html)

# Window smoke test (requires interactive desktop; gracefully handles failure)
add_browser_test(window_smoke_test LIBS platform)

# GPU smoke test (full render pipeline with OpenGL context)
add_browser_test(gpu_smoke_test LIBS platform render)

# --- Image tests ---
add_browser_test(image_test LIBS image)

# --- Browser/chrome tests ---
add_browser_test(websocket_test LIBS net)

add_browser_test(http_cache_test LIBS net)

add_browser_test(telemetry_test LIBS browser_lib)

add_browser_test(settings_test LIBS browser_lib)

add_browser_test(chrome_test LIBS browser_lib platform render net)

add_browser_test(history_test LIBS browser_lib)

add_browser_test(bookmark_test LIBS browser_lib)

add_browser_test(cookie_test LIBS net)

add_browser_test(storage_test LIBS net)

# --- Browser executable ---
add_executable(browser src/main.cpp)
target_link_libraries(browser net platform render js browser_lib)

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND test_runner
    DEPENDS test_runner
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# CTest registration for all executables (B-3)
foreach(t
    test_runner test_framework_test test_framework_impl_test utility_test
    iocp_test async_test channel_test thread_pool_test
    net_test tls_test tracker_test
    html_test css_test flex_test grid_test layout_test css_animation_test
    js_test builtins_test parser_test compiler_test vm_test gc_test jit_test dom_bindings_test web_api_test
    mesh_test font_test canvas_test csp_test paint_test window_smoke_test gpu_smoke_test
    image_test websocket_test http_cache_test telemetry_test settings_test chrome_test history_test bookmark_test cookie_test storage_test)
    if(TARGET ${t})
        add_test(NAME ${t} COMMAND ${t})
    endif()
endforeach()



