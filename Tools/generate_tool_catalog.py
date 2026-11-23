# -*- coding: utf-8 -*-
"""Docs/API.md 를 파싱해 툴별 JSON Schema 를 생성한다.
InvokeTool() 디스패치 목록과 대조해 커버리지를 보고한다.
"""
import io, json, re, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER_CPP = os.path.join(ROOT, r"Source\LLMBlueprintBridge\Private\LLMBlueprintBridgeServer.cpp")
API_MD = os.path.join(ROOT, r"Docs\API.md")


def read(p):
    return io.open(p, encoding="utf-8").read()


# ---------- 1. InvokeTool() 디스패치에서 실제 툴 이름 추출 ----------
def dispatch_tool_names():
    src = read(SERVER_CPP)
    start = src.find("FLLMBlueprintBridgeServer::InvokeTool")
    if start < 0:
        raise SystemExit("InvokeTool not found")
    body = src[start:]
    names = re.findall(r'ToolName\s*==\s*TEXT\("([A-Za-z0-9_.-]+)"\)', body)
    seen, out = set(), []
    for n in names:
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


# ---------- 2. API.md 에서 `name({args})->returns` 항목 추출 ----------
def split_top_level(s, sep=","):
    """중첩 괄호/따옴표를 존중하며 최상위 구분자로 분리."""
    parts, depth, buf, quote = [], 0, [], None
    for ch in s:
        if quote:
            buf.append(ch)
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
            buf.append(ch)
            continue
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
    if buf:
        parts.append("".join(buf))
    return [p.strip() for p in parts if p.strip()]


def match_brace(s, i):
    """s[i] == '{' 일 때 대응하는 '}' 인덱스 반환."""
    depth, quote = 0, None
    while i < len(s):
        ch = s[i]
        if quote:
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


# `tool_a|tool_b|suffix({args})` 그룹 표기를 모두 잡는다.
ENTRY_RE = re.compile(r"`([a-z0-9_]+(?:\|[a-z0-9_]+)*)\(\{")
# `tool_name(selector)` 표기
SELECTOR_RE = re.compile(r"`([a-z0-9_]+(?:\|[a-z0-9_]+)*)\(selector\)")

# Docs/API.md line 95: 디버그 툴 공용 셀렉터
SELECTOR_PROPS = {
    "selected_actor": {"type": "boolean", "description": "select the currently selected editor actor"},
    "actor_path": {"type": "string"},
    "actor_name": {"type": "string", "description": "aliases: actor"},
    "actor": {"type": "string", "description": "alias of actor_name"},
    "actor_class": {"type": "string"},
    "component_path": {"type": "string"},
}


def expand_group(group, known):
    """`a|b|suffix` 를 실제 툴 이름 목록으로 펼친다."""
    tokens = [t for t in group.split("|") if t]
    if not tokens:
        return []
    out, first = [], tokens[0]
    for tok in tokens:
        if tok in known:
            out.append(tok)
            continue
        # 접두사 공유 축약형: inspect_statetree_transitions|tasks -> inspect_statetree_tasks
        if "_" in first:
            prefix = first[: first.rfind("_") + 1]
            cand = prefix + tok
            if cand in known:
                out.append(cand)
                continue
        out.append(tok)  # 미지의 이름도 남겨서 리포트에 드러나게 한다
    seen, uniq = set(), []
    for n in out:
        if n not in seen:
            seen.add(n)
            uniq.append(n)
    return uniq


