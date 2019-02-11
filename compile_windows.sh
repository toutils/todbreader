#!/bin/bash
dllpath=/mingw64/bin/

echo "compiling..."
gcc `pkg-config --cflags gtk+-3.0` -O2 -o todbreader todbreader.c `pkg-config --libs gtk+-3.0 sqlite3` -mwindows
#delete the previous build directory
echo "recreating build directory..."
rm -rf build
mkdir build

echo "copying todbreader files..."
cp todbreader.exe build
cp theme.css build
cp builder.ui build
cp README build
cp LICENSE build

echo "copying dlls..."
cp $dllpath/libgdk-3-0.dll build
cp $dllpath/libcairo-gobject-2.dll build
cp $dllpath/libcairo-2.dll build
cp $dllpath/libgcc_s_seh-1.dll build
cp $dllpath/libwinpthread-1.dll build
cp $dllpath/libfontconfig-1.dll build
cp $dllpath/libexpat-1.dll build
cp $dllpath/libfreetype-6.dll build
cp $dllpath/libbz2-1.dll build
cp $dllpath/libharfbuzz-0.dll build
cp $dllpath/libglib-2.0-0.dll build
cp $dllpath/libintl-8.dll build
cp $dllpath/libiconv-2.dll build
cp $dllpath/libpcre-1.dll build
cp $dllpath/libgraphite2.dll build
cp $dllpath/libstdc++-6.dll build
cp $dllpath/libpng16-16.dll build
cp $dllpath/zlib1.dll build
cp $dllpath/libpixman-1-0.dll build
cp $dllpath/libgobject-2.0-0.dll build
cp $dllpath/libffi-6.dll build
cp $dllpath/libepoxy-0.dll build
cp $dllpath/libfribidi-0.dll build
cp $dllpath/libgdk_pixbuf-2.0-0.dll build
cp $dllpath/libgio-2.0-0.dll build
cp $dllpath/libgmodule-2.0-0.dll build
cp $dllpath/libpango-1.0-0.dll build
cp $dllpath/libthai-0.dll build
cp $dllpath/libdatrie-1.dll build
cp $dllpath/libpangocairo-1.0-0.dll build
cp $dllpath/libpangoft2-1.0-0.dll build
cp $dllpath/libpangowin32-1.0-0.dll build
cp $dllpath/libgtk-3-0.dll build
cp $dllpath/libatk-1.0-0.dll build
cp $dllpath/libsqlite3-0.dll build

#copy the gtk icons
echo "copying icons..."
mkdir build/share
cp -R /mingw64/share/icons build/share

#copy gdk-pixbuf files
echo "copying gdk-pixbuf..."
mkdir -p build/lib/gdk-pixbuf-2.0
cp -R /mingw64/lib/gdk-pixbuf-2.0 build/lib

#copy schemas
echo "copying schemas..."
mkdir -p build/share/glib-2.0
cp -R /mingw64/share/glib-2.0/schemas build/share/glib-2.0

#copy licenses
echo "copying licenses..."
cp -R thirdparty_licenses build

