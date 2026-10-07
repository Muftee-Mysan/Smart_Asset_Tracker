#!/usr/bin/env python3
"""Export the asset log from the device over SWD (CLAUDE.md 7.2).

Before running: on the device, Admin (hold D 3 s, PIN) -> 1 Export. The OLED
shows "EXPORT MODE". Then run, from the project folder:

    python tools/export_log.py [--elf build/Release/asset_tracker.elf]

Steps: find g_export / g_time_sync_mailbox in the ELF, attach without reset,
post the PC's UTC time to the mailbox (the device logs a sync record and sets
its clock), wait for the export buffer to refresh, upload it, check magic and
CRC-32, decode the records, join them with tools/students.csv, correct the
device clock drift between sync records, and write
asset_log_<YYYYmmdd_HHMM>.csv. The script never clears the log.

Use --no-sync to read the log without setting the device clock.
"""
import argparse
import csv
import datetime as dt
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Must match App/services/export.h and App/services/storage.h.
EXPORT_MAGIC = 0x54505845           # "EXPT"
TIME_SYNC_MAGIC = 0x454D4954        # "TIME"
EXPORT_VERSION = 1
HEADER_FMT = "<IHHIIIIIHHI"         # magic .. crc32, 36 bytes
HEADER_SIZE = struct.calcsize(HEADER_FMT)
RECORD_SIZE = 24
REC_TYPE_SCAN = 0xA1
REC_TYPE_SYNC = 0xA2
REC_FLAG_TIME_VALID = 0x01

SYNC_TIMEOUT_S = 5.0
SWD_FREQ_KHZ = 1000                 # 4 MHz is unreliable on the ST-LINK in use
CLI_FALLBACK = os.path.join(os.environ.get("LOCALAPPDATA", ""), "stm32cube", "bundles",
                            "programmer", "2.23.0", "bin", "STM32_Programmer_CLI.exe")


def fail(msg):
    print(f"export_log.py: error: {msg}", file=sys.stderr)
    sys.exit(1)


def default_elf():
    rel = os.path.join(ROOT, "build", "Release", "asset_tracker.elf")
    dbg = os.path.join(ROOT, "build", "Debug", "asset_tracker.elf")
    found = [p for p in (rel, dbg) if os.path.exists(p)]
    if not found:
        fail("no ELF found in build/Release or build/Debug; pass --elf")
    # The spec default is the Release build. If the Debug ELF is newer, that is
    # most likely what is on the device, so prefer it and say so.
    elf = max(found, key=os.path.getmtime)
    return elf


def tz_offset_min():
    try:
        with open(os.path.join(ROOT, "App", "config.h"), encoding="utf-8") as f:
            m = re.search(r"#define\s+TZ_OFFSET_MIN\s+(-?\d+)", f.read())
            if m:
                return int(m.group(1))
    except OSError:
        pass
    return 330


def symbol_addresses(elf, names):
    nm = shutil.which("arm-none-eabi-nm")
    if nm is None:
        fail("arm-none-eabi-nm not found on PATH")
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    addrs = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in names:
            addrs[parts[2]] = int(parts[0], 16)
    missing = [n for n in names if n not in addrs]
    if missing:
        fail(f"symbol(s) not in {elf}: {', '.join(missing)}")
    return addrs


class Programmer:
    def __init__(self):
        self.cli = shutil.which("STM32_Programmer_CLI") or (
            CLI_FALLBACK if os.path.exists(CLI_FALLBACK) else None)
        if self.cli is None:
            fail("STM32_Programmer_CLI not found on PATH")

    def run(self, *args):
        cmd = [self.cli, "-c", "port=SWD", "mode=HOTPLUG", f"freq={SWD_FREQ_KHZ}", *args]
        res = subprocess.run(cmd, capture_output=True, text=True)
        text = res.stdout + res.stderr
        if res.returncode != 0 or "Error" in text:
            err = [l for l in text.splitlines() if "Error" in l]
            fail("ST-LINK: " + ("; ".join(err) or f"exit code {res.returncode}") +
                 "\nIs the device awake in EXPORT MODE?")
        return text

    def read32(self, addr, nbytes):
        text = self.run("-r32", f"0x{addr:08X}", str(nbytes))
        words = []
        for line in text.splitlines():
            m = re.match(r"^0x[0-9A-Fa-f]{8}\s*:\s*(.*)$", line.strip())
            if m:
                words += [int(w, 16) for w in m.group(1).split()]
        data = b"".join(struct.pack("<I", w) for w in words)
        if len(data) < nbytes:
            fail(f"short read at 0x{addr:08X}: {len(data)} of {nbytes} bytes")
        return data[:nbytes]

    def upload(self, addr, nbytes):
        fd, path = tempfile.mkstemp(suffix=".bin")
        os.close(fd)
        try:
            self.run("-u", f"0x{addr:08X}", str(nbytes), path)
            with open(path, "rb") as f:
                return f.read()
        finally:
            os.remove(path)


