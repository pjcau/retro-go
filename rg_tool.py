#!/usr/bin/env python3
import argparse
import subprocess
import shutil
import glob
import math
import sys
import re
import os
import struct
import time
import zlib

DEFAULT_TARGET = os.getenv("RG_TOOL_TARGET", "odroid-go")
DEFAULT_BAUD = os.getenv("RG_TOOL_BAUD", "1152000")
DEFAULT_PORT = os.getenv("RG_TOOL_PORT", "COM3")
DEFAULT_APPS = os.getenv("RG_TOOL_APPS", "launcher retro-core prboom-go gwenesis sm64-go retro-extra mame-go mk64-go gbsp sdapp")
# Apps kept on the SD card (/retro-go/apps/<app>.bin) and started through the one
# partition "sdapp": the launcher copies the app's file into it when it is not
# the one already there (launcher/main/applications.c). "sdapp" in an app list
# stands for all of them: they are built, their binaries are gathered in
# sdapps/ for the card, and the first one is put in the partition of the image.
SD_APPS = ["cannonball", "wolf3d-go", "opentyrian-go"]
PROJECT_NAME = os.getenv("PROJECT_NAME", "Retro-Go")
PROJECT_ICON = os.getenv("PROJECT_ICON", "assets/icon.raw")
MAMEROM_SIZE = 4 * 1024 * 1024  # data partition "mamerom", see build_image()
FLASH_SIZE = 16 * 1024 * 1024  # of the handheld: an image larger than this is refused (build_image)
PROJECT_APPS = {
  # Project name  Type, SubType, Size
  'launcher':     [0, 16, 1179648],
  'retro-core':   [0, 16, 1245184],  # 1216 KB (binary 1034 KB): 64 KB went to mk64-go on 2026-10-06, so that the image keeps 64 KB free at its end
  'prboom-go':    [0, 16, 851968],  # 832 KB: the binary outgrew 768 KB and mkfw.py had already grown the partition on the board
  'gwenesis':     [0, 16, 1048576],
  # Super Mario 64 (native port, assets on the SD card). 1.75 MB: exactly the room
  # of Duke Nukem 3D (1 MB) and Quake (0.75 MB), taken out on 2026-10-05 at the
  # user's choice to make a place for it on the 16 MB flash; the binary is 1.55 MB.
  # It is not built by this tool (its assets come from the user's ROM, outside
  # git): see PREBUILT_APPS and sm64-go/build_esp32.sh.
  'sm64-go':      [0, 16, 1835008],
  # Set aside, not deleted: duke3d-go and quake-go still build (`build duke3d-go`)
  # and the launcher still has their tabs, which show only when the partition
  # exists. To put them back, restore these two lines, add them to DEFAULT_APPS
  # and take 1.75 MB from somewhere else (docs: next-steps/n64-native-ports).
  #   'duke3d-go':    [0, 16, 1048576],
  #   'quake-go':     [0, 16, 786432],
  'retro-extra':  [0, 16, 1310720],  # 1.25 MB since 2026-10-01 (MSX/Lynx/2600 gone, binary ~0.5 MB): 256 KB to mame-go
  'mame-go':      [0, 16, 2097152],  # 2 MB since 2026-10-01: the 68000 dynarec (M68KJIT, +39 KB) did not fit 1.75 MB
  # Mario Kart 64 (native port, pack on the SD card). 1344 KB: the room of
  # Wolfenstein 3D and OpenTyrian (640 KB each) plus 64 KB from retro-core; the
  # binary is 1.26 MB without sound. Not built by this tool (its data comes
  # from the user's ROM): see PREBUILT_APPS and mk64-go/build_esp32.sh.
  'mk64-go':      [0, 16, 1376256],
  'gbsp':         [0, 16, 851968],  # GBA (gpSP interpreter, from upstream); fMSX removed 2026-09-29 to make room
  # The partition shared by the apps kept on the SD card (SD_APPS above), 640 KB:
  # it was OutRun's. Wolfenstein 3D (574 KB), OpenTyrian (597 KB) and OutRun
  # (548 KB) take turns in it, at the user's choice of 2026-10-06. After it and
  # the mamerom cache the flash has 64 KB left, and they must stay: the image
  # ends with a 256-byte footer for retro-go's updater (FLASH_SIZE below).
  'sdapp':        [0, 16, 655360],
}
# Apps this tool does not build itself: it takes <app>/build/<app>.bin as it is.
PREBUILT_APPS = {
    "sm64-go": "run sm64-go/build_esp32.sh <baserom.us.z64>: it leaves sm64-go/build/sm64-go.bin",
    "mk64-go": "run mk64-go/build_esp32.sh <mk64.us.z64>: it leaves mk64-go/build/mk64-go.bin",
}
BUILDABLE_ONLY = {  # still buildable on their own, no partition of their own in the image
  'cannonball':   [0, 16, 655360],  # the three SD_APPS: they share "sdapp"
  'wolf3d-go':    [0, 16, 655360],
  'opentyrian-go': [0, 16, 655360],
  'duke3d-go':    [0, 16, 1048576],
  'quake-go':     [0, 16, 786432],
}
# PROJECT_APPS = {}
# for t in glob.glob("*/CMakeLists.txt"):
#     name = os.path.basename(os.path.dirname(t))
#     if name not in PROJECT_APPS:
#         PROJECT_APPS[name] = [0, 0, 0]
try:
    PROJECT_VER = os.getenv("PROJECT_VER") or subprocess.check_output(
        "git describe --tags --abbrev=5 --dirty --always", shell=True
    ).decode().rstrip()
