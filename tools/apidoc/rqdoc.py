#!/usr/bin/env python3
"""Reads the LuaLS annotations in rq.lua and writes the API reference.

  rqdoc.py json  rq.lua out.json      the model as JSON
  rqdoc.py md    rq.lua out_dir       Markdown: one page per namespace and per type
  rqdoc.py stats rq.lua               how much of the file uses the reference format
  rqdoc.py cover rq.lua apidump.json  compare with what the running engine exposes

Standard library only. The comment format is described in README.md next to this file.
"""
import json
import os
import re
import sys

# ---------------------------------------------------------------- reading

def read_type(s):
    """Splits 'TYPE rest' where TYPE may hold spaces inside brackets, unions and fun(...): ret."""
    s = s.strip()
    i, depth, n = 0, 0, len(s)
    while i < n:
        c = s[i]
        if c in "([{<":
            depth += 1
        elif c in ")]}>":
            depth -= 1
        elif c == '"':
            j = s.find('"', i + 1)
            i = j if j >= 0 else n
        elif c.isspace() and depth == 0:
            rest = s[i:].lstrip()
            # keep going for "fun(...): ret", "a | b" and "a, b" inside generics
            if rest.startswith("|") or s[:i].endswith("|") or s[:i].endswith(":") or rest.startswith(":"):
                i += 1
                continue
            return s[:i].strip(), rest
        i += 1
    return s.strip(), ""


MARKER_RE = re.compile(r"^(Since|Changed):\s*(\d+\.\d+\.\d+)\s*(.*)$")
LEGACY_RE = re.compile(r"^Loader (\d+\.\d+\.\d+)[.:]?\s*")


def parse_desc(lines):
    """Description lines -> summary, details, since, changes, legacy flag."""
    text_lines, since, changes, fence = [], None, [], False
    for ln in lines:
        if ln.strip().startswith("```"):
            fence = not fence
        m = None if fence else MARKER_RE.match(ln.strip())
        if m and m.group(1) == "Since":
            since = m.group(2)
        elif m:
            changes.append({"version": m.group(2), "text": m.group(3)})
        else:
            text_lines.append(ln)
    text = re.sub(r"\n\s*\n(\s*\n)+", "\n\n", "\n".join(text_lines)).strip()
    legacy = False
    m = LEGACY_RE.match(text)
    if m:
        legacy = True
        since = since or m.group(1)
        text = text[m.end():]
    paras = re.split(r"\n\s*\n", text, maxsplit=1) if text else [""]
    summary = paras[0].strip()
    details = paras[1].strip() if len(paras) > 1 else ""
    # an old-style entry is one long paragraph; its first sentence is the summary
    if legacy and not details and ". " in summary:
        head, tail = summary.split(". ", 1)
        summary, details = head + ".", tail
    return {"summary": summary, "details": details, "since": since,
            "changes": changes, "legacy": legacy}


def parse_tag(line):
    tag, _, rest = line[1:].partition(" ")
    rest = rest.strip()
    if tag in ("param", "field"):
        name, rest = (rest.split(None, 1) + [""])[:2]
        optional = name.endswith("?")
        typ, desc = read_type(rest)
        if typ.endswith("?"):
            optional, typ = True, typ[:-1]
        return tag, {"name": name.rstrip("?"), "type": typ, "optional": optional, "desc": desc}
    if tag == "return":
        typ, rest = read_type(rest)
        name, desc = "", rest
        m = re.match(r"([A-Za-z_][\w]*)(?:\s+(.*))?$", rest)
        if m and (m.group(2) or not rest.endswith(".")):
            name, desc = m.group(1), m.group(2) or ""
        return tag, {"type": typ, "name": name, "desc": desc}
    if tag in ("class", "alias", "type", "overload", "see"):
        return tag, rest
    return tag, rest


def blocks(path):
    """Yields (comment_lines, code_line) pairs. code_line is None for a standalone block."""
    cur, seen_tag = [], False
    for raw in open(path, encoding="utf-8"):
        line = raw.rstrip("\n")
        if line.startswith("---"):
            body = line[3:]
            if body.startswith("@meta"):
                continue
            is_tag = body.startswith("@")
            if not is_tag and seen_tag:          # new description after tags: a new entry
                yield cur, "<split>"
                cur, seen_tag = [], False
            cur.append(body)
            seen_tag = seen_tag or is_tag
            continue
        if cur:
            yield cur, (line.strip() or None)
        cur, seen_tag = [], False
    if cur:
        yield cur, None


