#include <Arduino.h>
#include <LittleFS.h>

int movie_nframes = 0;
int movie_frame = 0;
static File file;

int movie_open()
{
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return -1;
    }

    file = LittleFS.open("/bboop.bin", "r");
    if (!file) {
        Serial.println("Failed to open movie");
        return -1;
    }

    Serial.print("Dataset opened successfully. Size: ");
    Serial.print(file.size());
    Serial.println(" bytes.");

    movie_nframes = file.size() / (32 * 32);
    movie_frame = 0;

    return 0;
}

void movie_get_next_frame(uint8_t *dst)
{
    ++movie_frame;
    file.read(dst, 32 * 32);
    if (!file.available()) {
        file.seek(0, SeekSet);
        movie_frame = 0;
    }
}
