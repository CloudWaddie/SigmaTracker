#!/usr/bin/env python3
"""Generate flash-resident plain-text Lapwing index and original drills."""
import argparse, hashlib, json, pathlib, re, struct, urllib.request
ROOT = pathlib.Path(__file__).resolve().parents[1]
REV = "4ac5d53cf039a2b1f4c484456663c85e7df4e859"
UPSTREAM = f"https://raw.githubusercontent.com/aerickt/plover-lapwing-aio/{REV}/plover_lapwing/dictionaries/lapwing-base.json"
BLOB = "b405cd6d7b61cc50a96ca4af8d4ad79023a71aa2"
def valid(key, val):
    return (isinstance(key,str) and isinstance(val,str) and len(key)<=160 and
            1<=len(key.split('/'))<=6 and re.fullmatch(r"[#STKPWHRAO*EUFRPBLGTSDZ/-]+",key) and
            1<=len(val)<=80 and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 '-]*",val))
def cpp(s):
    return json.dumps(s,ensure_ascii=True)
def build(source, dest, header, strict):
    raw=json.loads(source)
    words={k:v for k,v in raw.items() if valid(k,v)}
    if not words: raise ValueError("Empty dictionary")
    entries=bytearray(); offsets=[]
    for k,v in sorted(words.items()):
        offsets.append(len(entries)); entries.extend(k.encode()+b'\0'+v.encode()+b'\0')
    result=b"STN1"+struct.pack("<I",len(offsets))+struct.pack(f"<{len(offsets)}I",*offsets)+entries
    if len(result)>11*1024*1024: raise ValueError("Dictionary too large for app partition")
    dest.write_bytes(result)
    reverse={}
    for k,v in words.items():
        if v not in reverse or (k.count('/'),len(k),k)<(reverse[v].count('/'),len(reverse[v]),reverse[v]):
            reverse[v]=k
    groups=json.loads((ROOT/"data/lessons.json").read_text())
    lines=["#pragma once",'#include "core.hpp"',"namespace content {",
           "inline constexpr const char *groups[] = {"+",".join(cpp(g["name"]) for g in groups)+"};",
           "inline constexpr steno::Drill drills[] = {"]
    for i,g in enumerate(groups):
        for s in g.get("raw",[]): lines.append(f"  {{{cpp(s)},{cpp(s)},{i},true}},")
        count=0
        for word in g.get("words",[]):
            if word not in reverse:
                print(f"Skipping unavailable word: {word}")
                continue
            count+=1
            lines.append(f"  {{{cpp(word)},{cpp(reverse[word])},{i},false}},")
        if strict and "words" in g and count<5: raise ValueError(f"Too few drills: {g['name']}")
    lines += ["};","}"]
    header.write_text("\n".join(lines)+"\n")
    print(f"{len(words):,} outlines; {len(result):,} bytes; {len(lines)-7} drills")
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--seed",action="store_true",help="Small illustrative dictionary for offline development (not authoritative Lapwing)")
    p.add_argument("--source",type=pathlib.Path,help="Use local upstream JSON")
    p.add_argument("--output",type=pathlib.Path,default=ROOT/"main/dictionary.bin")
    p.add_argument("--header",type=pathlib.Path,default=ROOT/"main/lessons_generated.hpp")
    a=p.parse_args()
    if a.seed: source=(ROOT/"data/seed.json").read_bytes()
    elif a.source: source=a.source.read_bytes()
    else:
        with urllib.request.urlopen(UPSTREAM,timeout=120) as f: source=f.read()
        digest=hashlib.sha1(f"blob {len(source)}\0".encode()+source).hexdigest()
        if digest!=BLOB: raise ValueError(f"Upstream blob changed: {digest}")
    build(source,a.output,a.header,not a.seed)
if __name__=="__main__": main()
