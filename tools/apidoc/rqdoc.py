#!/usr/bin/env python3
"""Reads the LuaLS annotations in rq.lua and writes the API reference.

  rqdoc.py json  rq.lua out.json      the model as JSON
  rqdoc.py md    rq.lua out_dir       Markdown: one page per namespace and per type
  rqdoc.py html  rq.lua out.html [ids.json]
                                      one offline page: every namespace and type, a search box,
                                      and the game's numbers from data/ids/ids.json when given
  rqdoc.py stats rq.lua               how much of the file uses the reference format
  rqdoc.py cover rq.lua apidump.json  compare with what the running engine exposes

Standard library only. The comment format is described in README.md next to this file.
"""
import html
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
    pat = re.compile(r"(?<![\w.])(" + "|".join(re.escape(n) for n in names) + r")(?![\w.])") if names else None
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

# ---------------------------------------------------------------- one HTML page, offline
#
# The same pages as `md`, in one file that needs no network (the release zip's xsystem4/api.html).
# The Markdown the pages are made of goes through a small converter for what they use: headings,
# tables, fenced code, quotes, lists, inline code, links, **bold** and *italic*. A link to another
# page becomes an anchor in this one: page X.md is #p-X, a function keeps its anchor().

def esc(s):
    return html.escape(s, quote=True)


def html_href(url):
    if re.match(r"[a-z]+:", url):
        return url
    page, _, frag = url.partition("#")
    if not page:
        return "#" + frag
    name = page.rsplit("/", 1)[-1]
    if name.endswith(".md"):
        return "#" + frag if frag else "#p-" + name[:-3]
    return url


def html_inline(text):
    slots = []

    def keep(s):
        slots.append(s)
        return "\x00%d\x00" % (len(slots) - 1)

    text = re.sub(r"\[((?:`[^`]*`|[^\]`])+)\]\(([^)\s]+)\)",
                  lambda m: keep('<a href="%s">%s</a>' % (esc(html_href(m.group(2))), html_inline(m.group(1)))), text)
    text = re.sub(r"(`+)(.+?)(?<!`)\1(?!`)", lambda m: keep("<code>%s</code>" % esc(m.group(2))), text)
    text = esc(text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\s](?:[^*]*[^*\s])?)\*(?![\w*])", r"<em>\1</em>", text)
    return re.sub("\x00(\\d+)\x00", lambda m: slots[int(m.group(1))], text)


LIST_RE = re.compile(r"(\s*)([-*]|\d+\.) (.*)$")


def html_list(lines, i, indent):
    ordered = bool(re.match(r"\s*\d+\. ", lines[i]))
    items = []
    while i < len(lines):
        m = LIST_RE.match(lines[i])
        if not m or len(m.group(1)) != indent:
            break
        text, sub = [m.group(3)], ""
        i += 1
        while i < len(lines) and lines[i].strip():
            m2 = LIST_RE.match(lines[i])
            if m2 and len(m2.group(1)) > indent:
                more, i = html_list(lines, i, len(m2.group(1)))
                sub += more
                continue
            if m2:
                break
            text.append(lines[i].strip())
            i += 1
        items.append("<li>%s%s</li>" % (html_inline(" ".join(text)), sub))
    tag = "ol" if ordered else "ul"
    return "<%s>%s</%s>" % (tag, "".join(items), tag), i