def api_entries(known):
    text = read(API_MD)
    entries = {}

    # selector 전용 표기 먼저 등록
    for m in SELECTOR_RE.finditer(text):
        desc = re.sub(r"\s+", " ", text[m.start() + 1: m.start() + 200]).strip()
        desc = desc.split("`")[0].strip(" .;") + "."
        for name in expand_group(m.group(1), known):
            entries.setdefault(name, {"args_src": "__SELECTOR__", "description": desc})

    for m in ENTRY_RE.finditer(text):
        group = m.group(1)
        brace_open = m.end() - 1          # '{' 위치
        brace_close = match_brace(text, brace_open)
        if brace_close < 0:
            continue
        args_src = text[brace_open + 1:brace_close]
        # 항목 설명: 여는 백틱부터 문장 끝(다음 백틱 뒤 마침표)까지
        tick_start = m.start()
        tick_end = text.find("`", brace_close)
        if tick_end < 0:
            continue
        # 설명 꼬리(세미콜론 뒤 부연) 흡수: 다음 백틱 항목 직전 혹은 문단 끝까지
        tail_stop = len(text)
        nxt = ENTRY_RE.search(text, tick_end)
        if nxt:
            tail_stop = nxt.start()
        tail = text[tick_end + 1:tail_stop]
        # 첫 문장 몇 개만 (다음 인라인 코드 시작 전까지 또는 개행까지)
        tail = tail.split("\n")[0]
        desc = text[tick_start + 1:tick_end] + tail
        desc = re.sub(r"\s+", " ", desc).strip()
        desc = desc.strip(" .;") + "."
        for name in expand_group(group, known):
            if name not in entries or entries[name]["args_src"] == "__SELECTOR__":
                entries[name] = {"args_src": args_src, "description": desc}
    return entries


# ---------- 3. 인자 → JSON Schema ----------
BOOL_PREFIX = ("include_", "is_", "has_", "allow_", "omit_", "use_", "enable_", "with_")
BOOL_EXACT = {
    "confirm", "recursive", "load_assets", "force", "dry_run", "call_in_editor",
    "saved", "blueprint_callable", "blueprint_pure", "static", "loaded",
    "enabled", "local_space", "always_reset_on_entry",
}
INT_EXACT = {
    "limit", "x", "y", "offset", "count", "port", "page", "index",
    "origin_x", "origin_y", "column_spacing", "row_spacing", "priority", "priority_order",
}
INT_SUFFIX = ("_count", "_limit", "_index", "_spacing", "_x", "_y")
NUM_EXACT = {"time", "duration", "start_time", "end_time", "length", "weight", "magnitude", "scale_value", "crossfade_duration"}
ARRAY_EXACT = {
    "exclude_paths", "excluded_paths", "exclude_path_prefixes", "excluded_path_prefixes",
    "graphs", "inputs", "outputs", "keys", "rows", "row_names", "tags", "modifiers",
}
# 타입을 강제하지 않을 이름 (임의 JSON 허용)
ANY_EXACT = {"value", "default", "arguments", "payload", "data", "properties", "bindings"}
OBJECT_EXACT = {"defaults", "values", "row_values", "params", "options", "overrides"}

# ---------- 수동 보정 ----------
# 자동 추론이 틀리는 지점만 손으로 잡는다. 키는 툴 이름.
#   drop_required : 문서상 "둘 중 하나" 인자를 required 에서 뺀다
#   enum          : 문서에 열거된 값 집합
#   description   : 설명 교체
OVERRIDES = {
    "find_functions": {"drop_required": ["query", "class_path"]},
    "get_property": {"drop_required": ["property"]},
    "set_property": {"drop_required": ["property", "value"]},
    "spawn_node": {
        "enum": {"node_type": ["branch", "call_function", "variable_get", "variable_set", "custom_event"]}
    },
    "format_blueprint_graph": {
        "enum": {"strategy": ["control_flow_parameters", "dependency_layered"]},
        "description": "DEPRECATED: automatic node placements are unreadable for humans. Prefer manually chosen NodePosX/NodePosY with set_property. Both control_flow_parameters and dependency_layered require allow_deprecated_automatic_layout=true for legacy execution."
    },
    "generate_attribute_set_cpp": {
        "description": "generate_attribute_set_cpp({...}) - guarded, reports unsupported unless a future generator is enabled."
    },
}


def apply_overrides(name, schema, description):
    ov = OVERRIDES.get(name)
    if not ov:
        return schema, description
    for prop in ov.get("drop_required", []):
        if prop in schema.get("required", []):
            schema["required"].remove(prop)
    if not schema.get("required"):
        schema.pop("required", None)
    for prop, values in ov.get("enum", {}).items():
        if prop in schema.get("properties", {}):
            schema["properties"][prop]["enum"] = values
    return schema, ov.get("description", description)


