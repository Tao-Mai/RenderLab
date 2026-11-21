from pathlib import Path
from sys import argv


source, output = map(Path, argv[1:])
data = source.read_bytes()
output.parent.mkdir(parents=True, exist_ok=True)

kBytesPerLine = 16
temporary = output.with_suffix(".tmp")
with temporary.open("w", encoding="ascii", newline="\n") as stream:
    for start in range(0, len(data), kBytesPerLine):
        row = data[start:start + kBytesPerLine]
        stream.write("    " + " ".join(f"0x{value:02x}," for value in row) + "\n")

temporary.replace(output)
