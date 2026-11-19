#!/usr/bin/env python3
"""
based on wav_to_pdm made with parts of gemini slop

- Input: 16-bit WAV (mono or stereo), 44100 Hz (or other)
- Output: 8-bit PCM @ FS_OUT Hz
- Uses resample_poly (anti-aliased) and a 2nd-order sigma-delta modulator
- Optionally writes a reconstructed WAV (for listening/debug)
"""

import wave
import numpy as np
from scipy.signal import resample_poly, firwin, lfilter
import sys
import struct

# ---------- Configuration ----------
INPUT_WAV = "in_audio/audio.wav"
OUTPUT_PCM = "output.pcm"

try:
    TARGET_FPS=int(getenv('TARGET_FPS'))
except:
    TARGET_FPS=20 # defaults to 20 fps, but

FS_OUT = 32*32*TARGET_FPS//2 # each pixel saves 4 bit, one audio sample per 2 pixels ~ 10khz
LPF_CUTOFF = 4000.0     # optional extra low-pass after resampling (Hz)
LPF_TAPS = 129          # FIR taps for optional LPF (odd recommended)
MAX_AMPL = 0.95         # scale to avoid modulator overload (<=1.0)

print(f"wav-pdm2.py: TARGET_FPS={TARGET_FPS} FS_OUT={FS_OUT}")
# -----------------------------------

def read_wav_mono(path):
    with wave.open(path, "rb") as w:
        nch = w.getnchannels()
        sr = w.getframerate()
        sw = w.getsampwidth()
        nframes = w.getnframes()
        raw = w.readframes(nframes)
    assert sw == 2, "Input must be 16-bit PCM"
    data = np.frombuffer(raw, dtype=np.int16).astype(np.float32)
    if nch == 2:
        data = data.reshape(-1, 2)
        data = data.mean(axis=1)   # simple stereo -> mono mix
    # normalize to -1..+1
    data = data / 32768.0
    return data, sr

def write_pcm_bytes(pcm_bytes, out_path):
    with open(out_path, "wb") as f:
        f.write(pcm_bytes)

def write_wav_mono(path, data, sr):
    # data should be float32 in -1..+1
    maxval = np.max(np.abs(data)) if len(data)>0 else 1.0
    if maxval > 1.0:
        data = data / maxval
    ints = (data * 32767.0).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(ints.tobytes())

# input in range -1..1
# output in range 0..255
def pcm8bit(signal):
    x = [int((x/2 + 0.5) * 255) for x in signal]
    return bytes(x)


def main():
    data, sr_in = read_wav_mono(INPUT_WAV)
    print(f"Read {len(data)} samples @ {sr_in} Hz")

    # scale / avoid overload
    data = np.clip(data, -1.0, 1.0)
    data *= MAX_AMPL

    # Resample with anti-alias polyphase resampler
    # Use integer up/down for resample_poly: up = FS_OUT, down = sr_in is fine
    # But to reduce internal filter length, simplify rational factor:
    # We can keep up/down as FS_OUT and sr_in (resample_poly handles it).
    gcd = np.gcd(FS_OUT, sr_in)
    up = FS_OUT // gcd
    down = sr_in // gcd
    print(f"Resampling: up={up}, down={down}")
    resampled = resample_poly(data, up, down)
    print(f"Resampled length: {len(resampled)} samples @ {FS_OUT} Hz")

    # Optional extra LPF to be safe and remove near-Nyquist noise (helps PDM SNR)
    if LPF_CUTOFF is not None and LPF_CUTOFF < (FS_OUT/2.0):
        fir = firwin(LPF_TAPS, LPF_CUTOFF, fs=FS_OUT)
        resampled = lfilter(fir, 1.0, resampled)
        # remove filter startup transient for listening tests (simple)
        # no special de-emphasis here; acceptable for moderate-length audio
        print(f"Applied extra FIR LPF: cutoff={LPF_CUTOFF} Hz, taps={LPF_TAPS}")

    # Ensure -1..+1 bounds after filtering
    resampled = np.clip(resampled, -1.0, 1.0)

    # Convert bipolar -1..1 to the modulator input range.
    # Our sigma-delta implementation expects x in roughly -1..+1
    #pdm_bits = second_order_sigma_delta(resampled)

    # -1..1 fp to 8 bit unsigned
    pcm_bytes = pcm8bit(resampled)

    write_pcm_bytes(pcm_bytes, OUTPUT_PCM)
    print(f"Wrote 8-bit PCM @{FS_OUT}Hz: {OUTPUT_PCM} ({len(pcm_bytes)} bytes)")

if __name__ == "__main__":
    main()
