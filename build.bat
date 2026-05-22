windres ico.rc -o ico.o
g++ -std=c++17 -g -static -DCURL_STATICLIB -I ./include ^
    -L ./lib AutoSZUWeb.cpp ico.o -lcurl -lssl -lcrypto ^
    -lnghttp2 -lnghttp3 -lngtcp2 -lngtcp2_crypto_libressl ^
    -lssh2 -lz ^
    -lzstd -lbrotlidec -lbrotlicommon -lpsl ^
    -lws2_32 -lcrypt32 -lbcrypt -lwldap32 -liphlpapi -lsecur32 -lshell32 ^
    -o ^
    ./build/AutoSZUWeb.exe
pause