def infer_type(name, default_src, marker):
    if marker == "array":
        return "array"
    if marker == "object":
        return "object"
    if default_src is not None:
        d = default_src.strip()
        if d in ("true", "false"):
            return "boolean"
        if d.startswith("[") :
            return "array"
        if d.startswith("{"):
            return "object"
        if re.fullmatch(r"-?\d+", d):
            return "integer"
        if re.fullmatch(r"-?\d*\.\d+", d):
            return "number"
        return "string"
    if name in ANY_EXACT:
        return None
    if name in OBJECT_EXACT:
        return "object"
    if name in ARRAY_EXACT:
        return "array"
    if name in BOOL_EXACT or name.startswith(BOOL_PREFIX):
        return "boolean"
    if name in INT_EXACT or name.endswith(INT_SUFFIX):
        return "integer"
    if name in NUM_EXACT:
        return "number"
    return "string"


def parse_enum_default(default_src):
    """`"static"|"actor"` 처럼 기본값 자리에 열거형이 오는 표기를 잡는다.

    반환: (허용값 목록, 첫 값) 또는 (None, None)
    """
    if default_src is None:
        return None, None
    d = default_src.strip()
    if "|" not in d:
        return None, None
    values = []
    for part in d.split("|"):
        part = part.strip()
        if len(part) >= 2 and part[0] == part[-1] and part[0] in "\"'":
            part = part[1:-1]
        if not part:
            return None, None
        values.append(part)
    return values, values[0]


def parse_default(default_src, jtype):
    if default_src is None:
        return None
    d = default_src.strip()
    if d in ("true", "false"):
        return d == "true"
    if d == "[]":
        return []
    if d == "{}":
        return {}
    if re.fullmatch(r"-?\d+", d):
        return int(d)
    if re.fullmatch(r"-?\d*\.\d+", d):
        return float(d)
    if len(d) >= 2 and d[0] == d[-1] and d[0] in "\"'":
        return d[1:-1]
    return None


def build_schema(args_src):
    props, required, notes = {}, [], []
    if args_src == "__SELECTOR__":
        return {"type": "object", "properties": dict(SELECTOR_PROPS)}, notes
    for raw in split_top_level(args_src):
        entry = raw.strip()
        if not entry:
            continue
        marker = None
        default_src = None

        # name:[{...}] / name:{...} / name{...} / name?{} 형태
        mcolon = re.match(r"^([A-Za-z0-9_|?]+)\s*:\s*(.+)$", entry, re.S)
        meq = re.match(r"^([A-Za-z0-9_|?]+)\s*=\s*(.+)$", entry, re.S)
        mbrace = re.match(r"^([A-Za-z0-9_|?]+)\s*([\[{].*)$", entry, re.S)
        if meq:
            head, default_src = meq.group(1), meq.group(2)
        elif mcolon:
            head, rest = mcolon.group(1), mcolon.group(2).strip()
            marker = "array" if rest.startswith("[") else "object" if rest.startswith("{") else None
        elif mbrace:
            head, rest = mbrace.group(1), mbrace.group(2).strip()
            marker = "array" if rest.startswith("[") else "object"
        else:
            head = entry

        optional_mark = head.endswith("?")
        head = head.rstrip("?")
        aliases = [a for a in head.split("|") if a]
        if not aliases:
            continue
        name = aliases[0]
        if not re.fullmatch(r"[A-Za-z0-9_]+", name):
            continue

        if name == "selector":
            props.update(SELECTOR_PROPS)
            continue

        jtype = infer_type(name, default_src, marker)
        enum_values, enum_default = parse_enum_default(default_src)
        prop = {}
        if enum_values:
            prop["type"] = "string"
            prop["enum"] = enum_values
            prop["default"] = enum_default
        else:
            if jtype:
                prop["type"] = jtype
                if jtype == "array":
                    # Strict MCP clients reject array schemas that do not declare items.
                    prop["items"] = {}
            dv = parse_default(default_src, jtype)
            if dv is not None:
                prop["default"] = dv

        desc_bits = []
        if len(aliases) > 1:
            desc_bits.append("aliases: " + ", ".join(aliases[1:]))
        if desc_bits:
            prop["description"] = "; ".join(desc_bits)
        props[name] = prop

        # 별칭도 노출한다. 단 타입은 별칭 이름으로 다시 추론한다
        # (`graph|graphs` 처럼 단수/복수 별칭은 타입이 서로 다르다).
        for alt in aliases[1:]:
            if re.fullmatch(r"[A-Za-z0-9_]+", alt) and alt not in props:
                alt_prop = dict(prop)
                alt_type = infer_type(alt, default_src, marker)
                if alt_type:
                    alt_prop["type"] = alt_type
                elif "type" in alt_prop:
                    del alt_prop["type"]
                if alt_prop.get("type") == "array":
                    alt_prop["items"] = {}
                else:
                    alt_prop.pop("items", None)
                alt_prop["description"] = "alias of " + name
                props[alt] = alt_prop

        is_required = (default_src is None and marker is None
                       and not optional_mark and len(aliases) == 1)
        if is_required:
            required.append(name)

    schema = {"type": "object", "properties": props}
    if required:
        schema["required"] = required
    return schema, notes