def merged_blocks(path):
    """Joins a class block with description lines written after its tags, when they are
    followed by the class's table (\"---@class rq.scene / ---text / local scene = {}\")."""
    items = list(blocks(path))
    i = 0
    while i < len(items):
        comment, code = items[i]
        if code == "<split>" and i + 1 < len(items):
            nxt_comment, nxt_code = items[i + 1]
            has_class = any(l.startswith("@class") for l in comment)
            if has_class and not any(l.startswith("@") for l in nxt_comment) and nxt_code and \
                    re.match(r"(local \w+|\w+\.\w+)\s*=\s*\{\}", nxt_code):
                tags = [l for l in comment if l.startswith("@")]
                desc = [l for l in comment if not l.startswith("@")]
                yield desc + ([""] if desc else []) + nxt_comment + tags, nxt_code
                i += 2
                continue
        yield comment, (None if code == "<split>" else code)
        i += 1


def parse(path):
    model = {"version": None, "classes": {}, "aliases": {}, "functions": [], "bindings": {}}
    var_class = {}          # local var -> class name
    classes = model["classes"]
    pending_fields = []     # (owner var, field name, entry) for "var.name = ..." with a block

    def new_class(name, desc, fields, overloads):
        c = classes.setdefault(name, {"name": name, "fields": [], "overloads": []})
        c.update(desc)
        c["fields"] += fields
        c["overloads"] += overloads
        return c

    for comment, code in merged_blocks(path):
        desc_lines = [l for l in comment if not l.startswith("@")]
        tags = [parse_tag(l) for l in comment if l.startswith("@")]
        desc = parse_desc(desc_lines)
        cls = next((v for t, v in tags if t == "class"), None)
        alias = next((v for t, v in tags if t == "alias"), None)
        if alias:
            name, typ = (alias.split(None, 1) + [""])[:2]
            model["aliases"][name] = dict(desc, name=name, type=typ)
            continue
        if cls:
            cname = cls.split(":")[0].strip()
            c = new_class(cname, desc, [v for t, v in tags if t == "field"],
                          [v for t, v in tags if t == "overload"])
            if code:
                m = re.match(r"local (\w+)\s*=\s*\{\}", code)
                if m:
                    var_class[m.group(1)] = cname
                m = re.match(r"(\w+)\.(\w+)\s*=\s*\{\}", code)
                if m:
                    c["parent_var"], c["key"] = m.group(1), m.group(2)
            continue
        if not code:
            continue
        m = re.match(r"function ([\w]+)([.:])(\w+)\((.*?)\)", code)
        if m:
            owner, sep, name, args = m.groups()
            model["functions"].append(dict(desc, owner_var=owner, sep=sep, name=name,
                args=[a.strip() for a in args.split(",") if a.strip()],
                params=[v for t, v in tags if t == "param"],
                returns=[v for t, v in tags if t == "return"],
                see=[v for t, v in tags if t == "see"],
                overloads=[v for t, v in tags if t == "overload"]))
            continue
        m = re.match(r"(\w+)\.(\w+)\s*=\s*(.+)$", code)
        if m:
            typ = next((v for t, v in tags if t == "type"), "")
            if m.group(1) == "rq" and m.group(2) == "version":
                model["version"] = m.group(3).strip().strip('"')
            pending_fields.append((m.group(1), m.group(2), dict(desc, name=m.group(2), type=typ,
                                                                optional=False, desc=desc["summary"])))

    # bindings rq.NAME = var, read from plain code lines
    for raw in open(path, encoding="utf-8"):
        m = re.match(r"rq\.(\w+)\s*=\s*(\w+)\s*$", raw.strip())
        if m and m.group(2) in var_class:
            model["bindings"][m.group(2)] = "rq." + m.group(1)
    var_class["rq"] = "rq"
    model["bindings"]["rq"] = "rq"

    # where each class lives in the API: rq.speed, rq.store.global, or nowhere (a data type)
    path_of = {}
    for var, cname in var_class.items():
        if var in model["bindings"]:
            path_of[cname] = model["bindings"][var]
    for c in classes.values():
        if "parent_var" in c and c["parent_var"] in var_class:
            parent = path_of.get(var_class[c["parent_var"]], var_class[c["parent_var"]])
            path_of[c["name"]] = parent + "." + c["key"]
    for c in classes.values():
        c["path"] = path_of.get(c["name"])
        c["kind"] = "namespace" if c["path"] else "type"
        c["functions"] = []

    for owner_var, key, entry in pending_fields:
        cname = var_class.get(owner_var)
        if cname and not any(f["name"] == key for f in classes[cname]["fields"]):
            classes[cname]["fields"].append(entry)
        elif cname:
            for f in classes[cname]["fields"]:
                if f["name"] == key and not f.get("desc"):
                    f["desc"] = entry["desc"]

    for f in model["functions"]:
        cname = var_class.get(f["owner_var"], f["owner_var"])
        c = classes.get(cname)
        base = (c and c["path"]) or cname
        f["class"] = cname
        f["full"] = base + f["sep"] + f["name"]
        f["event"] = f["name"].startswith("on_")
        if c:
            c["functions"].append(f)
    model["functions"] = [f["full"] for f in model["functions"]]
    return model

