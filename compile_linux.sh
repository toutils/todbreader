#!/bin/bash
gcc `pkg-config --cflags gtk+-3.0` -O2 -o todbreader todbreader.c `pkg-config --libs gtk+-3.0 sqlite3`
