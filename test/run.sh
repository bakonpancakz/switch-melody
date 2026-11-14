#!/bin/sh
# The PC tests: makes test media with ffmpeg (into test/media), builds the
# tests with a native gcc and runs them. Needs ffmpeg, node and gcc on PATH
# (e.g. PATH=/c/msys64/ucrt64/bin:$PATH) and libnx's headers (DEVKITPRO).
set -e
cd "$(dirname "$0")"
DKP=${DEVKITPRO:-/c/devkitPro}
[ -d "$DKP/libnx/include" ] || DKP=/c/devkitPro  # (an msys2 /opt/devkitpro a native gcc can't see)
mkdir -p media
if [ ! -f media/t.opus ]; then
    ff() { ffmpeg -loglevel error -y "$@"; }
    ff -f lavfi -i "testsrc=size=600x600:rate=1" -frames:v 1 media/cover.jpg
    ff -f lavfi -i "testsrc2=size=1400x1400:rate=1" -frames:v 1 -q:v 2 media/bigcover.jpg
    ff -f lavfi -i "sine=frequency=440:duration=20:sample_rate=44100" -i media/cover.jpg -map 0:a -map 1:v \
        -disposition:v attached_pic -ac 2 -metadata title="A very long song title that has to scroll across the overlay" \
        -metadata artist="Test Artist" -metadata album="Test Album" -c:a flac -c:v copy media/t44.flac
    node mkpic.js media/bigcover.jpg media/meta.txt "Opus Song"  # (a cover over several Ogg pages)
    ff -f lavfi -i "sine=frequency=440:duration=20:sample_rate=48000" -i media/meta.txt -map_metadata 1 -ac 2 \
        -c:a libopus -b:a 128k media/t.opus
    ff -f lavfi -i "sine=frequency=1000:duration=15:sample_rate=96000" -ac 2 -sample_fmt s32 -c:a flac media/t96.flac
    ff -f lavfi -i "sine=frequency=440:duration=8:sample_rate=48000" -ac 1 -c:a libopus media/mono.opus
    ff -f lavfi -i "sine=frequency=300:duration=200:sample_rate=48000" -ac 2 -c:a libopus -b:a 96k media/long.opus
fi
C="../common/tags.c ../common/ogg.c ../common/stream_file.c"
gcc -O2 -Wall -o tags_test.exe tags_test.c $C
gcc -O2 -Wall -Wno-unused -o ogg_test.exe ogg_test.c $C
gcc -O2 -Wall -o resample_test.exe resample_test.c ../common/resample.c -lm
gcc -O1 -Wall -Wno-unused-function -Wno-missing-braces -I"$DKP/libnx/include" -o ipc_test.exe ipc_test.c
g++ -O1 -Wall -std=c++17 -I../common -o playlist_test.exe playlist_test.cpp ../common/playlist_file.cpp
g++ -O2 -Wall -std=c++17 -o queue_test.exe queue_test.cpp ../common/track_queue.cpp
gcc -O2 -Wall -o eq_test.exe eq_test.c ../common/eq.c -lm
./tags_test.exe media/t44.flac media/t.opus media/t96.flac media/mono.opus
cmp media/t44.flac.cover media/cover.jpg && cmp media/t.opus.cover media/bigcover.jpg && echo "covers: ok"
./ogg_test.exe media/t.opus media/mono.opus media/long.opus
./resample_test.exe
./ipc_test.exe
./playlist_test.exe
./queue_test.exe
./eq_test.exe
