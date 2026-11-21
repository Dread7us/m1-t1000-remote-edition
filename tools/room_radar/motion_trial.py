#!/usr/bin/env python3
"""Record a labeled quiet/walking trial over USB; never infer labels from RF data."""
import argparse
import datetime
import json
from pathlib import Path
import statistics
import time
from device_check import RoomScope, list_ports

PHASES = ("quiet_a", "walking_a", "quiet_b", "walking_b")
MIN_SAMPLES = 12


def analyze(records):
    groups = {phase: [r for r in records if r["phase"] == phase] for phase in PHASES}
    if any(len(rows) < 3 or any(r.get("state") != 2 or r.get("samples", 0) < MIN_SAMPLES
                               for r in rows) for rows in groups.values()):
        return {"verdict": "Inconclusive", "reason": "Missing or sparse labeled measurements"}
    scores = {key: [r["score"] for r in rows] for key, rows in groups.items()}
    quiet, walking = scores["quiet_a"], scores["walking_a"]
    result = {"scores": scores, "scope": "Two labeled blocks; no people-count or general reliability claim"}
    if min(walking) > max(quiet):
        direction, threshold = "higher", (min(walking) + max(quiet)) / 2
    elif max(walking) < min(quiet):
        direction, threshold = "lower", (max(walking) + min(quiet)) / 2
    else:
        return dict(result, verdict="Inconclusive", reason="Quiet and walking scores overlap in the first block")
    positive = lambda value: value > threshold if direction == "higher" else value < threshold
    false_alarms = sum(positive(value) for value in scores["quiet_b"])
    detected = sum(positive(value) for value in scores["walking_b"])
    result.update(direction=direction, candidate_threshold=threshold,
                  holdout_false_alarms=false_alarms, holdout_walking_detected=detected,
                  holdout_quiet_windows=3, holdout_walking_windows=3)
    result["verdict"] = "Separated in this trial" if false_alarms == 0 and detected == 3 else "Inconclusive"
    result["reason"] = "Threshold selected on block A and checked on block B; not deployed to firmware"
    result["quiet_medians"] = [statistics.median(scores[key]) for key in ("quiet_a", "quiet_b")]
    return result


def cue(phase):
    walking = phase.startswith("walking")
    print("WALK NOW: walk normally back and forth across the room." if walking else
          "STAY STILL NOW: keep everyone and pets still; leave the M1 untouched.", flush=True)
    try:
        import winsound
        for _ in range(2 if walking else 1):
            winsound.Beep(1000 if walking else 600, 350)
            time.sleep(0.15)
    except (ImportError, RuntimeError):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port")
    parser.add_argument("--start", action="store_true", help="Start only after the participant confirms readiness.")
    args = parser.parse_args()
    if not args.start:
        parser.error("Confirm readiness, then pass --start. One low beep means still; two high beeps mean walk.")
    ports = [p.device for p in list_ports.comports()
             if (p.vid, p.pid, p.serial_number) == (0x0483, 0x5750, "3177384F3434")]
    if not args.port and len(ports) != 1:
        parser.error("Connect exactly one M1")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "labels": "Participant instructed by phase; adherence not independently verified", "records": []}
    device = RoomScope(args.port or ports[0])
    def save():
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(json.dumps(report, indent=2), encoding="utf-8")
        temporary.replace(args.output)
    try:
        device.request(1, expected=2)
        if "+CWJAP:" not in device.at("AT+CWJAP?"):
            raise RuntimeError("Join Wi-Fi on the M1 first")
        status = device.radar()
        if status["state"] == 1:
            raise RuntimeError("Exit the on-device Room Motion screen before starting")
        for phase in PHASES:
            cue(phase)
            time.sleep(8)  # Allow the participant to settle or begin walking.
            for index in range(3):
                result = device.job(1)
                record = dict(result, phase=phase, index=index + 1,
                              utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
                report["records"].append(record)
                save()
                print(json.dumps(record), flush=True)
        report["analysis"] = analyze(report["records"])
        report["wifi_after"] = "+CWLAP:" in device.at("AT+CWLAP")
        print("DONE: you can stop walking.", flush=True)
    except (RuntimeError, TimeoutError, OSError) as error:
        report["error"] = str(error)
        report["analysis"] = {"verdict": "Inconclusive", "reason": "Trial interrupted"}
        print("STOP: " + str(error), flush=True)
    finally:
        device.port.close()
        save()
    print(json.dumps(report.get("analysis")), flush=True)
    return 1 if "error" in report else 0


if __name__ == "__main__":
    raise SystemExit(main())