def parse_header(raw):
    (magic, version, record_size, generation, count, capacity, rtc_now, lsi_hz,
     vdd_mv, fw, crc) = struct.unpack_from(HEADER_FMT, raw)
    return dict(magic=magic, version=version, record_size=record_size, generation=generation,
                count=count, capacity=capacity, rtc_now=rtc_now, lsi_hz=lsi_hz, vdd_mv=vdd_mv,
                fw=fw, crc=crc)


def crc16_ccitt_false(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode_records(raw, count):
    recs = []
    for i in range(count):
        r = raw[i * RECORD_SIZE:(i + 1) * RECORD_SIZE]
        rtype, flags, uid_len = r[0], r[1], r[2]
        seq, rtc, value, crc = struct.unpack_from("<IIIH", r, 10)
        recs.append(dict(index=i, type=rtype, flags=flags, uid=r[3:3 + min(uid_len, 7)],
                         seq=seq, rtc=rtc, value=value,
                         crc_ok=(crc == crc16_ccitt_false(r[:22]))))
    return recs


def load_students(path):
    students = {}
    if not os.path.exists(path):
        return students
    with open(path, newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            uid = (row.get("uid") or "").strip().upper()
            if uid:
                students[uid] = ((row.get("student_id") or "").strip(),
                                 (row.get("name") or "").strip())
    return students


def correct_times(recs):
    """Drift correction (CLAUDE.md 7.2 step 7).

    Sync i set the clock to H_i. Sync i+1 says the clock read D_{i+1} just
    before being set to H_{i+1}. Between them:
        corrected = H_i + (rtc - H_i) * (H_{i+1} - H_i) / (D_{i+1} - H_i)
    After the last sync: extrapolate with the last scale factor.
    Records with time_valid == 0 are "unsynced".
    """
    syncs = [r for r in recs if r["type"] == REC_TYPE_SYNC and r["crc_ok"]]
    syncs.sort(key=lambda r: r["seq"])
    scales = []
    for a, b in zip(syncs, syncs[1:]):
        span_dev = b["rtc"] - a["value"]
        span_true = b["value"] - a["value"]
        scales.append(span_true / span_dev if span_dev > 0 else 1.0)

    for r in recs:
        if r["type"] != REC_TYPE_SCAN:
            continue
        if not (r["flags"] & REC_FLAG_TIME_VALID):
            r["corrected"], r["quality"] = None, "unsynced"
            continue
        prev = [i for i, s in enumerate(syncs) if s["seq"] < r["seq"]]
        if not prev:
            # Time was valid but the sync that set it is no longer in the log
            # (cleared): no reference point, report the device time.
            r["corrected"], r["quality"] = float(r["rtc"]), "uncorrected"
            continue
        i = prev[-1]
        h_i = syncs[i]["value"]
        if i + 1 < len(syncs):
            r["corrected"] = h_i + (r["rtc"] - h_i) * scales[i]
            r["quality"] = "corrected"
        else:
            scale = scales[-1] if scales else 1.0
            r["corrected"] = h_i + (r["rtc"] - h_i) * scale
            r["quality"] = "extrapolated"


def fmt_utc(t):
    return dt.datetime.fromtimestamp(t, dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


def fmt_local(t, offset_min):
    tz = dt.timezone(dt.timedelta(minutes=offset_min))
    return dt.datetime.fromtimestamp(t, tz).strftime("%Y-%m-%d %H:%M:%S")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--elf", help="firmware ELF on the device (default: newest of "
                                  "build/Release and build/Debug)")
    ap.add_argument("--students", default=os.path.join(ROOT, "tools", "students.csv"))
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--no-sync", action="store_true", help="do not set the device clock")
    args = ap.parse_args()

    elf = args.elf or default_elf()
    print(f"ELF: {elf}")
    addrs = symbol_addresses(elf, ("g_export", "g_time_sync_mailbox"))
    exp, mbox = addrs["g_export"], addrs["g_time_sync_mailbox"]
    print(f"g_export = 0x{exp:08X}, g_time_sync_mailbox = 0x{mbox:08X}")

    prog = Programmer()
    hdr = parse_header(prog.read32(exp, HEADER_SIZE + 4))
    if hdr["magic"] != EXPORT_MAGIC:
        fail(f"export magic 0x{hdr['magic']:08X} (expected 0x{EXPORT_MAGIC:08X}). "
             "Is the device in EXPORT MODE, and is --elf the firmware it runs?")

    if not args.no_sync:
        gen0 = hdr["generation"]
        host_unix = int(time.time())
        # host_unix first, then the magic (CLAUDE.md 7.2 step 3).
        prog.run("-w32", f"0x{mbox + 4:08X}", f"0x{host_unix:08X}",
                 "-w32", f"0x{mbox:08X}", f"0x{TIME_SYNC_MAGIC:08X}")
        deadline = time.time() + SYNC_TIMEOUT_S
        while True:
            hdr = parse_header(prog.read32(exp, HEADER_SIZE + 4))
            if hdr["magic"] == EXPORT_MAGIC and hdr["generation"] != gen0:
                break
            if time.time() > deadline:
                fail("device did not answer the time sync within 5 s")
            time.sleep(0.2)
        print(f"Time synced to {fmt_utc(host_unix)} UTC")

    if hdr["version"] != EXPORT_VERSION or hdr["record_size"] != RECORD_SIZE:
        fail(f"unsupported export version {hdr['version']} / record size {hdr['record_size']}")
    if hdr["count"] > hdr["capacity"]:
        fail(f"record count {hdr['count']} > capacity {hdr['capacity']}")

    raw = prog.upload(exp, HEADER_SIZE + hdr["count"] * RECORD_SIZE)
    check = parse_header(raw)
    if check["generation"] != hdr["generation"] or check["magic"] != EXPORT_MAGIC:
        fail("export buffer changed during upload; run again")
    records_raw = raw[HEADER_SIZE:HEADER_SIZE + hdr["count"] * RECORD_SIZE]
    if zlib.crc32(records_raw) != hdr["crc"]:
        fail("CRC-32 mismatch over the records: upload corrupted, run again")

    recs = decode_records(records_raw, hdr["count"])
    bad = [r for r in recs if not r["crc_ok"]]
    correct_times(recs)
    students = load_students(args.students)
    offset = tz_offset_min()

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M")
    out_path = os.path.join(args.out_dir, f"asset_log_{stamp}.csv")
    scans = [r for r in recs if r["type"] == REC_TYPE_SCAN]
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["seq", "uid", "student_id", "name", "asset_id", "device_time_utc",
                    "corrected_local_time", "time_quality"])
        for r in scans:
            uid = r["uid"].hex().upper()
            sid, name = students.get(uid, ("", "(unregistered)"))
            quality = r["quality"] if r["crc_ok"] else "CRC ERROR"
            corrected = fmt_local(r["corrected"], offset) if r["corrected"] is not None else ""
            w.writerow([r["seq"], uid, sid, name, r["value"], fmt_utc(r["rtc"]), corrected,
                        quality])

    syncs = len([r for r in recs if r["type"] == REC_TYPE_SYNC])
    print(f"Device: FW {hdr['fw'] >> 8}.{hdr['fw'] & 0xFF}, VDD {hdr['vdd_mv']} mV, "
          f"LSI {hdr['lsi_hz']} Hz, clock {fmt_utc(hdr['rtc_now'])} UTC")
    print(f"Records: {hdr['count']} / {hdr['capacity']} ({len(scans)} scans, {syncs} syncs)")
    if bad:
        print(f"WARNING: {len(bad)} record(s) with a bad CRC-16 (marked in the CSV)")
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
