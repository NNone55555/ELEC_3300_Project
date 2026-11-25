"""Create local Flash keys for one mouse/receiver pair without displaying them."""
import argparse
from pathlib import Path
import secrets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rotate", action="store_true", help="Replace both keys. Existing credentials become unreadable.")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    for relative, symbol in (
        ("shared/private/radio_key.h", "RADIO_KEY"),
        ("mouse/private/profile_key.h", "PROFILE_KEY"),
    ):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with path.open("w" if args.rotate else "x", encoding="ascii") as output:
                key = ", ".join(f"0x{byte:02x}" for byte in secrets.token_bytes(16))
                output.write(f"static const uint8_t {symbol}[16] = {{ {key} }};\n")
        except FileExistsError:
            print(f"Kept {relative}")
        else:
            print(f"Provisioned {relative}")


if __name__ == "__main__":
    main()
