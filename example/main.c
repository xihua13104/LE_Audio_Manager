/*
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <speaker|broadcaster|tws|gatt-cli|gatt-srv>\n", argv[0]);
        return -1;
    }

    if (0 == strcmp(argv[1], "speaker")) {
        speaker_demo();
    } else if (0 == strcmp(argv[1], "broadcaster")) {
        broadcaster_demo();
    } else if (0 == strcmp(argv[1], "tws")) {
        tws_demo();
    } else if (0 == strcmp(argv[1], "gatt-cli")) {
        gatt_cli_demo();
    } else if (0 == strcmp(argv[1], "gatt-srv")) {
        gatt_srv_demo();
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        fprintf(stderr, "Usage: %s <speaker|broadcaster|tws|gatt-cli|gatt-srv>\n", argv[0]);
        return -1;
    }

    return 0;
}