# ---------- 4. C++ 테이블 출력 ----------
MAX_DESC = 400

GEN_HEADER = """// Copyright Epic Games, Inc. All Rights Reserved.
//
// GENERATED FILE - DO NOT EDIT BY HAND.
// Regenerate with:  python Plugins/LLMBlueprintBridge/Tools/generate_tool_catalog.py
// Source of truth:  Plugins/LLMBlueprintBridge/Docs/API.md
//                   Plugins/LLMBlueprintBridge/Source/.../LLMBlueprintBridgeServer.cpp (InvokeTool dispatch)
//
// Tools: %d

"""


def cpp_escape(text):
    """C++ 문자열 리터럴용 이스케이프: 역슬래시와 큰따옴표."""
    return text.replace(chr(92), chr(92) * 2).replace('"', chr(92) + '"')


def emit_cpp(tools, dest):
    lines = [GEN_HEADER % len(tools)]
    lines.append("static const FLLMBlueprintBridgeGeneratedTool GLLMBlueprintBridgeGeneratedTools[] =\n{\n")
    for t in tools:
        desc = t["description"]
        if len(desc) > MAX_DESC:
            desc = desc[:MAX_DESC - 3].rstrip() + "..."
        schema = json.dumps(t["inputSchema"], ensure_ascii=True, separators=(",", ":"))
        lines.append("\t{\n")
        lines.append('\t\tTEXT("%s"),\n' % cpp_escape(t["name"]))
        lines.append('\t\tTEXT("%s"),\n' % cpp_escape(desc))
        lines.append('\t\tTEXT("%s")\n' % cpp_escape(schema))
        lines.append("\t},\n")
    lines.append("};\n")
    io.open(dest, "w", encoding="utf-8", newline="\r\n").write("".join(lines))


def main():
    dispatch = dispatch_tool_names()
    entries = api_entries(set(dispatch))

    covered = [t for t in dispatch if t in entries]
    missing = [t for t in dispatch if t not in entries]
    extra = [t for t in entries if t not in dispatch]

    out = []
    for name in dispatch:
        e = entries.get(name)
        if e:
            schema, _ = build_schema(e["args_src"])
            schema, desc = apply_overrides(name, schema, e["description"])
        else:
            schema, desc = apply_overrides(
                name, {"type": "object"},
                name + " (schema not documented in Docs/API.md)")
        out.append({"name": name, "description": desc, "inputSchema": schema})

    here = os.path.dirname(os.path.abspath(__file__))
    json_dest = os.path.join(here, "tools.json")
    io.open(json_dest, "w", encoding="utf-8").write(json.dumps(out, ensure_ascii=False, indent=1))

    inl_dest = os.path.join(
        ROOT, "Source", "LLMBlueprintBridge", "Private",
        "LLMBlueprintBridgeToolCatalog.gen.inl")
    emit_cpp(out, inl_dest)

    print("dispatch tools : %d" % len(dispatch))
    print("API.md covered : %d" % len(covered))
    print("API.md missing : %d" % len(missing))
    for m in missing:
        print("   - " + m)
    print("API.md extra   : %d" % len(extra))
    for m in extra:
        print("   + " + m)
    print("wrote " + json_dest)
    print("wrote " + inl_dest)


if __name__ == "__main__":
    main()
