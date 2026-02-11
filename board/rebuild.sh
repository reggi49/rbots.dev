#!/bin/bash
cd /Users/reggi/Downloads/rbot/board
source /Users/reggi/.espressif/v5.5.2/esp-idf/export.sh
idf.py build
idf.py flash
idf.py monitor
