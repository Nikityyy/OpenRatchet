#!/usr/bin/env python3

import importlib.util
import json
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
    original_vag_header = module.TocParser.parse_vag_header
    original_parse_toc = module.TocParser.parse_toc
    original_dump_toc = module.TocParser.dump_toc

    def parse_vag_header(parser):
        header, length, filename = original_vag_header(parser)
        # Retail headers can retain developer paths such as Z:\I5\sound\spee.
        filename = ntpath.basename(filename.replace("/", "\\")) or "unnamed"
        return header, length, filename

    def parse_toc(parser):
        original_parse_toc(parser)
        # The upstream parser exposes --leveldirs-count but historically stopped
        # after vags2. R&C1's declared 10592-byte TOC has exactly 38 trailing
        # uint32 sector locations (152 bytes), so preserve them in our extraction
        # artifact instead of forcing the native runtime to fabricate that tail.
        parser.leveldirs = []
        for i in range(parser.args.leveldirs_count):
            parser.leveldirs.append(module.Location(num=i, start=parser.read_int32()))

    def dump_toc(parser):
        original_dump_toc(parser)
        if not parser.args.dumptoc:
            return
        toc_path = Path(parser.args.dumptoc)
        with toc_path.open("r", encoding="utf-8") as handle:
            toc = json.load(handle)
        toc["leveldirs"] = [entry._asdict() for entry in parser.leveldirs]
        with toc_path.open("w", encoding="utf-8") as handle:
            json.dump(toc, handle, indent=4)

    module.TocParser.parse_vag_header = parse_vag_header
    module.TocParser.parse_toc = parse_toc
    module.TocParser.dump_toc = dump_toc
    module.main()


if __name__ == "__main__":
    main()
