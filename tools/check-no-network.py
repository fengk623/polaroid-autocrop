#!/usr/bin/env python3
"""Offline guarantee (SECURITY-PRIVACY.md): product sources must not use networking APIs."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
PATTERNS = [r"#include\s*<(sys/socket|netinet/|arpa/inet|winsock2?|ws2tcpip|curl/)", r"\bQtNetwork\b",
            r"Qt6::Network", r"\bQNetworkAccessManager\b", r"\bQTcpSocket\b", r"\bQUdpSocket\b", r"\bWinHttp\w*\(",
            r"\bInternetOpen\w*\(", r"\bNSURLSession\b", r"\bgetaddrinfo\(", r"\bsocket\("]
bad = []
for d in ("core", "app", "platform", "tools/cli"):
    base = ROOT / d
    if not base.exists():
        continue
    for f in base.rglob("*"):
        if f.suffix not in {".cpp", ".hpp", ".h", ".c", ".mm", ".qml", ".txt", ".cmake"}:
            continue
        text = f.read_text(encoding="utf-8", errors="ignore")
        for p in PATTERNS:
            for m in re.finditer(p, text):
                bad.append(f"{f.relative_to(ROOT)}: '{m.group(0)}'")
for b in bad:
    print("network API found:", b)
sys.exit(1 if bad else 0)