def table_cells(row):
    """A table row's cells; \\| inside a cell is a | of its text."""
    s = row.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|") and not s.endswith("\\|"):
        s = s[:-1]
    return [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", s)]


def html_blocks(lines, heading_attrs=lambda level, text: ""):
    """Markdown lines -> HTML. Headings move one level down (the page has its own h1)."""
    out, i, n = [], 0, len(lines)

    def starts_block(l):
        return (l.startswith("```") or l.startswith("|") or l.startswith("> ")
                or re.match(r"#{1,6} ", l) or LIST_RE.match(l))

    while i < n:
        l = lines[i]
        if not l.strip():
            i += 1
        elif l.startswith("```"):
            lang, body = l[3:].strip(), []
            i += 1
            while i < n and lines[i].strip() != "```":
                body.append(lines[i])
                i += 1
            i += 1
            out.append('<pre><code%s>%s</code></pre>' % (' class="lang-%s"' % esc(lang) if lang else "",
                                                         esc("\n".join(body))))
        elif re.match(r"#{1,6} ", l):
            level, text = len(l.split(" ", 1)[0]), l.split(" ", 1)[1].strip()
            out.append("<h%d%s>%s</h%d>" % (level + 1, heading_attrs(level, text), html_inline(text), level + 1))
            i += 1
        elif l.startswith("|"):
            rows = []
            while i < n and lines[i].startswith("|"):
                rows.append(table_cells(lines[i]))
                i += 1
            head, align = rows[0], []
            if len(rows) > 1 and all(re.match(r"^:?-+:?$", c) for c in rows[1]):
                align = ["right" if c.endswith(":") and not c.startswith(":") else "center" if c.startswith(":") and c.endswith(":") else ""
                         for c in rows[1]]
                rows = [head] + rows[2:]

            def td(tag, c, k):
                a = align[k] if k < len(align) else ""
                return "<%s%s>%s</%s>" % (tag, ' class="%s"' % a if a else "", html_inline(c), tag)
            body = "".join("<tr>%s</tr>" % "".join(td("td", c, k) for k, c in enumerate(r)) for r in rows[1:])
            out.append('<div class="table"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>'
                       % ("".join(td("th", c, k) for k, c in enumerate(head)), body))
        elif l.startswith("> "):
            quote = []
            while i < n and lines[i].startswith(">"):
                quote.append(lines[i][1:].strip())
                i += 1
            out.append("<blockquote>%s</blockquote>" % html_blocks(quote))
        elif LIST_RE.match(l):
            block, i = html_list(lines, i, len(LIST_RE.match(l).group(1)))
            out.append(block)
        else:
            para = []
            while i < n and lines[i].strip() and not (para and starts_block(lines[i])):
                para.append(lines[i].strip())
                i += 1
            out.append("<p>%s</p>" % html_inline(" ".join(para)))
    return "".join(out)


HTML_CSS = """
:root{--bg:#fbfaf7;--panel:#f2efe8;--text:#22201c;--muted:#6b665d;--line:#ddd7cb;--accent:#8a2f1d;
--code:#efebe2;--mark:#fff1c2;color-scheme:light}
@media (prefers-color-scheme:dark){:root{--bg:#17171a;--panel:#1f1f24;--text:#e6e3dc;--muted:#a19c92;
--line:#34343b;--accent:#f0a080;--code:#26262c;--mark:#4a3f1a;color-scheme:dark}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.55 system-ui,"Segoe UI",sans-serif}
a{color:var(--accent);text-decoration:none}a:hover{text-decoration:underline}
code,pre{font-family:ui-monospace,Consolas,"Cascadia Mono",monospace;font-size:13px}
code{background:var(--code);padding:1px 4px;border-radius:4px;overflow-wrap:break-word}
pre{background:var(--code);padding:10px 12px;border-radius:6px;overflow-x:auto}pre code{padding:0;background:none}
header{position:sticky;top:0;z-index:2;display:flex;gap:16px;align-items:center;flex-wrap:wrap;
padding:10px 16px;background:var(--panel);border-bottom:1px solid var(--line)}
header h1{font-size:17px;margin:0}header .ver{color:var(--muted);font-size:13px}
#q{flex:1;min-width:200px;max-width:480px;padding:7px 10px;border:1px solid var(--line);border-radius:6px;
background:var(--bg);color:var(--text);font:inherit}
#results{position:absolute;top:100%;left:16px;right:16px;max-width:760px;max-height:70vh;overflow:auto;
background:var(--panel);border:1px solid var(--line);border-radius:6px;box-shadow:0 6px 24px rgba(0,0,0,.25)}
#results a{display:block;padding:6px 10px;border-bottom:1px solid var(--line);color:var(--text)}
#results a span{display:block;color:var(--muted);font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#results a.sel,#results a:hover{background:var(--mark);text-decoration:none}
.layout{display:grid;grid-template-columns:270px minmax(0,1fr)}
nav{position:sticky;top:53px;height:calc(100vh - 53px);overflow:auto;padding:12px 16px;border-right:1px solid var(--line);font-size:14px}
nav h2{font-size:12px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);margin:16px 0 6px}
nav ul{list-style:none;margin:0;padding:0}nav li{margin:2px 0}nav summary{cursor:pointer;color:var(--muted)}
main{padding:8px 32px 64px;max-width:1100px}
section{border-bottom:1px solid var(--line);padding-bottom:24px}
section>h2{font-size:24px;margin:28px 0 8px}section>h2 .kind{font-size:13px;font-weight:normal;color:var(--muted);margin-left:8px}
h3{font-size:18px;margin:24px 0 8px}h4{font-size:16px;margin:28px 0 6px;padding-top:10px;border-top:1px dashed var(--line)}
:target{scroll-margin-top:64px}h4:target,section:target>h2,tr:target{background:var(--mark)}
.table{overflow-x:auto}table{border-collapse:collapse;margin:8px 0 14px;font-size:14px}
th,td{border:1px solid var(--line);padding:5px 9px;text-align:left;vertical-align:top}th{background:var(--panel)}
td.right,th.right{text-align:right}td.center,th.center{text-align:center}
blockquote{margin:8px 0;padding:2px 12px;border-left:3px solid var(--accent);color:var(--muted)}
@media (max-width:800px){.layout{display:block}nav{position:static;height:auto;max-height:45vh;border-right:0;border-bottom:1px solid var(--line)}
main{padding:8px 16px 48px}}
"""

HTML_JS = """
(function(){
var q=document.getElementById('q'),box=document.getElementById('results'),sel=0,shown=[];
var items=[].map.call(document.querySelectorAll('[data-s]'),function(e){
 return {id:e.id,s:e.getAttribute('data-s'),d:e.getAttribute('data-d')||''};});
items.forEach(function(it){it.l=it.s.toLowerCase();it.dl=it.d.toLowerCase();});
function go(it){location.hash=it.id;box.hidden=true;q.blur();}
function draw(){
 var v=q.value.trim().toLowerCase();box.innerHTML='';shown=[];sel=0;
 if(!v){box.hidden=true;return;}
 var terms=v.split(/\\s+/),hits=[];
 items.forEach(function(it){
  var sc;
  if(terms.every(function(t){return it.l.indexOf(t)>=0;}))
   sc=it.l===v?0:(it.l.slice(-v.length-1)==='.'+v||it.l.slice(-v.length-1)===':'+v)?1:it.l.indexOf(v)===0?2:3;
  else if(terms.every(function(t){return it.l.indexOf(t)>=0||it.dl.indexOf(t)>=0;}))sc=4;
  else return;
  hits.push([sc,it.s.length,it]);});
 hits.sort(function(a,b){return a[0]-b[0]||a[1]-b[1];});
 hits.slice(0,60).forEach(function(h,k){
  var a=document.createElement('a'),c=document.createElement('code'),s=document.createElement('span');
  a.href='#'+h[2].id;c.textContent=h[2].s;s.textContent=h[2].d;a.appendChild(c);a.appendChild(s);
  a.onclick=function(ev){ev.preventDefault();go(h[2]);};if(!k)a.className='sel';box.appendChild(a);shown.push(h[2]);});
 if(!hits.length){var p=document.createElement('a');p.textContent='Nothing matches.';box.appendChild(p);}
 box.hidden=false;}
q.addEventListener('input',draw);q.addEventListener('focus',function(){if(q.value)draw();});
q.addEventListener('keydown',function(e){
 if(e.key==='Enter'&&shown.length){go(shown[sel]);}
 else if(e.key==='Escape'){q.value='';box.hidden=true;}
 else if(e.key==='ArrowDown'||e.key==='ArrowUp'){if(!shown.length)return;e.preventDefault();
  box.children[sel].className='';sel=(sel+(e.key==='ArrowDown'?1:shown.length-1))%shown.length;
  box.children[sel].className='sel';box.children[sel].scrollIntoView({block:'nearest'});}});
document.addEventListener('keydown',function(e){if(e.key==='/'&&document.activeElement!==q){e.preventDefault();q.focus();}});
document.addEventListener('click',function(e){if(!box.contains(e.target)&&e.target!==q)box.hidden=true;});
})();
"""


def plain(s):
    """Markdown inline -> plain text, for the search's summaries."""
    s = re.sub(r"\[((?:`[^`]*`|[^\]`])+)\]\([^)\s]+\)", r"\1", s or "")
    return re.sub(r"`+|\*\*", "", s).replace("\n", " ")


def game_font(s):
    """The game writes é in its own font code; show the letter (as the docs' data pages do)."""
    return s.replace("･", "é")


def write_html(model, out, ids_path=None):
    spaces = sorted((c for c in model["classes"].values() if c["kind"] == "namespace"), key=lambda c: c["path"])
    types = sorted((c for c in model["classes"].values() if c["kind"] == "type"), key=lambda c: c["name"].lower())
    summaries = {f["full"]: f["summary"] for c in model["classes"].values() for f in c["functions"]}
    nfunc = len(summaries)

    def heading_attrs(level, text):
        if level == 3 and text in summaries:
            return ' id="%s" data-s="%s" data-d="%s"' % (anchor(text), esc(text), esc(plain(summaries[text])))
        return ""

    sections = []
    for c in spaces + types:
        here = "api" if c["kind"] == "namespace" else "types"
        title = c["path"] or c["name"]
        # md_class's items can hold several lines (a summary, details with a code block): split them
        body = html_blocks("\n".join(md_class(c, model, here)).split("\n")[2:], heading_attrs)
        sections.append('<section id="p-%s" data-s="%s" data-d="%s"><h2>%s<span class="kind">%s</span></h2>%s</section>'
                        % (esc(title), esc(title), esc(plain(c.get("summary"))), esc(title), c["kind"], body))

    contents = ["## Namespaces", "", "| Namespace | Summary |", "| --- | --- |"]
    contents += ["| [`%s`](%s) | %s |" % (c["path"], page_name(c), cell(c.get("summary"))) for c in spaces]
    contents += ["", "## Types", "", "| Type | Summary |", "| --- | --- |"]
    contents += ["| [`%s`](%s) | %s |" % (c["name"], page_name(c), cell(c.get("summary"))) for c in types]
    if model["aliases"]:
        contents += ["", "## Aliases", "", "| Alias | Type |", "| --- | --- |"]
        contents += ["| `%s` | %s |" % (a["name"], cell("`%s`" % a["type"])) for a in model["aliases"].values()]

    kinds = []
    if ids_path:
        with open(ids_path, encoding="utf-8") as f:
            kinds = json.load(f)["kinds"]
    data_sections, data_nav = [], []
    if kinds:
        contents += ["", "## Game data", "", "| Kind | Entries | What the number is |", "| --- | --- | --- |"]
    for k in kinds:
        kid = "data-" + k["kind"]
        contents.append("| [`rq.ids.%s`](#%s) | %d | %s |" % (k["kind"], kid, len(k["entries"]), cell(k["what"])))
        detail = any(e.get("detail") for e in k["entries"])
        rows = []
        for e in k["entries"]:
            keys = ", ".join("<code>%s</code>" % esc(x) for x in [e["key"]] + e.get("aliases", []))
            name = game_font(e["name"])
            rows.append('<tr id="d-%s-%d" data-s="rq.ids.%s.%s" data-d="%d: %s"><td class="right">%d</td><td>%s</td><td>%s</td>%s</tr>'
                        % (k["kind"], e["no"], k["kind"], esc(e["key"]), e["no"], esc(name), e["no"], keys, esc(name),
                           "<td>%s</td>" % esc(game_font(e.get("detail") or "")) if detail else ""))
        data_sections.append('<section id="%s" data-s="rq.ids.%s" data-d="%s"><h2>%s<span class="kind">game data</span></h2>'
                             '<p><code>rq.ids.%s</code>: %s. %d entries.</p><div class="table"><table><thead><tr>'
                             '<th class="right">Number</th><th>Key</th><th>Name</th>%s</tr></thead><tbody>%s</tbody></table></div></section>'
                             % (kid, k["kind"], esc(k["title"]), esc(k["title"]), k["kind"], html_inline(k["what"]),
                                len(k["entries"]), "<th>Detail</th>" if detail else "", "".join(rows)))
        data_nav.append('<li><a href="#%s">%s</a></li>' % (kid, esc(k["title"])))

    def nav_list(cs):
        return "".join('<li><a href="#p-%s">%s</a></li>' % (esc(c["path"] or c["name"]), esc(c["path"] or c["name"])) for c in cs)
    nav = ('<nav><h2>Namespaces</h2><ul>%s</ul><h2>Types</h2><details><summary>%d types</summary><ul>%s</ul></details>%s</nav>'
           % (nav_list(spaces), len(types), nav_list(types),
              "<h2>Game data</h2><ul>%s</ul>" % "".join(data_nav) if data_nav else ""))
    intro = ('<section id="contents"><h2>RQ MOD API reference<span class="kind">loader %s</span></h2>'
             '<p>The <code>rq.*</code> Lua API of RQ MOD %s, made from <code>lua/rq.lua</code> (the same file your editor reads). '
             'The guides and this reference online: <a href="https://rq-modding.github.io/rq-modding-docs/">rq-modding.github.io/rq-modding-docs</a>. '
             'Search with the box above, or press <kbd>/</kbd>.</p>%s</section>'
             % (esc(model["version"] or "?"), esc(model["version"] or "?"), html_blocks(contents)))
    page = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width, initial-scale=1">'
            '<title>RQ MOD API %s</title><style>%s</style></head><body>'
            '<header><h1>RQ MOD API</h1><span class="ver">loader %s</span>'
            '<input id="q" type="search" placeholder="Search: rq.hud.text, on_hit, KAORU..." autocomplete="off">'
            '<div id="results" hidden></div></header>'
            '<div class="layout">%s<main>%s%s%s</main></div><script>%s</script></body></html>\n'
            % (esc(model["version"] or ""), HTML_CSS, esc(model["version"] or "?"), nav, intro,
               "".join(sections), "".join(data_sections), HTML_JS))
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    return len(spaces), len(types), nfunc, len(kinds)

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
    elif cmd == "html":
        n, t, f, k = write_html(model, argv[3], argv[4] if len(argv) > 4 else None)
        print("wrote %s: %d namespaces, %d types, %d functions and events, %d kinds of game data, %d KB"
              % (argv[3], n, t, f, k, os.path.getsize(argv[3]) // 1024))
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
