#!/usr/bin/env python3
"""Forward strokes from a selected Gemini PR or Plover HID writer to LilyGO.

Both devices connect independently to an approved USB host. No networking,
general keyboard capture, stroke logging or USB power changes.
"""
import argparse
import sys


# Plover HID report 0x50: eight bytes of key state in MSB-first lever order.
# See https://github.com/dnaq/plover-machine-hid for the protocol.
PLOVER_USAGE_PAGE = 0xFF50
PLOVER_USAGE = 0x4C56
PLOVER_REPORT_ID = 0x50
HID_LEVERS = (
    "S1", "T", "K", "P", "W", "H", "R", "A",
    "O", "*1", "-E", "-U", "-F", "-R", "-P", "-B",
    "-L", "-G", "-T", "-S", "-D", "-Z", "#1", "S2",
    "*2", "*3", "*4", "#2", "#3", "#4", "#5", "#6",
    "#7", "#8", "#9", "#A", "#B", "#C", "X1", "X2",
    "X3", "X4",
)
# Gemini PR payload order, seven bits per byte; the high bit marks a frame.
# See https://docs.qmk.fm/features/stenography#geminipr
GEMINI_LEVERS = (
    "X1", "#1", "#2", "#3", "#4", "#5", "#6",
    "S1", "S2", "T", "K", "P", "W", "H",
    "R", "A", "O", "*1", "*2", "X3", "X4",
    "X2", "*3", "*4", "-E", "-U", "-F", "-R",
    "-P", "-B", "-L", "-G", "-T", "-S", "-D",
    "#7", "#8", "#9", "#A", "#B", "#C", "-Z",
)
HID_TO_GEMINI = tuple(GEMINI_LEVERS.index(key) for key in HID_LEVERS)


def packets(stream):
    """Yield valid-sized Gemini PR frames, resynchronizing on a start byte."""
    frame = bytearray()
    for byte in stream:
        if byte & 0x80:
            frame = bytearray([byte])
        elif frame:
            frame.append(byte)
            if len(frame) == 6:
                yield bytes(frame)
                frame.clear()


def serial_bytes(port):
    while True:
        yield from port.read(port.in_waiting or 1)


def hid_to_gemini(state):
    """Encode a completed Plover HID chord as Gemini PR, ignoring X5-X26."""
    frame = bytearray([0x80, 0, 0, 0, 0, 0])
    for lever, target in enumerate(HID_TO_GEMINI):
        if state & (1 << (63 - lever)):
            frame[target // 7] |= 0x40 >> (target % 7)
    # No representable keys: do not send an empty stroke.
    return bytes(frame) if any(frame[1:]) or frame[0] != 0x80 else None


class HidChord:
    """Accumulate all depressed steno keys, emitting once all are released."""

    def __init__(self):
        self.state = 0

    def feed(self, report):
        # Some platforms pad input reports. Ignore the padding, but never
        # interpret truncated or unrelated reports as key releases.
        if len(report) < 9 or report[0] != PLOVER_REPORT_ID:
            return None
        current = int.from_bytes(bytes(report[1:9]), "big")
        if current:
            self.state |= current
            return None
        completed, self.state = self.state, 0
        return hid_to_gemini(completed)


def plover_devices(hid):
    """Select only Plover's distinct vendor-defined steno HID collection."""
    unique = {}
    for device in hid.enumerate():
        if (device.get("usage_page") == PLOVER_USAGE_PAGE
                and device.get("usage") == PLOVER_USAGE and device.get("path")):
            unique[device["path"]] = device
    return list(unique.values())


def import_hid():
    # Linux hidraw reports the usage page; libusb-based packages may not.
    try:
        import hidraw as hid
    except ImportError:
        try:
            import hid
        except ImportError:
            sys.exit("Install hidapi: python -m pip install hidapi")
    return hid


def describe_device(device):
    name = device.get("product_string") or "Plover HID writer"
    return f"{name} ({device['vendor_id']:04x}:{device['product_id']:04x})"


def list_hid(devices):
    if not devices:
        print("No Plover HID interface found. Check the connection and writer firmware.")
    for index, device in enumerate(devices, 1):
        print(f"{index}: {describe_device(device)}")


def forward_hid(hid, device, target):
    writer = hid.device()
    try:
        writer.open_path(device["path"])
        chord = HidChord()
        print(f"Connected to {describe_device(device)}.")
        print("Switch the writer to Plover HID mode. Waiting for strokes (Ctrl+C to stop).")
        while True:
            report = writer.read(64, 100)
            if report:
                packet = chord.feed(report)
                if packet is not None:
                    target.write(packet)
    finally:
        writer.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    listing = p.add_mutually_exclusive_group()
    listing.add_argument("--list", action="store_true", help="list serial ports")
    listing.add_argument("--hid-list", action="store_true", help="list Plover HID writers")
    sources = p.add_mutually_exclusive_group()
    sources.add_argument("--keyboard", help="Gemini PR serial port")
    sources.add_argument("--hid", action="store_true", help="use a Plover HID writer")
    p.add_argument("--hid-device", type=int, metavar="N",
                   help="one-based number from --hid-list (required if multiple writers)")
    p.add_argument("--display", help="LilyGO serial port")
    args = p.parse_args()

    if args.hid_list:
        try:
            list_hid(plover_devices(import_hid()))
        except OSError as error:
            sys.exit(f"HID error: {error}")
        return

    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        sys.exit("Install pyserial: python -m pip install pyserial")
    if args.list:
        for port in list_ports.comports():
            print(port.device, port.description, port.hwid)
        return

    if not args.display or not (args.keyboard or args.hid):
        p.error("Specify --display and either --keyboard PORT or --hid")
    if args.keyboard and args.keyboard.casefold() == args.display.casefold():
        p.error("The keyboard and display ports must be distinct")
    if args.hid_device is not None and not args.hid:
        p.error("--hid-device requires --hid")

    try:
        if args.hid:
            hid = import_hid()
            devices = plover_devices(hid)
            if not devices:
                sys.exit("No Plover HID writer found. Run --hid-list and check your connection.")
            if len(devices) > 1 and args.hid_device is None:
                list_hid(devices)
                p.error("Select one writer using --hid-device N")
            index = args.hid_device if args.hid_device is not None else 1
            if not 1 <= index <= len(devices):
                p.error("Invalid --hid-device number; run --hid-list")
            with serial.Serial(args.display, 115200, write_timeout=1) as target:
                forward_hid(hid, devices[index - 1], target)
        else:
            with serial.Serial(args.keyboard, 9600, timeout=.1) as key, \
                    serial.Serial(args.display, 115200, write_timeout=1) as target:
                print("Forwarding Gemini PR packets (Ctrl+C to stop).")
                for packet in packets(serial_bytes(key)):
                    target.write(packet)
    except KeyboardInterrupt:
        print("\nStopped.")
    except serial.SerialException as error:
        sys.exit(f"Serial error: {error}")
    except OSError as error:
        sys.exit(f"HID or I/O error: {error}. Close other writer applications and retry.")


if __name__ == "__main__":
    main()
