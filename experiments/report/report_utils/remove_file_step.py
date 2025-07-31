from pathlib import Path
import contextlib


def remove_file(path: Path):
    with contextlib.suppress(FileNotFoundError):
        path.unlink()