# ---------------------------------------------------------------- Markdown

def page_name(c):
    return (c["path"] or c["name"]) + ".md"


def link_types(typ, model, here):
    """Type text -> Markdown with links to type pages; safe inside table cells."""
    if not typ:
        return ""
    names = sorted(model["classes"].keys() | model["aliases"].keys(), key=len, reverse=True)
    pat = re.compile(r"(?<![\w.])(" + "|".join(re.escape(n) for n in names) + r")(?![\w])") if names else None
    out, pos = [], 0
    for m in (pat.finditer(typ) if pat else []):
        if m.start() > pos:
            out.append(("code", typ[pos:m.start()]))
        out.append(("link", m.group(1)))
        pos = m.end()
    if pos < len(typ):
        out.append(("code", typ[pos:]))
    res = []
    for kind, text in out:
        if kind == "link":
            target = model["classes"].get(text)
            if target:
                rel = os.path.relpath(os.path.join("types" if target["kind"] == "type" else "api",
                                                   page_name(target)), here).replace("\\", "/")
                res.append("[`%s`](%s)" % (text, rel))
            else:
                res.append("`%s`" % text)
        elif text.strip():
            res.append("`%s`" % text.strip())
    return " ".join(res).replace("|", "\\|")


def anchor(full):
    """The heading anchor GitHub and the docs site both make for a function's full name."""
    return re.sub(r"[^a-z0-9_-]", "", full.lower().replace(".", "").replace(":", ""))


def ref_link(name, model, here):
    """`rq.x.y` -> a link to that namespace, type or function; plain code when it is unknown."""
    target = model["classes"].get(name) or next(
        (c for c in model["classes"].values() if c["path"] == name), None)
    if target:
        kind = "types" if target["kind"] == "type" else "api"
        rel = os.path.relpath(os.path.join(kind, page_name(target)), here).replace("\\", "/")
        return "[`%s`](%s)" % (name, rel)
    for c in model["classes"].values():
        for f in c["functions"]:
            if f["full"] == name:
                kind = "types" if c["kind"] == "type" else "api"
                rel = os.path.relpath(os.path.join(kind, page_name(c)), here).replace("\\", "/")
                return "[`%s`](%s#%s)" % (name, rel, anchor(name))
    return "`%s`" % name


def cell(text):
    return (text or "").replace("|", "\\|").replace("\n", " ")


def badges(e):
    out = []
    if e.get("since"):
        out.append("`since %s`" % e["since"])
    for ch in e.get("changes", []):
        out.append("`changed %s`" % ch["version"])
    return " ".join(out)