except:
    PROJECT_VER = "unknown"
FW_FORMAT = "none"

TARGETS = []
for t in glob.glob("components/retro-go/targets/*/config.h"):
    TARGETS.append(os.path.basename(os.path.dirname(t)))

IDF_TARGET = os.getenv("IDF_TARGET", "esp32")
IDF_PATH = os.getenv("IDF_PATH")
if not IDF_PATH:
    exit("IDF_PATH is not defined. Are you running inside esp-idf environment?")

if os.name == 'nt':
    IDF_PY = os.path.join(IDF_PATH, "tools", "idf.py")
    IDF_MONITOR_PY = os.path.join(IDF_PATH, "tools", "idf_monitor.py")
    ESPTOOL_PY = os.path.join(IDF_PATH, "components", "esptool_py", "esptool", "esptool.py")
    PARTTOOL_PY = os.path.join(IDF_PATH, "components", "partition_table", "parttool.py")
    GEN_ESP32PART_PY = os.path.join(IDF_PATH, "components", "partition_table", "gen_esp32part.py")
else:
    IDF_PY = "idf.py"
    IDF_MONITOR_PY = "idf_monitor.py"
    ESPTOOL_PY = "esptool.py"
    PARTTOOL_PY = "parttool.py"
    GEN_ESP32PART_PY = "gen_esp32part.py"
MKFW_PY = os.path.join("tools", "mkfw.py")


def run(cmd, cwd=None, check=True):
    print(f"Running command: {' '.join(cmd)}")
    if os.name == 'nt' and cmd[0].endswith(".py"):
        return subprocess.run(["python", *cmd], shell=True, cwd=cwd, check=check)
    return subprocess.run(cmd, shell=False, cwd=cwd, check=check)


