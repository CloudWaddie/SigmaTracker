#!/usr/bin/env python3
"""Forward only a selected Polyglot serial port's Gemini PR packets to LilyGO.

Uses two separate USB device connections to an approved host. No networking,
keystroke capture from other apps, logging, or USB power changes.
"""
import argparse
import sys
def packets(stream):
    frame=bytearray()
    for b in stream:
        if b&128: frame=bytearray([b])
        elif frame:
            frame.append(b)
            if len(frame)==6:
                yield bytes(frame)
                frame.clear()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--list",action="store_true")
    p.add_argument("--keyboard")
    p.add_argument("--display")
    args=p.parse_args()
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        sys.exit("Install pyserial: python -m pip install pyserial")
    if args.list:
        for port in list_ports.comports():
            print(port.device,port.description,port.hwid)
        return
    if not args.keyboard or not args.display or args.keyboard==args.display:
        p.error("Specify two distinct ports with --keyboard and --display")
    try:
        with serial.Serial(args.keyboard,9600,timeout=.1) as key, serial.Serial(args.display,115200,write_timeout=1) as target:
            print("Forwarding Gemini PR packets (Ctrl+C to stop).")
            frame=bytearray()
            while True:
                buf=key.read(key.in_waiting or 1)
                for b in buf:
                    if b&128: frame=bytearray([b])
                    elif frame:
                        frame.append(b)
                        if len(frame)==6:
                            target.write(frame)
                            frame.clear()
    except KeyboardInterrupt:
        print("\nStopped.")
    except serial.SerialException as e:
        sys.exit(f"Serial error: {e}")
if __name__=="__main__":main()
