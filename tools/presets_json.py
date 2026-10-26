# SPDX-License-Identifier: AGPL-3.0-or-later
"""Read/write presets/factory.json in its hand-editable layout (one pedal per line)."""
import json

def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)

def _num(v):
    v = float(v)
    return int(v) if v.is_integer() else round(v, 3)

def save(data, path):
    d = lambda o: json.dumps(o, ensure_ascii=False)
    lines = ["{", f' "format": {d(data["format"])}, "version": {data["version"]},',
             f' "categories": {d(data["categories"])},', ' "presets": [']
    for pi, p in enumerate(data["presets"]):
        lines.append("  {")
        lines.append(f'   "name": {d(p["name"])}, "category": {d(p["category"])},')
        lines.append(f'   "description": {d(p["description"])},')
        lines.append('   "chain": [')
        for mi, m in enumerate(p["chain"]):
            e = {"type": m["type"]}
            if m.get("bypassed"): e["bypassed"] = True
            e["params"] = {k: _num(v) for k, v in m.get("params", {}).items()}
            lines.append("    " + d(e) + ("," if mi < len(p["chain"]) - 1 else ""))
        lines.append("   ]")
        lines.append("  }" + ("," if pi < len(data["presets"]) - 1 else ""))
    lines += [" ]", "}", ""]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
