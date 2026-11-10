#!/usr/bin/env python3
"""Add Room Radar commands to a checkout of the pinned M1 ESP-AT source."""
from pathlib import Path
import argparse
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("companion", type=Path)
args = parser.parse_args()
assets = Path(__file__).resolve().parent
main = args.companion / "main"
commands = main / "at_custom_wifi_cmd.c"
config = args.companion / "module_config/module_esp32c6-spi/sdkconfig.defaults"
source = commands.read_bytes()
marker = b"static const esp_at_cmd_struct s_wifi_cmd_list[] = {"
entry = b'    {"+M1RADAR", NULL, at_query_cmd_m1radar, at_setup_cmd_m1radar, NULL},'
if source.count(marker) != 1 or (b"+M1RADAR" in source and entry not in source):
    parser.error("Unsupported ESP-AT Wi-Fi command table")
if b'#include "at_room_radar.inc"' not in source:
    newline = b"\r\n" if b"\r\n" in source else b"\n"
    source = source.replace(marker, b'#include "at_room_radar.inc"' + newline * 2 + marker + newline + entry)
    commands.write_bytes(source)
for name in ("at_room_radar.inc", "radar_metric.h"):
    shutil.copyfile(assets / name, main / name)
settings = config.read_bytes()
if b"CONFIG_ESP_WIFI_CSI_ENABLED=y" not in settings:
    config.write_bytes(settings.rstrip() + b"\n\nCONFIG_ESP_WIFI_CSI_ENABLED=y\n")
