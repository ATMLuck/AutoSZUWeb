cmake_minimum_required(VERSION 3.14)
project(AutoSZUWeb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 源文件 ──────────────────────────────────────
set(SOURCES
    src/AutoSZUWeb.cpp
    src/sys.cpp
    src/web.cpp
    src/file_path.cpp
)

# ── 图标资源 (MinGW windres) ────────────────────
if(MINGW)
    # windres 需要在项目根目录运行, ico.rc 内引用路径相对于根目录
    set(ICO_RC  "${CMAKE_CURRENT_SOURCE_DIR}/res/ico.rc")
    set(ICO_ICO "${CMAKE_CURRENT_SOURCE_DIR}/ico/ynz[256x256].ico")
    set(ICO_OBJ "${CMAKE_CURRENT_BINARY_DIR}/ico.o")

    add_custom_command(
        OUTPUT  "${ICO_OBJ}"
        COMMAND windres "${ICO_RC}" -o "${ICO_OBJ}"
        DEPENDS "${ICO_RC}" "${ICO_ICO}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Compiling resource: ico.rc -> ico.o"
    )
endif()

# ── 可执行文件 ──────────────────────────────────
add_executable(AutoSZUWeb ${SOURCES} ${ICO_OBJ})

target_include_directories(AutoSZUWeb PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_directories(AutoSZUWeb PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_compile_definitions(AutoSZUWeb PRIVATE CURL_STATICLIB)

# 静态链接
if(MINGW)
    target_link_options(AutoSZUWeb PRIVATE -static -mwindows)
endif()

# ── 链接库 ──────────────────────────────────────
target_link_libraries(AutoSZUWeb
    # curl + TLS
    curl
    ssl
    crypto
    # HTTP/2 HTTP/3
    nghttp2
    nghttp3
    ngtcp2
    ngtcp2_crypto_libressl
    # SSH / 压缩 / DNS
    ssh2
    z
    zstd
    brotlidec
    brotlicommon
    psl
    # Windows 系统库
    ws2_32
    crypt32
    bcrypt
    wldap32
    iphlpapi
    secur32
    shell32
)
