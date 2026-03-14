#ifdef ESP32
    #include <getChipId.h>

    uint32_t getChipId(void) {
    uint32_t chipId = 0;
    for (int i = 0; i < 17; i = i + 8) {
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    return chipId;
    }
#endif