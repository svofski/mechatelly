#!/usr/bin/env python3

import os
from PIL import Image

# output format: rrrrggggbbbbssss, where each field is a value between 0..15

pdm_bytes = []

# audio is input
def load_audio(pathname):
    global pdm_bytes
    with open(pathname, 'rb') as pdmfile:
        pdm_bytes = bytearray(pdmfile.read())
        print(f"Read {len(pdm_bytes)} from {pathname}, which is enough for {len(pdm_bytes)*2//(32*32)} frames")


def convert_images(input_path, output_path):
    global pdm_bytes

    if not os.path.exists(output_path):
        os.makedirs(output_path)

    pdm_index = 0
    nframes = 0

    for filename in sorted(os.listdir(input_path)):
        if filename.endswith('.png'):
            # Load image and resize to 32x32 pixels
            img = Image.open(os.path.join(input_path, filename)).resize((32, 32), resample=Image.BILINEAR)
            
            # Convert image data to a numpy array for easy manipulation
            pixel_array = img.convert('RGB').getdata()

            packed_data = bytearray()
            for rgb in pixel_array:
                r, g, b = ((c + 15) // 32 for c in rgb)  # r, g, b in range 0..8 (in the future: try 0..16)

                # take sound nibbles
                t = 0
                idx = pdm_index // 2
                if idx < len(pdm_bytes):
                    t = pdm_bytes[idx]
                    if pdm_index % 2 == 0:
                        t = 0xf & (t >> 4)
                    else:
                        t = t & 0xf
                    pdm_index += 1
                
                # put them all together and append to packed_data
                byte1 = ((r << 4) | g).to_bytes(1, 'big')
                byte2 = ((b << 4) | t).to_bytes(1, 'big')
                packed_data.extend(byte1)
                packed_data.extend(byte2)

            output_filename = os.path.join(output_path, f"{os.path.splitext(filename)[0]}.bin")
            
            with open(output_filename, "wb") as file:
                file.write(packed_data)

            nframes += 1

    print(f"Processed {nframes} frames, of which {pdm_index//(32*32)} with sound; written to {output_path}")


if __name__ == "__main__":
    load_audio('in_audio/audio.pcm')
    convert_images('in_frames', 'output_frames')
