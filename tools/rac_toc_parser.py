#!/usr/bin/env python3

import importlib.util
import ntpath
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit("usage: rac_toc_parser.py <upstream-tocparser.py> [arguments...]")

    parser_path = Path(sys.argv.pop(1)).resolve()
    spec = importlib.util.spec_from_file_location("rac_dvd_tocparser", parser_path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"could not load TOC parser: {parser_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original = module.TocParser.parse_vag_header

    def parse_vag_header(parser):
        header, length, filename = original(parser)
        # Retail headers can retain developer paths such as Z:\I5\sound\spee.
        filename = ntpath.basename(filename.replace("/", "\\")) or "unnamed"
        return header, length, filename

    module.TocParser.parse_vag_header = parse_vag_header
    module.main()


if __name__ == "__main__":
    main()
