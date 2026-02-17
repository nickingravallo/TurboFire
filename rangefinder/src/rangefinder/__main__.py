"""Entry point for python -m rangefinder."""

try:
    from rangefinder.app import RangefinderApp
except ModuleNotFoundError as e:
    if "textual" in str(e).lower():
        print("Missing dependency: textual")
        print("Install with:  pip install textual")
        print("Or from project:  pip install -e .")
        raise SystemExit(1) from e
    raise


def main() -> None:
    app = RangefinderApp()
    app.run()


if __name__ == "__main__":
    main()