def md_function(f, model, here):
    sig = "%s(%s)" % (f["full"], ", ".join(f["args"]))
    rets = ", ".join(r["type"] for r in f["returns"])
    lines = ["### %s" % f["full"], "", "```lua", sig + (" -> " + rets if rets else ""), "```", ""]
    b = badges(f)
    if b:
        lines += [b, ""]
    if f["summary"]:
        lines += [f["summary"], ""]
    if f["details"]:
        lines += [f["details"], ""]
    if f["params"]:
        lines += ["| Parameter | Type | Description |", "| --- | --- | --- |"]
        for p in f["params"]:
            name = "`%s`" % p["name"] + (" *(optional)*" if p["optional"] else "")
            lines.append("| %s | %s | %s |" % (name, link_types(p["type"], model, here), cell(p["desc"])))
        lines.append("")
    if f["returns"]:
        lines += ["| Returns | Type | Description |", "| --- | --- | --- |"]
        for r in f["returns"]:
            lines.append("| %s | %s | %s |" % ("`%s`" % r["name"] if r["name"] else "", link_types(r["type"], model, here), cell(r["desc"])))
        lines.append("")
    for ch in f.get("changes", []):
        lines += ["> Changed in %s: %s" % (ch["version"], ch["text"]), ""]
    if f["see"]:
        lines += ["See also: " + ", ".join(ref_link(s, model, here) for s in f["see"]), ""]
    return lines


def md_class(c, model, here):
    title = c["path"] or c["name"]
    lines = ["# " + title, ""]
    b = badges(c)
    if b:
        lines += [b, ""]
    for part in ("summary", "details"):
        if c.get(part):
            lines += [c[part], ""]
    funcs = [f for f in c["functions"] if not f["event"]]
    events = [f for f in c["functions"] if f["event"]]
    fnames = {f["name"] for f in c["functions"]}
    fields = [f for f in c["fields"] if model["classes"].get(f["type"], {}).get("kind") != "namespace"
              and f["name"] not in fnames]
    subs = [s for s in model["classes"].values() if s["kind"] == "namespace" and c["path"]
            and s["path"].rsplit(".", 1)[0] == c["path"]]
    if subs:
        lines += ["## Namespaces", "", "| Namespace | Summary |", "| --- | --- |"]
        for s in sorted(subs, key=lambda s: s["path"]):
            lines.append("| [`%s`](%s) | %s |" % (s["path"], page_name(s), cell(s.get("summary"))))
        lines.append("")
    if fields:
        lines += ["## Fields" if c["kind"] == "type" else "## Properties", "",
                  "| Name | Type | Description |", "| --- | --- | --- |"]
        for f in fields:
            name = "`%s`" % f["name"] + (" *(optional)*" if f["optional"] else "")
            lines.append("| %s | %s | %s |" % (name, link_types(f["type"], model, here), cell(f["desc"])))
        lines.append("")
    for heading, group in (("Functions", funcs), ("Events", events)):
        if not group:
            continue
        lines += ["## " + heading, "", "| Name | Summary |", "| --- | --- |"]
        for f in group:
            lines.append("| [`%s`](#%s) | %s |" % (f["full"], anchor(f["full"]), cell(f["summary"])))
        lines.append("")
        for f in group:
            lines += md_function(f, model, here)
    used = used_by(c["name"], model) if c["kind"] == "type" else []
    if used:
        lines += ["## Used by", "", ", ".join(ref_link(u, model, here) for u in used), ""]
    return lines


def used_by(name, model):
    pat = re.compile(r"(?<![\w.])" + re.escape(name) + r"(?![\w])")
    out = []
    for c in model["classes"].values():
        for f in c["functions"]:
            if any(pat.search(p["type"]) for p in f["params"]) or any(pat.search(r["type"]) for r in f["returns"]):
                out.append(f["full"])
        if c["name"] != name and any(pat.search(f["type"]) for f in c["fields"]):
            out.append(c["path"] or c["name"])
    return sorted(set(out))


