"""One-time version 1 -> version 2 scene conversion; runtime only reads version 2."""
import argparse
import copy
import json
import math
import os
from pathlib import Path
import tempfile


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def identity():
    return [[float(i == j) for j in range(4)] for i in range(4)]


def inverse(matrix):
    rows = [list(row) + identity()[i] for i, row in enumerate(matrix)]
    for column in range(4):
        pivot = max(range(column, 4), key=lambda i: abs(rows[i][column]))
        rows[column], rows[pivot] = rows[pivot], rows[column]
        divisor = rows[column][column]
        if not math.isfinite(divisor) or divisor == 0:
            raise ValueError("Singular transform")
        rows[column] = [value / divisor for value in rows[column]]
        for i in range(4):
            if i != column:
                factor = rows[i][column]
                rows[i] = [value - factor * other for value, other in zip(rows[i], rows[column])]
    return [row[4:] for row in rows]


def compose(obj):
    for key in ("position", "rotation", "scale"):
        values = obj[key]
        if len(values) != 3 or any(isinstance(v, bool) or not isinstance(v, (int, float)) or
                                   not math.isfinite(v) for v in values):
            raise ValueError(f"{obj['id']}: invalid {key}")
    if any(abs(v) < 1e-6 for v in obj["scale"]):
        raise ValueError(f"{obj['id']}: zero scale")
    sx, sy, sz = obj["scale"]
    x, y, z = obj["rotation"]
    cx, cy, cz, ax, ay, az = math.cos(x), math.cos(y), math.cos(z), math.sin(x), math.sin(y), math.sin(z)
    scale = [[sx, 0, 0, 0], [0, sy, 0, 0], [0, 0, sz, 0], [0, 0, 0, 1]]
    rx = [[1, 0, 0, 0], [0, cx, ax, 0], [0, -ax, cx, 0], [0, 0, 0, 1]]
    ry = [[cy, 0, -ay, 0], [0, 1, 0, 0], [ay, 0, cy, 0], [0, 0, 0, 1]]
    rz = [[cz, az, 0, 0], [-az, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    translation = identity()
    translation[3][:3] = obj["position"]
    return multiply(multiply(multiply(multiply(scale, rx), ry), rz), translation)


def matches(a, b):
    return all(math.isfinite(a[i][j]) and math.isfinite(b[i][j]) and
               abs(a[i][j] - b[i][j]) <= 1e-4 * max(1, abs(b[i][j]))
               for i in range(4) for j in range(4))


def decompose(matrix, reference):
    result = copy.deepcopy(reference)
    signs = [math.copysign(1, s) for s in reference["scale"]]
    a, b, c = matrix[:3]
    determinant = (a[0]*(b[1]*c[2]-b[2]*c[1]) - a[1]*(b[0]*c[2]-b[2]*c[0]) +
                   a[2]*(b[0]*c[1]-b[1]*c[0]))
    if math.copysign(1, determinant) != math.prod(signs):
        signs[0] *= -1
    scales = [math.sqrt(sum(v*v for v in matrix[i][:3])) * signs[i] for i in range(3)]
    if any(not math.isfinite(s) or abs(s) < 1e-6 for s in scales):
        raise ValueError(f"{reference['id']}: local scale outside supported range")
    r = [[v/scales[i] for v in matrix[i][:3]] for i in range(3)]
    y = math.asin(max(-1, min(1, -r[0][2])))
    if abs(math.cos(y)) > 1e-4:
        x, z = math.atan2(r[1][2], r[2][2]), math.atan2(r[0][1], r[0][0])
    else:
        z = reference["rotation"][2]
        x = math.atan2(r[1][0], r[1][1])+z if y > 0 else math.atan2(-r[1][0], r[1][1])-z
    def nearest(angles):
        return [old + math.remainder(new-old, math.tau) for old, new in zip(reference["rotation"], angles)]
    choices = [nearest([x, y, z]), nearest([x+math.pi, math.pi-y, z+math.pi])]
    result["rotation"] = min(choices, key=lambda values: sum((v-old)**2 for v, old in zip(values, reference["rotation"])))
    result["position"], result["scale"] = matrix[3][:3], scales
    if not matches(compose(result), matrix):
        raise ValueError(f"{reference['id']}: local transform requires shear; conversion cancelled")
    return result


def resolve(objects):
    indices = {obj["id"]: i for i, obj in enumerate(objects)}
    if len(indices) != len(objects) or any(not obj["id"] for obj in objects):
        raise ValueError("Empty or duplicate object ID")
    parents = []
    for obj in objects:
        parent = obj.get("parent", "")
        if parent and parent not in indices:
            raise ValueError(f"{obj['id']}: missing parent {parent}")
        parents.append(indices[parent] if parent else None)
    matrices, visited = [compose(obj) for obj in objects], [0] * len(objects)
    for start in range(len(objects)):
        chain, current = [], start
        while current is not None and visited[current] == 0:
            visited[current] = 1
            chain.append(current)
            current = parents[current]
        if current is not None and visited[current] == 1:
            raise ValueError("Parent cycle")
        for index in reversed(chain):
            if parents[index] is not None:
                matrices[index] = multiply(matrices[index], matrices[parents[index]])
            visited[index] = 2
    return matrices


def convert(document):
    version = document.get("version")
    if version not in (1, 2) or isinstance(version, bool):
        raise ValueError("Unsupported scene version")
    space = document.get("transformSpace", "world" if version == 1 else "local")
    if space not in ("world", "local") or (version == 2 and "transformSpace" in document):
        raise ValueError("Unsupported coordinate convention")
    source = document["objects"]
    resolve(source)  # Validate hierarchy even when the old placements are world coordinates.
    original = [compose(obj) for obj in source] if space == "world" else resolve(source)
    result = copy.deepcopy(document)
    if space == "world":
        indices = {obj["id"]: i for i, obj in enumerate(source)}
        for index, obj in enumerate(source):
            if obj.get("parent"):
                local = multiply(original[index], inverse(original[indices[obj["parent"]]]))
                result["objects"][index] = decompose(local, obj)
    rebuilt = resolve(result["objects"])
    if not all(matches(a, b) for a, b in zip(original, rebuilt)):
        raise ValueError("Conversion would change world placement")
    result["version"] = 2
    result.pop("transformSpace", None)
    return result


def format_document(document, original_text):
    # Preserve unchanged object lines, including their original numeric spelling.
    lines = {}
    for line in original_text.splitlines():
        candidate = line.strip().removesuffix(",")
        if candidate.startswith("{") and '"id"' in candidate:
            try:
                obj = json.loads(candidate)
                if "id" in obj:
                    lines[obj["id"]] = (obj, candidate)
            except json.JSONDecodeError:
                pass
    rows = []
    for obj in document["objects"]:
        previous = lines.get(obj["id"])
        rows.append(previous[1] if previous and previous[0] == obj else
                    json.dumps(obj, ensure_ascii=False, separators=(",", ":"), allow_nan=False))
    fields = ["  " + json.dumps(key) + ": " + json.dumps(value, ensure_ascii=False, allow_nan=False)
              for key, value in document.items() if key != "objects"]
    return "{\n" + ",\n".join(fields) + ',\n  "objects": [\n' + ",\n".join("    " + row for row in rows) + '\n  ]\n}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--write", action="store_true", help="Atomically replace validated files (default: validate only)")
    args = parser.parse_args()
    prepared = []
    for path in args.paths:
        original = path.read_text(encoding="utf-8-sig")
        prepared.append((path, convert(json.loads(original)), original))
    for path, document, original in prepared:
        if args.write:
            text = format_document(document, original)
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as temporary:
                temporary.write(text)
                temporary.flush()
                os.fsync(temporary.fileno())
            try:
                os.replace(temporary.name, path)
            finally:
                if os.path.exists(temporary.name):
                    os.unlink(temporary.name)
        print(f"{'Converted' if args.write else 'Validated'}: {path} ({len(document['objects'])} objects)")


if __name__ == "__main__":
    main()
