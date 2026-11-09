"""Check public documentation links and launch scripts without proprietary inputs."""
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


def main():
    root = Path(__file__).resolve().parent.parent
    documents = ["README.md", "README.es.md", "CONTRIBUTING.md",
                 "bakugan-recomp/README.md", "docs/PROJECT_GUIDE.md",
                 "docs/HUD_START_BUG.md", "docs/DEFECTOS_Y_INSTALADOR.md"]
    failures = []
    for name in documents:
        document = root / name
        text = re.sub(r"```.*?```", "", document.read_text(encoding="utf-8"), flags=re.S)
        targets = re.findall(r"\]\(([^\s)]+)\)", text)
        targets += re.findall(r'(?:href|src)="([^"]+)"', text)
        for target in targets:
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            if not (document.parent / unquote(parsed.path)).is_file():
                failures.append(f"{name}: missing {target}")
    for name in ("BakuganBrawlers.cmd", "tools/play.ps1", "tools/run.ps1"):
        if not (root / name).is_file():
            failures.append(f"Missing entry point: {name}")
    if failures:
        raise SystemExit("\n".join(failures))
    print("PASS: maintained documentation links and game entry points")


if __name__ == "__main__":
    main()
