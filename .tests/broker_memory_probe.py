"""Resident + disposable worker measurement. Child windows never become visible."""
import gc
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from broker import Broker
from fixtures import memory_stats

broker = Broker()
initial = memory_stats()
child = subprocess.Popen([sys.executable, "-B", ".tests/memory_probe.py", "0"],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         text=True, encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
output, error = child.communicate(timeout=45)
if child.returncode:
    raise RuntimeError(error)
child_metrics = json.loads(output.splitlines()[0])
gc.collect()
after = memory_stats()
assert child.poll() == 0
seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 60
time.sleep(seconds)
gc.collect()
print(json.dumps({"residentInitial": initial, "workerPeak": child_metrics["peak"],
                  "workerExited": True, "after12Windows": after,
                  "idleSeconds": seconds, "residentIdle": memory_stats()}), flush=True)
