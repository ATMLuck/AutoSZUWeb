cmake_minimum_required(VERSION 3.20)
project(AutoSZUWeb VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUTOSZUWEB_BUILD_TESTS "Build platform and logic tests" ON)

set(AUTOSZUWEB_CORE_SOURCES
    src/file_path.cpp
    src/network.cpp
    src/platform_support.cpp
    src/runtime.cpp
    src/single_instance.cpp
    src/sys.cpp
    src/user_data.cpp
)

set(AUTOSZUWEB_APP_SOURCES
    src/AutoSZUWeb.cpp
    src/web.cpp
    src/srun.cpp
)

if(APPLE)
    enable_language(OBJCXX)
    list(APPEND AUTOSZUWEB_APP_SOURCES src/mac_ui.mm)
endif()

add_library(AutoSZUWebCore STATIC ${AUTOSZUWEB_CORE_SOURCES})
target_include_directories(AutoSZUWebCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(AutoSZUWebCore PUBLIC cxx_std_17)

if(WIN32)
    target_include_directories(AutoSZUWebCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(MINGW AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/libcrypto.a")
        target_link_libraries(AutoSZUWebCore PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/libcrypto.a")
    else()
        find_package(OpenSSL REQUIRED)
        target_link_libraries(AutoSZUWebCore PUBLIC OpenSSL::Crypto)
    endif()
    target_link_libraries(AutoSZUWebCore PUBLIC ws2_32 crypt32 bcrypt shell32)
elseif(APPLE)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(AutoSZUWebCore PUBLIC OpenSSL::Crypto "-framework Security")
elseif(UNIX AND AUTOSZUWEB_BUILD_TESTS)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(AutoSZUWebCore PUBLIC AUTOSZUWEB_LINUX_TEST=1)
    target_link_libraries(AutoSZUWebCore PUBLIC OpenSSL::Crypto)
else()
    message(FATAL_ERROR "AutoSZUWeb supports Windows and macOS; Linux is available only for tests")
endif()

if(WIN32 OR APPLE)
    if(WIN32 AND MINGW)
        set(ICO_RC  "${CMAKE_CURRENT_SOURCE_DIR}/res/ico.rc")
        set(ICO_ICO "${CMAKE_CURRENT_SOURCE_DIR}/ico/ynz[256x256].ico")
        set(ICO_OBJ "${CMAKE_CURRENT_BINARY_DIR}/ico.o")
        add_custom_command(
            OUTPUT "${ICO_OBJ}"
            COMMAND windres "${ICO_RC}" -o "${ICO_OBJ}"
            DEPENDS "${ICO_RC}" "${ICO_ICO}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Compiling Windows icon resource"
            VERBATIM
        )
        list(APPEND AUTOSZUWEB_APP_SOURCES "${ICO_OBJ}")
    endif()

    if(APPLE)
        add_executable(AutoSZUWeb MACOSX_BUNDLE ${AUTOSZUWEB_APP_SOURCES})
    else()
        add_executable(AutoSZUWeb ${AUTOSZUWEB_APP_SOURCES})
    endif()

    target_include_directories(AutoSZUWeb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(AutoSZUWeb PRIVATE AutoSZUWebCore)

    if(WIN32)
        target_compile_definitions(AutoSZUWeb PRIVATE CURL_STATICLIB)
        target_link_directories(AutoSZUWeb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
        if(MINGW)
            target_link_options(AutoSZUWeb PRIVATE -static -mwindows)
        endif()
        target_link_libraries(AutoSZUWeb PRIVATE
            curl ssl crypto
            nghttp2 nghttp3 ngtcp2 ngtcp2_crypto_libressl
            ssh2 z zstd brotlidec brotlicommon psl
            ws2_32 crypt32 bcrypt wldap32 iphlpapi secur32 shell32
        )
    elseif(APPLE)
        find_package(CURL REQUIRED)
        find_package(OpenSSL REQUIRED)
        find_package(nlohmann_json CONFIG REQUIRED)
        target_link_libraries(AutoSZUWeb PRIVATE
            CURL::libcurl OpenSSL::SSL OpenSSL::Crypto nlohmann_json::nlohmann_json
            "-framework Cocoa" "-framework Security"
        )
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
            ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
            @ONLY
        )
        set_target_properties(AutoSZUWeb PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER com.autoszuweb.AutoSZUWeb
            MACOSX_BUNDLE_BUNDLE_NAME AutoSZUWeb
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        )
    endif()
endif()

if(AUTOSZUWEB_BUILD_TESTS)
    enable_testing()
    add_executable(AutoSZUWebTests tests/test_main.cpp)
    target_link_libraries(AutoSZUWebTests PRIVATE AutoSZUWebCore)
    if(APPLE)
        target_sources(AutoSZUWebTests PRIVATE src/mac_ui.mm)
        target_link_libraries(AutoSZUWebTests PRIVATE "-framework Cocoa")

        add_executable(AutoSZUWebMacUITests
            tests/mac_ui_test.mm
            src/mac_ui.mm)
        target_include_directories(AutoSZUWebMacUITests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(AutoSZUWebMacUITests PRIVATE "-framework Cocoa")
        add_test(NAME AutoSZUWebMacUITests COMMAND AutoSZUWebMacUITests)
        set_tests_properties(AutoSZUWebMacUITests PROPERTIES TIMEOUT 15)
    endif()
    if(WIN32)
        target_link_directories(AutoSZUWebTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
        if(MINGW)
            target_link_options(AutoSZUWebTests PRIVATE -static)
        endif()
    endif()
    add_test(NAME AutoSZUWebTests COMMAND AutoSZUWebTests)
endif()