def write_md(model, out):
    os.makedirs(os.path.join(out, "api"), exist_ok=True)
    os.makedirs(os.path.join(out, "types"), exist_ok=True)
    spaces = sorted((c for c in model["classes"].values() if c["kind"] == "namespace"), key=lambda c: c["path"])
    types = sorted((c for c in model["classes"].values() if c["kind"] == "type"), key=lambda c: c["name"].lower())
    for c in spaces + types:
        here = "api" if c["kind"] == "namespace" else "types"
        with open(os.path.join(out, here, page_name(c)), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(md_class(c, model, here)).rstrip() + "\n")
    idx = ["# API reference", "", "Loader version %s." % model["version"], "",
           "## Namespaces", "", "| Namespace | Summary |", "| --- | --- |"]
    idx += ["| [`%s`](api/%s) | %s |" % (c["path"], page_name(c), cell(c.get("summary"))) for c in spaces]
    idx += ["", "## Types", "", "| Type | Summary |", "| --- | --- |"]
    idx += ["| [`%s`](types/%s) | %s |" % (c["name"], page_name(c), cell(c.get("summary"))) for c in types]
    if model["aliases"]:
        idx += ["", "## Aliases", "", "| Alias | Type |", "| --- | --- |"]
        idx += ["| `%s` | %s |" % (a["name"], cell("`%s`" % a["type"])) for a in model["aliases"].values()]
    with open(os.path.join(out, "index.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(idx) + "\n")
    return len(spaces), len(types)

# ---------------------------------------------------------------- stats and coverage

def entries(model):
    for c in model["classes"].values():
        yield c["path"] or c["name"], c
        for f in c["functions"]:
            yield f["full"], f


def stats(model):
    rows, total = {}, {"entries": 0, "new": 0, "legacy": 0, "no_since": 0, "no_summary": 0, "params_no_desc": 0}
    for name, e in entries(model):
        total["entries"] += 1
        c = model["classes"].get(e.get("class") or e.get("name"))
        ns = (c["path"] or c["name"]) if c else name
        r = rows.setdefault(ns, {"entries": 0, "new": 0})
        r["entries"] += 1
        if e.get("legacy"):
            total["legacy"] += 1
        elif e.get("since"):
            total["new"] += 1
            r["new"] += 1
        else:
            total["no_since"] += 1
        if not e.get("summary"):
            total["no_summary"] += 1
        total["params_no_desc"] += sum(1 for p in e.get("params", []) if not p["desc"])
    print("entries %(entries)d: reference format %(new)d, old 'Loader X' prefix %(legacy)d, "
          "no version %(no_since)d, no summary %(no_summary)d, params without description %(params_no_desc)d" % total)
    for ns, r in sorted(rows.items(), key=lambda kv: -kv[1]["entries"]):
        if r["entries"] > 1:
            print("  %-22s %3d entries, %3d done" % (ns, r["entries"], r["new"]))


def documented_paths(model):
    """Every rq.* path the annotations promise exists in the running engine."""
    out = {}
    for c in model["classes"].values():
        if not c["path"]:
            continue
        out[c["path"]] = "table"
        for f in c["functions"]:
            if f["sep"] == ".":
                out[f["full"]] = "function"
        for f in c["fields"]:
            sub = model["classes"].get(f["type"])
            out[c["path"] + "." + f["name"]] = "table" if sub else ("function" if f["type"].startswith("fun(") else "value")
    return out


def cover(model, dump_path):
    dump = json.load(open(dump_path, encoding="utf-8"))
    docs = documented_paths(model)
    # inside a documented data field (a theme, a lookup table) the keys are data, not API
    opaque = [n for n, k in docs.items() if k == "value" or (k == "table" and n not in
              {c["path"] for c in model["classes"].values() if c["path"]})]
    def private(n):
        return any(part.startswith("_") for part in n.split("."))
    live = {e["name"]: e["kind"] for e in dump["entries"]
            if not private(e["name"]) and not any(e["name"].startswith(o + ".") for o in opaque)}
    docs.pop("rq", None)
    undocumented = sorted(n for n in live if n not in docs)
    stale = sorted(n for n in docs if n not in live)
    print("engine %s exposes %d entries, rq.lua documents %d" % (dump.get("version"), len(live), len(docs)))
    print("undocumented (in the engine, not in rq.lua): %d" % len(undocumented))
    for n in undocumented:
        print("  + %s (%s)" % (n, live[n]))
    print("stale (in rq.lua, not in the engine): %d" % len(stale))
    for n in stale:
        print("  - %s" % n)
    return 1 if undocumented or stale else 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, src = argv[1], argv[2]
    model = parse(src)
    if cmd == "json":
        with open(argv[3], "w", encoding="utf-8", newline="\n") as f:
            json.dump(model, f, ensure_ascii=False, indent=1)
    elif cmd == "md":
        n, t = write_md(model, argv[3])
        print("wrote %d namespace pages and %d type pages to %s" % (n, t, argv[3]))
    elif cmd == "stats":
        stats(model)
    elif cmd == "cover":
        return cover(model, argv[3])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
