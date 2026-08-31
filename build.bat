windres ./res/ico.rc -o ./res/ico.o
g++ -std=c++17 -static -mwindows -DCURL_STATICLIB -I ./include -L ./lib ^
    ./src/AutoSZUWeb.cpp ./src/sys.cpp ./src/web.cpp ./src/file_path.cpp ./src/srun.cpp ./res/ico.o ^
    -lcurl -lssl -lcrypto ^
    -lnghttp2 -lnghttp3 -lngtcp2 -lngtcp2_crypto_libressl ^
    -lssh2 -lz ^
    -lzstd -lbrotlidec -lbrotlicommon -lpsl ^
    -lws2_32 -lcrypt32 -lbcrypt -lwldap32 -liphlpapi -lsecur32 -lshell32 ^
    -o ^
    ./build/AutoSZUWeb.exe
pause