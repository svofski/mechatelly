#!/bin/bash

export TARGET_FPS=20

if [ "$2" == "" ]; then
    echo "usage: $0 input.mp4 output.bin"
    exit 1
fi

echo "CONVERTING TO VIDEO WITH PCM AUDIO"

output_bin=$2
rm -rf in_frames in_audio output_frames
mkdir -p in_frames in_audio output_frames

ffmpeg  -i "$1" -r $TARGET_FPS -vf "crop='0.98*ih:0.98*ih:(iw-0.98*ih)/2:(ih-0.98*ih)/2',scale=32:32" in_frames/frame_%04d.png 

# make it as loud as possible without clipping: compress to 14 LUFS, true peak max out at 0dB with limiter
# save to in_audio/audio.wav
ffmpeg -i "$1" -vn -af "loudnorm=I=-6:TP=0.0:LRA=11,alimiter=level_in=1:level_out=1:limit=1.0:attack=5:release=50" in_audio/audio.wav

# in_audio/audio.wav -> in_audio/audio.pcm (8-bit pcm sample)
./wav-pcm.py && mv output.pcm in_audio/audio.pcm

# mux in_frames/*.png + in_audio/audio.pcm -> output_frames/*.bin (one file per frame)
./png2rgbt.py

# finally glue them together and write to $2
cat output_frames/*.bin >$output_bin

# ...and compress using zx0 
./salvador.exe -classic -w 2000 $output_bin `basename $output_bin .pcm`.pcz