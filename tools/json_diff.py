#!/usr/bin/env python3
"""
json_diff.py - Recursive structural diff engine for browser test output.

Compares expected vs actual JSON output from any pipeline stage.
Exits with:
  0 - all checks pass
  1 - CRITICAL failures
  2 - only MINOR failures
"""

import json
import sys
import os
from pathlib import Path
from typing import Any, Optional


# ---------------------------------------------------------------------------
# Diff entry
# ---------------------------------------------------------------------------
class DiffEntry:
    def __init__(self, path: str, expected: Any, actual: Any,
                 severity: str, category: str):
        self.path = path
        self.expected = expected
        self.actual = actual
        self.severity = severity  # CRITICAL or MINOR
        self.category = category  # PARSE_ERROR, CASCADE_ERROR, etc.

    def __str__(self) -> str:
        return (f"[{self.severity}] [{self.category}] {self.path}\n"
                f"  expected: {json.dumps(self.expected, ensure_ascii=False)}\n"
                f"  actual:   {json.dumps(self.actual, ensure_ascii=False)}")

    def to_json(self) -> dict:
        return {
            "path": self.path,
            "expected": self.expected,
            "actual": self.actual,
            "severity": self.severity,
            "category": self.category,
        }


# ---------------------------------------------------------------------------
# Diff engine
# ---------------------------------------------------------------------------
class Differ:
    def __init__(self, mode: str):
        self.mode = mode
        self.entries: list[DiffEntry] = []
        self.checks = 0

    def _path(self, *parts: str) -> str:
        return ".".join(str(p) for p in parts if p)

    def _add(self, path: str, expected: Any, actual: Any,
             severity: str, category: str):
        self.entries.append(DiffEntry(path, expected, actual, severity, category))

    # -----------------------------------------------------------------------
    # DOM mode
    # -----------------------------------------------------------------------
    def _diff_node(self, exp: dict, act: dict, path: str):
        self.checks += 1
        if exp.get("type") != act.get("type"):
            self._add(self._path(path, "type"), exp.get("type"), act.get("type"),
                      "CRITICAL", "PARSE_ERROR")
            return

        typ = exp.get("type")
        if typ == "element":
            self._diff_element(exp, act, path)
        elif typ == "text":
            self._diff_text(exp, act, path)
        elif typ == "comment":
            self._diff_comment(exp, act, path)
        elif typ == "doctype":
            self._diff_doctype(exp, act, path)

    def _diff_element(self, exp: dict, act: dict, path: str):
        if exp.get("tag") != act.get("tag"):
            self._add(self._path(path, "tag"), exp.get("tag"), act.get("tag"),
                      "CRITICAL", "PARSE_ERROR")

        exp_attrs = exp.get("attributes", {})
        act_attrs = act.get("attributes", {})
        # Sort attribute keys to avoid spurious order diffs (P1-F)
        for k in sorted(set(list(exp_attrs.keys()) + list(act_attrs.keys()))):
            ev = exp_attrs.get(k)
            av = act_attrs.get(k)
            if ev is None and av is not None:
                self._add(self._path(path, "attributes", k), "<missing>", av,
                          "CRITICAL", "MISSING_NODE")
            elif ev is not None and av is None:
                self._add(self._path(path, "attributes", k), ev, "<missing>",
                          "CRITICAL", "EXTRA_NODE")
            elif ev != av:
                self._add(self._path(path, "attributes", k), ev, av,
                          "CRITICAL", "WRONG_VALUE")

        exp_kids = exp.get("children", [])
        act_kids = act.get("children", [])
        self._diff_children(exp_kids, act_kids, path)

    def _diff_text(self, exp: dict, act: dict, path: str):
        ed = exp.get("data", "")
        ad = act.get("data", "")
        if ed != ad:
            # Check if it's an encoding error (multibyte vs garbled)
            if any(ord(c) > 127 for c in ed) and any(ord(c) > 127 for c in ad):
                sev, cat = "CRITICAL", "ENCODING_ERROR"
            else:
                sev, cat = "CRITICAL", "WRONG_VALUE"
            self._add(self._path(path, "data"), ed, ad, sev, cat)

        en = exp.get("data_normalized", "")
        an = act.get("data_normalized", "")
        if en != an:
            # Whitespace-only differences are MINOR
            if en.strip() == "" and an.strip() == "":
                self._add(self._path(path, "data_normalized"), en, an,
                          "MINOR", "WRONG_VALUE")

    def _diff_comment(self, exp: dict, act: dict, path: str):
        if exp.get("data") != act.get("data"):
            self._add(self._path(path, "data"), exp.get("data"), act.get("data"),
                      "CRITICAL", "WRONG_VALUE")

    def _diff_doctype(self, exp: dict, act: dict, path: str):
        for key in ("name", "public_id", "system_id"):
            if exp.get(key) != act.get(key):
                self._add(self._path(path, key), exp.get(key), act.get(key),
                          "CRITICAL", "WRONG_VALUE")

    def _diff_children(self, exp_kids: list, act_kids: list, path: str):
        max_len = max(len(exp_kids), len(act_kids))
        for i in range(max_len):
            child_path = self._path(path, f"children[{i}]")
            if i >= len(exp_kids):
                self._add(child_path, "<missing>",
                          json.dumps(act_kids[i], ensure_ascii=False),
                          "CRITICAL", "EXTRA_NODE")
            elif i >= len(act_kids):
                self._add(child_path,
                          json.dumps(exp_kids[i], ensure_ascii=False),
                          "<missing>", "CRITICAL", "MISSING_NODE")
            else:
                # Whitespace-only text node presence/absence -> MINOR
                ek = exp_kids[i]
                ak = act_kids[i]
                if (ek.get("type") == "text" and ak.get("type") == "text" and
                        ek.get("data", "").strip() == "" and
                        ak.get("data", "").strip() == ""):
                    if ek.get("data") != ak.get("data"):
                        self._add(child_path, ek.get("data"), ak.get("data"),
                                  "MINOR", "WRONG_VALUE")
                    continue
                self._diff_node(ek, ak, child_path)

    # -----------------------------------------------------------------------
    # CSS mode
    # -----------------------------------------------------------------------
    def _diff_css_value(self, ev: dict, av: dict, path: str):
        if ev.get("type") != av.get("type"):
            self._add(self._path(path, "type"), ev.get("type"), av.get("type"),
                      "CRITICAL", "CASCADE_ERROR")
            return
        if ev.get("important") != av.get("important"):
            self._add(self._path(path, "important"), ev.get("important"), av.get("important"),
                      "CRITICAL", "CASCADE_ERROR")

    def _diff_css(self, exp: dict, act: dict):
        self._diff_css_rules_list(exp.get("rules", []), act.get("rules", []), "rules")
        self._diff_css_atrules_list(exp.get("at_rules", []), act.get("at_rules", []), "at_rules")

    def _diff_css_rules_list(self, exp_rules: list, act_rules: list, path: str):
        max_len = max(len(exp_rules), len(act_rules))
        for i in range(max_len):
            rp = self._path(path, f"[{i}]")
            if i >= len(exp_rules):
                self._add(rp, "<missing>", json.dumps(act_rules[i]), "CRITICAL", "EXTRA_NODE")
            elif i >= len(act_rules):
                self._add(rp, json.dumps(exp_rules[i]), "<missing>", "CRITICAL", "MISSING_NODE")
            else:
                er = exp_rules[i]
                ar = act_rules[i]
                self._diff_css_rule(er, ar, rp)

    def _diff_css_rule(self, er: dict, ar: dict, path: str):
        # Compare selectors
        es = er.get("selectors", [])
        as_ = ar.get("selectors", [])
        for j in range(max(len(es), len(as_))):
            sp = self._path(path, f"selectors[{j}]")
            if j >= len(es):
                self._add(sp, "<missing>", as_[j], "CRITICAL", "EXTRA_NODE")
            elif j >= len(as_):
                self._add(sp, es[j], "<missing>", "CRITICAL", "MISSING_NODE")
            elif es[j] != as_[j]:
                self._add(sp, es[j], as_[j], "CRITICAL", "WRONG_VALUE")
        # Compare declarations
        ed = er.get("declarations", [])
        ad = ar.get("declarations", [])
        for j in range(max(len(ed), len(ad))):
            dp = self._path(path, f"declarations[{j}]")
            if j >= len(ed):
                self._add(dp, "<missing>", json.dumps(ad[j]), "CRITICAL", "EXTRA_NODE")
            elif j >= len(ad):
                self._add(dp, json.dumps(ed[j]), "<missing>", "CRITICAL", "MISSING_NODE")
            else:
                self._diff_css_decl(ed[j], ad[j], dp)

    def _diff_css_decl(self, ed: dict, ad: dict, path: str):
        if ed.get("property") != ad.get("property"):
            self._add(self._path(path, "property"), ed.get("property"), ad.get("property"),
                      "CRITICAL", "WRONG_VALUE")
        if ed.get("important") != ad.get("important"):
            self._add(self._path(path, "important"), ed.get("important"), ad.get("important"),
                      "CRITICAL", "CASCADE_ERROR")
        ev = ed.get("value", "")
        av = ad.get("value", "")
        if ev != av:
            # Numeric values within 0.5 tolerance are MINOR
            try:
                ef = float(ev)
                af = float(av)
                sev = "MINOR" if abs(ef - af) <= 0.5 else "CRITICAL"
            except (ValueError, TypeError):
                sev = "CRITICAL"
            self._add(self._path(path, "value"), ev, av, sev, "WRONG_VALUE")

    def _diff_css_atrules_list(self, exp_rules: list, act_rules: list, path: str):
        for i in range(max(len(exp_rules), len(act_rules))):
            rp = self._path(path, f"[{i}]")
            if i >= len(exp_rules):
                self._add(rp, "<missing>", json.dumps(act_rules[i]), "CRITICAL", "EXTRA_NODE")
            elif i >= len(act_rules):
                self._add(rp, json.dumps(exp_rules[i]), "<missing>", "CRITICAL", "MISSING_NODE")
            else:
                ea = exp_rules[i]
                aa = act_rules[i]
                if ea.get("name") != aa.get("name"):
                    self._add(self._path(rp, "name"), ea.get("name"), aa.get("name"),
                              "CRITICAL", "WRONG_VALUE")
                if ea.get("prelude") != aa.get("prelude"):
                    self._add(self._path(rp, "prelude"), ea.get("prelude"), aa.get("prelude"),
                              "CRITICAL", "WRONG_VALUE")
                self._diff_css_rules_list(ea.get("rules", []), aa.get("rules", []),
                                          self._path(rp, "rules"))

    # -----------------------------------------------------------------------
    # Cascade mode
    # -----------------------------------------------------------------------
    def _diff_cascade(self, exp: dict, act: dict):
        """Compare cascade element lists by index order (P1-A)."""
        exp_els = exp.get("elements", [])
        act_els = act.get("elements", [])
        max_len = max(len(exp_els), len(act_els))
        for i in range(max_len):
            path = f"elements[{i}]"
            if i >= len(exp_els):
                self._add(path, "<missing>",
                          json.dumps(act_els[i], ensure_ascii=False),
                          "CRITICAL", "EXTRA_NODE")
            elif i >= len(act_els):
                self._add(path, json.dumps(exp_els[i], ensure_ascii=False),
                          "<missing>", "CRITICAL", "MISSING_NODE")
            else:
                ee = exp_els[i]
                ae = act_els[i]
                # Tag/ID mismatch
                if ee.get("tag") != ae.get("tag") or ee.get("id") != ae.get("id"):
                    self._add(self._path(path, "tag"), ee.get("tag"), ae.get("tag"),
                              "CRITICAL", "PARSE_ERROR")
                self._diff_computed(ee.get("computed", {}),
                                    ae.get("computed", {}),
                                    path)

    def _diff_computed(self, exp_props: dict, act_props: dict, path: str):
        all_props = set(list(exp_props.keys()) + list(act_props.keys()))
        for prop in all_props:
            pp = self._path(path, prop)
            if prop not in exp_props:
                self._add(pp, "<missing>", json.dumps(act_props[prop]),
                          "CRITICAL", "EXTRA_NODE")
            elif prop not in act_props:
                self._add(pp, json.dumps(exp_props[prop]), "<missing>",
                          "CRITICAL", "MISSING_NODE")
            else:
                self._diff_css_value_obj(exp_props[prop], act_props[prop], pp)

    def _diff_css_value_obj(self, ev: dict, av: dict, path: str):
        et = ev.get("type")
        at = av.get("type")
        if et != at:
            self._add(self._path(path, "type"), et, at, "CRITICAL", "CASCADE_ERROR")
            return
        if et == "KEYWORD":
            if ev.get("value") != av.get("value"):
                self._add(self._path(path, "value"), ev.get("value"), av.get("value"),
                          "CRITICAL", "CASCADE_ERROR")
        elif et == "LENGTH":
            try:
                ef = float(ev.get("value", 0))
                af = float(av.get("value", 0))
                if abs(ef - af) > 0.5:
                    self._add(self._path(path, "value"), ef, af,
                              "CRITICAL", "CASCADE_ERROR")
                elif abs(ef - af) > 0:
                    self._add(self._path(path, "value"), ef, af,
                              "MINOR", "CASCADE_ERROR")
            except (ValueError, TypeError):
                if ev.get("value") != av.get("value"):
                    self._add(self._path(path, "value"), ev.get("value"), av.get("value"),
                              "CRITICAL", "CASCADE_ERROR")
            if ev.get("unit") != av.get("unit"):
                self._add(self._path(path, "unit"), ev.get("unit"), av.get("unit"),
                          "CRITICAL", "CASCADE_ERROR")
        elif et == "COLOR":
            for ch in ("r", "g", "b", "a"):
                try:
                    ef = float(ev.get(ch, 0))
                    af = float(av.get(ch, 0))
                    if abs(ef - af) > 2:
                        self._add(self._path(path, ch), ef, af,
                                  "CRITICAL", "CASCADE_ERROR")
                    elif abs(ef - af) > 0:
                        self._add(self._path(path, ch), ef, af,
                                  "MINOR", "CASCADE_ERROR")
                except (ValueError, TypeError):
                    if ev.get(ch) != av.get(ch):
                        self._add(self._path(path, ch), ev.get(ch), av.get(ch),
                                  "CRITICAL", "CASCADE_ERROR")
        else:
            if ev.get("value") != av.get("value"):
                self._add(self._path(path, "value"), ev.get("value"), av.get("value"),
                          "CRITICAL", "CASCADE_ERROR")

    # -----------------------------------------------------------------------
    # Layout mode
    # -----------------------------------------------------------------------
    def _diff_layout(self, exp: dict, act: dict, path: str = "root"):
        self.checks += 1

        # Tag mismatch
        if exp.get("tag") != act.get("tag"):
            self._add(self._path(path, "tag"), exp.get("tag"), act.get("tag"),
                      "CRITICAL", "PARSE_ERROR")

        # is_text
        if exp.get("is_text") != act.get("is_text"):
            self._add(self._path(path, "is_text"), exp.get("is_text"), act.get("is_text"),
                      "CRITICAL", "PARSE_ERROR")

        # Text content
        if exp.get("text") != act.get("text"):
            self._add(self._path(path, "text"), exp.get("text"), act.get("text"),
                      "CRITICAL", "WRONG_VALUE")

        # Rect comparison helper
        def diff_rect(rect_name: str, er: dict, ar: dict):
            for field in ("x", "y", "width", "height"):
                ef = er.get(field, 0)
                af = ar.get(field, 0)
                try:
                    diff = abs(float(ef) - float(af))
                except (ValueError, TypeError):
                    diff = 999
                if diff > 1.0:
                    sev = "CRITICAL"
                elif diff > 0:
                    sev = "MINOR"
                else:
                    continue
                self._add(self._path(path, rect_name, field),
                          ef, af, sev, "LAYOUT_ERROR")

        diff_rect("content", exp.get("content", {}), act.get("content", {}))
        diff_rect("margin", exp.get("margin", {}), act.get("margin", {}))
        diff_rect("padding", exp.get("padding", {}), act.get("padding", {}))
        diff_rect("border", exp.get("border", {}), act.get("border", {}))

        # Text lines
        exp_lines = exp.get("text_lines", [])
        act_lines = act.get("text_lines", [])
        for i in range(max(len(exp_lines), len(act_lines))):
            lp = self._path(path, f"text_lines[{i}]")
            if i >= len(exp_lines):
                self._add(lp, "<missing>", json.dumps(act_lines[i]),
                          "CRITICAL", "EXTRA_NODE")
            elif i >= len(act_lines):
                self._add(lp, json.dumps(exp_lines[i]), "<missing>",
                          "CRITICAL", "MISSING_NODE")
            elif exp_lines[i].get("text") != act_lines[i].get("text"):
                self._add(self._path(lp, "text"),
                          exp_lines[i].get("text"), act_lines[i].get("text"),
                          "CRITICAL", "WRONG_VALUE")

        # Children
        exp_kids = exp.get("children", [])
        act_kids = act.get("children", [])
        for i in range(max(len(exp_kids), len(act_kids))):
            cp = self._path(path, f"children[{i}]")
            if i >= len(exp_kids):
                self._add(cp, "<missing>",
                          json.dumps(act_kids[i], ensure_ascii=False),
                          "CRITICAL", "EXTRA_NODE")
            elif i >= len(act_kids):
                self._add(cp, json.dumps(exp_kids[i], ensure_ascii=False),
                          "<missing>", "CRITICAL", "MISSING_NODE")
            else:
                self._diff_layout(exp_kids[i], act_kids[i], cp)

    # -----------------------------------------------------------------------
    # Display list mode
    # -----------------------------------------------------------------------
    def _diff_display_list(self, exp_list: list, act_list: list):
        max_len = max(len(exp_list), len(act_list))
        for i in range(max_len):
            pp = f"commands[{i}]"
            if i >= len(exp_list):
                self._add(pp, "<missing>",
                          json.dumps(act_list[i]), "MINOR", "EXTRA_NODE")
            elif i >= len(act_list):
                self._add(pp, json.dumps(exp_list[i]), "<missing>",
                          "CRITICAL", "MISSING_NODE")
            else:
                self._diff_command(exp_list[i], act_list[i], pp)

    def _diff_command(self, ec: dict, ac: dict, path: str):
        if ec.get("cmd") != ac.get("cmd"):
            self._add(self._path(path, "cmd"), ec.get("cmd"), ac.get("cmd"),
                      "CRITICAL", "LAYOUT_ERROR")
        for field in ("x", "y", "w", "h"):
            try:
                diff = abs(float(ec.get(field, 0)) - float(ac.get(field, 0)))
            except (ValueError, TypeError):
                diff = 999
            if diff > 1:
                self._add(self._path(path, field), ec.get(field), ac.get(field),
                          "CRITICAL", "LAYOUT_ERROR")
        if ec.get("color") != ac.get("color"):
            self._add(self._path(path, "color"), ec.get("color"), ac.get("color"),
                      "CRITICAL", "WRONG_VALUE")
        if ec.get("text") != ac.get("text"):
            self._add(self._path(path, "text"), ec.get("text"), ac.get("text"),
                      "CRITICAL", "WRONG_VALUE")
        if ec.get("font_size") != ac.get("font_size"):
            self._add(self._path(path, "font_size"), ec.get("font_size"), ac.get("font_size"),
                      "CRITICAL", "WRONG_VALUE")

    # -----------------------------------------------------------------------
    # Public API
    # -----------------------------------------------------------------------
    def diff(self, expected: dict, actual: dict) -> list[DiffEntry]:
        self.entries = []
        self.checks = 0

        if self.mode == "dom":
            self._diff_children(
                expected.get("children", []),
                actual.get("children", []),
                "document")
        elif self.mode == "css":
            self._diff_css(expected, actual)
        elif self.mode == "cascade":
            self._diff_cascade(expected, actual)
        elif self.mode == "layout":
            # Layout output is a single root node
            self._diff_layout(expected, actual)
        elif self.mode == "display-list":
            self._diff_display_list(expected, actual)
        else:
            raise ValueError(f"Unknown mode: {self.mode}")

        return self.entries

    def summary(self) -> dict:
        total = self.checks
        critical = sum(1 for e in self.entries if e.severity == "CRITICAL")
        minor = sum(1 for e in self.entries if e.severity == "MINOR")
        passed = total - len(self.entries)

        by_cat: dict[str, int] = {}
        for e in self.entries:
            by_cat[e.category] = by_cat.get(e.category, 0) + 1

        score = (passed / total * 100) if total > 0 else 100.0

        return {
            "checks": total,
            "passed": passed,
            "critical": critical,
            "minor": minor,
            "by_category": by_cat,
            "score": round(score, 1),
        }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Structural JSON diff for browser test output")
    parser.add_argument("expected", help="Expected JSON file")
    parser.add_argument("actual", help="Actual JSON file")
    parser.add_argument("--mode", required=True,
                        choices=["dom", "css", "cascade", "layout", "display-list"],
                        help="Diff mode")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print all entries including passes")
    parser.add_argument("--json-out", type=str, default=None,
                        help="Write diff entries array to this JSON file")
    args = parser.parse_args()

    if not os.path.exists(args.expected):
        print(f"Error: expected file not found: {args.expected}", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(args.actual):
        print(f"Error: actual file not found: {args.actual}", file=sys.stderr)
        sys.exit(1)

    try:
        with open(args.expected, encoding="utf-8") as f:
            expected = json.load(f)
        with open(args.actual, encoding="utf-8") as f:
            actual = json.load(f)
    except json.JSONDecodeError as e:
        print(f"Error: invalid JSON: {e}", file=sys.stderr)
        sys.exit(1)

    differ = Differ(args.mode)
    entries = differ.diff(expected, actual)
    summary = differ.summary()

    # Print diff entries
    for entry in entries:
        print(str(entry))
        print()

    # Print summary table
    print("=" * 60)
    print(f"DIFF SUMMARY [{args.mode}]")
    print(f"  Checks: {summary['checks']}")
    print(f"  Passed: {summary['passed']}")
    print(f"  Critical: {summary['critical']}")
    print(f"  Minor: {summary['minor']}")
    print(f"  Score: {summary['score']}%")
    if summary["by_category"]:
        print("  By category:")
        for cat, cnt in sorted(summary["by_category"].items()):
            print(f"    {cat}: {cnt}")
    print("=" * 60)

    # Write JSON diff entries if requested
    if args.json_out:
        entries_json = [e.to_json() for e in entries]
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(entries_json, f, indent=2, ensure_ascii=False)

    if summary["critical"] > 0:
        sys.exit(1)
    elif summary["minor"] > 0:
        sys.exit(2)
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