def build_image(apps, output_file, img_type="odroid", fatsize=0, target="unknown", version="unknown"):
    print("Building firmware image with: %s\n" % " ".join(apps))
    args = [MKFW_PY, "--type", img_type, "--name", PROJECT_NAME, "--icon", PROJECT_ICON, "--version", PROJECT_VER]

    if img_type not in ["odroid", "esplay"]:
        print("Building bootloader...")
        bootloader_file = os.path.join(os.getcwd(), list(apps)[0], "build", "bootloader", "bootloader.bin")
        if not os.path.exists(bootloader_file):
            run([IDF_PY, "bootloader"], cwd=os.path.join(os.getcwd(), list(apps)[0]))
        args += ["--target", target, "--bootloader", bootloader_file]

    args += [output_file]

    ota_next_id = 16
    for app in apps:
        part = PROJECT_APPS[app]
        subtype = part[1]
        if part[0] == 0 and (part[1] & 0xF0) == 0x10:  # Rewrite OTA indexes to maintain order
            subtype = ota_next_id
            ota_next_id += 1
        binary = os.path.join(app, "build", app + ".bin")
        if app == "sdapp":
            # the shared partition starts with the first SD app in it; all of them
            # are gathered for the card
            os.makedirs("sdapps", exist_ok=True)
            for sd_app in SD_APPS:
                shutil.copyfile(os.path.join(sd_app, "build", sd_app + ".bin"), os.path.join("sdapps", sd_app + ".bin"))
                if os.path.getsize(os.path.join("sdapps", sd_app + ".bin")) > part[2]:
                    exit("%s does not fit the sdapp partition" % sd_app)
            print("SD apps gathered in sdapps/ (copy them to /retro-go/apps/ on the card): %s\n" % " ".join(SD_APPS))
            binary = os.path.join("sdapps", SD_APPS[0] + ".bin")
        args += [str(part[0]), str(subtype), str(part[2]), app, binary]
    if "mame-go" in apps:
        # mame-go serves big read-only ROM regions (gfx, sound samples) from here,
        # memory-mapped, instead of PSRAM (mame-go/main/main.c mamego_flash_store)
        args += ["1", "64", str(MAMEROM_SIZE), "mamerom", "none"]
    if fatsize:
        args += ["1", "129", fatsize, "vfs", "none"]

    run(args)
    # mkfw.py lays the partitions out and appends its 256-byte footer without
    # knowing the flash's size: a layout that fills the flash to the last byte
    # gave an image of 16 MB + 256 bytes on 2026-10-06.
    if img_type not in ["odroid", "esplay"] and os.path.getsize(output_file) > FLASH_SIZE:
        os.rename(output_file, output_file + ".too-big")
        exit("The image is %d bytes larger than the %d MB flash: shrink a partition (kept as %s.too-big)"
             % (os.path.getsize(output_file + ".too-big") - FLASH_SIZE, FLASH_SIZE // 1048576, output_file))


def clean_app(app):
    print("Cleaning up app '%s'..." % app)
    try:
        os.unlink(os.path.join(app, "sdkconfig"))
        os.unlink(os.path.join(app, "sdkconfig.old"))
    except:
        pass
    try:
        shutil.rmtree(os.path.join(app, "build"))
    except:
        pass
    print("Done.\n")


def build_app(app, device_type, with_profiling=False, no_networking=False, is_release=False):
    if app in PREBUILT_APPS and not os.getenv("RG_BUILD_PREBUILT"):  # set by the app's own build script
        prebuilt = os.path.join(app, "build", app + ".bin")
        if not os.path.exists(prebuilt):
            exit("%s is missing: %s" % (prebuilt, PREBUILT_APPS[app]))
        print("Using prebuilt app '%s' (%d bytes)\n" % (app, os.path.getsize(prebuilt)))
        return
    # To do: clean up if any of the flags changed since last build
    print("Building app '%s'" % app)
    args = [IDF_PY, "app"]
    args.append(f"-DRG_PROJECT_APP={app}")
    args.append(f"-DRG_PROJECT_VER={PROJECT_VER}")
    args.append(f"-DRG_BUILD_TARGET=RG_TARGET_{re.sub(r'[^A-Z0-9]', '_', device_type.upper())}")
    args.append(f"-DRG_BUILD_RELEASE={1 if is_release else 0}")
    args.append(f"-DRG_ENABLE_PROFILING={1 if with_profiling else 0}")
    args.append(f"-DRG_ENABLE_NETWORKING={0 if no_networking else 1}")
    with open("partitions.csv", "w") as f:
        f.write("# This table isn't used, it's just needed to avoid esp-idf build failures.\n")
        f.write("dummy, app, ota_0, 65536, 3145728\n")
    run(args, cwd=os.path.join(os.getcwd(), app))
    print("Done.\n")


def flash_app(app, port, baudrate=1152000):
    os.putenv("ESPTOOL_CHIP", IDF_TARGET)
    os.putenv("ESPTOOL_BAUD", str(baudrate))
    os.putenv("ESPTOOL_PORT", port)
    if not os.path.exists("partitions.bin"):
        print("Reading device's partition table...")
        run([ESPTOOL_PY, "read_flash", "0x8000", "0x1000", "partitions.bin"], check=False)
        run([GEN_ESP32PART_PY, "partitions.bin"], check=False)
    app_bin = os.path.join(app, "build", app + ".bin")
    print(f"Flashing '{app_bin}' to port {port}")
    run([PARTTOOL_PY, "--partition-table-file", "partitions.bin", "write_partition", "--partition-name", app, "--input", app_bin])
    print("Done.\n")


def flash_image(image_file, port, baudrate=1152000):
    os.putenv("ESPTOOL_CHIP", IDF_TARGET)
    os.putenv("ESPTOOL_BAUD", str(baudrate))
    os.putenv("ESPTOOL_PORT", port)
    print(f"Flashing image file '{image_file}' to {port}")
    run([ESPTOOL_PY, "write_flash", "--flash_size", "detect", "0x0", image_file])
    print("Done.\n")


def monitor_app(app, port, baudrate=115200):
    print(f"Starting monitor for app {app} on port {port}")
    elf_file = os.path.join(os.getcwd(), app, "build", app + ".elf")
    if os.path.exists(elf_file):
        run([IDF_MONITOR_PY, "--port", port, elf_file])
    else: # We must pass a file to idf_monitor.py but it doesn't have to be valid with -d
        run([IDF_MONITOR_PY, "--port", port, "-d", sys.argv[0]])


parser = argparse.ArgumentParser(description="Retro-Go build tool")
parser.add_argument(
# To do: Learn to use subcommands instead...
    "command", choices=["build-fw", "build-img", "release", "build", "clean", "flash", "monitor", "run", "profile", "install"],
)
parser.add_argument(
    "apps", nargs="*", default="all", choices=["all"] + list(PROJECT_APPS.keys()) + list(BUILDABLE_ONLY.keys())
)
parser.add_argument(
    "--target", default=DEFAULT_TARGET, choices=set(TARGETS), help="Device to target"
)
parser.add_argument(
    "--no-networking", action="store_const", const=True, help="Build without networking support"
)
parser.add_argument(
    "--port", default=DEFAULT_PORT, help="Serial port to use for flash and monitor"
)
parser.add_argument(
    "--baud", default=DEFAULT_BAUD, help="Serial baudrate to use for flashing"
)
parser.add_argument(
    "--fatsize", help="Add FAT storage partition of provided size (500K, 5M,...) to the built image."
)
args = parser.parse_args()

if os.path.exists(f"components/retro-go/targets/{args.target}/env.py"):
    with open(f"components/retro-go/targets/{args.target}/env.py", "rb") as f:
        prev_idf_target = os.getenv("IDF_TARGET")
        exec(f.read())
         # Detect if env.py modified os.environ[IDF_TARGET] instead of IDF_TARGET (old behavior)
        if os.getenv("IDF_TARGET") != prev_idf_target:
            IDF_TARGET = os.getenv("IDF_TARGET")

if os.path.exists(f"components/retro-go/targets/{args.target}/sdkconfig"):
    defaults = os.path.abspath(f"components/retro-go/targets/{args.target}/sdkconfig")
    # RG_SDKCONFIG_EXTRA: more defaults files (semicolon-separated, relative to the
    # target directory), applied on top - e.g. sdkconfig.psram120 for an experiment
    for extra in filter(None, os.getenv("RG_SDKCONFIG_EXTRA", "").split(";")):
        defaults += ";" + os.path.abspath(f"components/retro-go/targets/{args.target}/{extra}")
    os.putenv("SDKCONFIG_DEFAULTS", defaults)
os.putenv("IDF_TARGET", IDF_TARGET)

command = args.command
apps = DEFAULT_APPS.split() if "all" in args.apps else args.apps
apps = [app for app in list(PROJECT_APPS.keys()) + list(BUILDABLE_ONLY.keys()) if app in apps] # Ensure ordering and uniqueness

try:
    if command in ["clean", "release"]:
        print("=== Step: Cleaning ===\n")
        for app in apps:
            for real_app in (SD_APPS if app == "sdapp" else [app]):
                clean_app(real_app)

    if command in ["build", "build-fw", "build-img", "release", "run", "profile", "install"]:
        print("=== Step: Building ===\n")
        for app in apps:
            for real_app in (SD_APPS if app == "sdapp" else [app]):
                build_app(real_app, args.target, command == "profile", args.no_networking, command == "release")

    if command in ["build-fw", "release"]:
        print("=== Step: Packing ===\n")
        if FW_FORMAT in ["odroid", "esplay"]:
            fw_file = ("%s_%s_%s.fw" % (PROJECT_NAME, PROJECT_VER, args.target)).lower()
            build_image(apps, fw_file, FW_FORMAT, args.fatsize, args.target, PROJECT_VER)
        else:
            print("Device doesn't support fw format, try build-img!")

    if command in ["build-img", "release", "install"]:
        print("=== Step: Packing ===\n")
        img_file = ("%s_%s_%s.img" % (PROJECT_NAME, PROJECT_VER, args.target)).lower()
        build_image(apps, img_file, IDF_TARGET, args.fatsize, args.target, PROJECT_VER)

    if command in ["install"]:
        print("=== Step: Flashing entire image to device ===\n")
        # Should probably show a warning here and ask for confirmation...
        img_file = ("%s_%s_%s.img" % (PROJECT_NAME, PROJECT_VER, args.target)).lower()
        flash_image(img_file, args.port, args.baud)

    if command in ["flash", "run", "profile"]:
        print("=== Step: Flashing ===\n")
        try: os.unlink("partitions.bin")
        except: pass
        for app in apps:
            flash_app(app, args.port, args.baud)

    if command in ["monitor", "run", "profile"]:
        print("=== Step: Monitoring ===\n")
        monitor_app(apps[0] if len(apps) else "none", args.port)

    print("All done!")

except KeyboardInterrupt as e:
    exit("\n")

except Exception as e:
    exit(f"\nTask failed: {e}")
