#!/usr/bin/env python3
"""Stream a CI check and expose a bounded failure receipt in GitHub annotations."""
import collections
import re
import subprocess
import sys


def main():
    if len(sys.argv) < 2:
        print("Usage: run_ci_check.py <command> [args...]", file=sys.stderr)
        return 2
    tail = collections.deque(maxlen=120)
    failures = collections.deque(maxlen=30)
    try:
        child = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1)
    except OSError as error:
        print("::error title=Native check could not start::" + escape(str(error)), flush=True)
        return 127
    with child.stdout:
        for line in child.stdout:
            print(line, end="", flush=True)
            tail.append(line[-3000:])
            if re.search(r"error:|fatal error|FAIL(?:ED)?[ :]|Assertion failure|Undefined symbols", line, re.I):
                failures.append(line[-3000:])
    result = child.wait()
    if result:
        # Public Actions pages show annotations even when raw logs require login.
        # Keep the relevant diagnostics and final context rather than the whole log.
        receipt = ("Relevant diagnostics:\n" + "".join(failures)[-7000:]
                   + "\nFinal output:\n" + "".join(tail)[-10000:])
        print("::error title=Native check failed::" + escape(receipt), flush=True)
    return result if result >= 0 else 128 - result


def escape(text):
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


if __name__ == "__main__":
    raise SystemExit(main())
