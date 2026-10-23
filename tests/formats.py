#!/usr/bin/env python3
"""Checks seqview against an independent parser on every supported format.

Writes FASTA and FASTQ files of every kind seqview claims to read (each
extension, plain and gzipped, multi-member gzip, multi-line FASTQ, Windows
line endings, lowercase, RNA, ambiguity codes, empty records, a large
chromosome) and broken ones, runs the seqview commands on them, and compares
the results with what this script computes itself.

    tests/formats.py [path/to/seqview]   (default: build/seqview)
"""
import gzip, json, os, random, re, subprocess, sys, tempfile

SEQVIEW = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "build", "seqview")
CHROMOSOME = int(os.environ.get("CHROMOSOME_BASES", "30000000"))
fails = []


def write_files(d):
    random.seed(42)
    def dna(n, alpha="ACGT"): return "".join(random.choice(alpha) for _ in range(n))
    def wrap(s, w): return "\n".join(s[i:i + w] for i in range(0, len(s), w))
    def qual(n): return "".join(chr(33 + random.randint(2, 40)) for _ in range(n))
    def put(name, text, mode="w"): open(os.path.join(d, name), mode, newline="" if mode == "w" else None).write(text)
    recs = [("seq%d" % i, "desc %d" % i, dna(random.randint(50, 900))) for i in range(30)]
    fa = "".join(">%s %s\n%s\n" % (i, desc, wrap(s, 60)) for i, desc, s in recs)
    for ext in ["fa", "fasta", "fna"]: put("nuc." + ext, fa)
    aa = "ACDEFGHIKLMNPQRSTVWY"
    put("prot.faa", "".join(">p%d hypothetical\n%s\n" % (i, wrap("M" + dna(random.randint(30, 400), aa) + "*", 60)) for i in range(20)))
    reads = [("r%d/1" % i, dna(random.randint(30, 151))) for i in range(500)]
    fq = "".join("@%s\n%s\n+\n%s\n" % (i, s, qual(len(s))) for i, s in reads)
    for ext in ["fq", "fastq"]: put("reads." + ext, fq)
    # Quality lines that start with @ and +, wrapped like the sequence.
    put("multiline.fastq", "".join("@ml%d\n%s\n+ml%d\n%s\n" % (i, wrap(dna(120), 50), i, wrap("@+" + qual(118), 50)) for i in range(50)))
    put("crlf.fa", fa.replace("\n", "\r\n"))
    put("crlf.fq", fq.replace("\n", "\r\n"))
    put("odd.fasta", ">soft\tmasked region\nacgtACGTnnnnNNNNacgt\n\n>rna\nAUGGCCAUUGUAAUGGGCCGCUGA\n>empty\n"
                     ">iupac only\nRYKMSWBDHVN\n>nodesc\nGGGGCCCC\n")
    put("nonewline.fa", ">a\nACGT\n>b\nGGCC")
    put("chrom.fa", ">chr1 big\n" + wrap(dna(CHROMOSOME), 80) + "\n")
    put("reads.txt", fq)
    for f in ["nuc.fa", "nuc.fasta", "nuc.fna", "prot.faa", "reads.fq", "reads.fastq", "multiline.fastq", "crlf.fa", "odd.fasta"]:
        put(f + ".gz", gzip.compress(open(os.path.join(d, f), "rb").read()), "wb")
    cut = fq.rfind("\n@", 0, len(fq) // 2) + 1
    put("multimember.fq.gz", gzip.compress(fq[:cut].encode()) + gzip.compress(fq[cut:].encode()), "wb")
    put("empty.fa", "")
    put("notseq.fa", "hello world\nthis is not a sequence\n")
    put("truncated.fq", fq[:len(fq) // 2 - 7])
    g = gzip.compress(fq.encode()); put("truncated.fq.gz", g[:len(g) // 2], "wb")
    put("binary.fa", os.urandom(4096), "wb")
    put("badqual.fq", "@x\nACGT\n+\nII\n@y\nAC\n+\nII\n")


def run(*args):
    p = subprocess.run([SEQVIEW, *args], capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def text(path):
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    return raw.decode().replace("\r\n", "\n")


def parse(path):
    lines = text(path).split("\n")
    recs, i = [], 0
    while i < len(lines) and not lines[i].strip(): i += 1
    if i == len(lines): return "empty", []
    if not lines[i].startswith("@"):
        cur = None
        for l in lines:
            if l.startswith(">"):
                cur = [l[1:].split()[0] if l[1:].split() else "", [], None]; recs.append(cur)
            elif cur is not None:
                cur[1].append(l.replace(" ", "").replace("\t", ""))
        for r in recs: r[1] = "".join(r[1])
        return "fasta", recs
    while i < len(lines):
        if not lines[i].strip(): i += 1; continue
        rid = lines[i][1:].split()[0]; i += 1; s = ""
        while not lines[i].startswith("+"): s += lines[i]; i += 1
        i += 1; q = ""
        while len(q) < len(s): q += lines[i].strip(); i += 1
        recs.append([rid, s, q])
    return "fastq", recs


def expect(path):
    fmt, recs = parse(path)
    lengths = [len(r[1]) for r in recs]; total = sum(lengths)
    up = "".join(r[1] for r in recs).upper()
    gc = up.count("G") + up.count("C"); at = up.count("A") + up.count("T") + up.count("U")
    letters = sum(c.isalpha() for c in up)
    nucleotide = sum(up.count(c) for c in "ACGTUN")
    protein_only = sum(up.count(c) for c in "EFILPQJOZX")
    acc = n50 = 0
    for x in sorted(lengths, reverse=True):
        acc += x
        if acc * 2 >= total: n50 = x; break
    is_nucleotide = fmt == "fastq" or nucleotide >= letters * 0.9 or protein_only * 100 < letters
    e = {"records": len(recs), "total_length": total, "min_length": min(lengths), "max_length": max(lengths),
         "n50": n50, "format": fmt, "alphabet": "nucleotide" if is_nucleotide else "protein"}
    if is_nucleotide: e["gc_percent"] = 100 * gc / (gc + at) if gc + at else 0
    if fmt == "fastq":
        qs = "".join(r[2] for r in recs); e["mean_quality"] = sum(ord(c) - 33 for c in qs) / len(qs)
    return e, recs


def check(ok, message):
    if not ok: fails.append(message)


IUPAC = {"A": "A", "C": "C", "G": "G", "T": "T", "R": "[AG]", "Y": "[CT]", "S": "[GC]", "W": "[AT]", "K": "[GT]",
         "M": "[AC]", "B": "[CGT]", "D": "[AGT]", "H": "[ACT]", "V": "[ACG]", "N": "[ACGT]"}
COMPLEMENT = str.maketrans("ACGTURYKMBDHVNacgturykmbdhvn", "TGCAAYRMKVHDBNtgcaayrmkvhdbn")


def revcomp(s):
    out = s.translate(COMPLEMENT)[::-1]
    if "U" in s.upper() and "T" not in s.upper():
        out = out.replace("T", "U").replace("t", "u")
    return out


def hits(seq, motif):
    out, up = [], seq.upper().replace("U", "T")
    for strand, m in (("+", motif), ("-", revcomp(motif))):
        if strand == "-" and m == motif: continue
        pattern = re.compile("(?=" + "".join(IUPAC[c] for c in m) + ")")
        out += [(x.start() + 1, strand) for x in pattern.finditer(up)]
    return out


CODE = "FFLLSSSSYY**CC*WLLLLPPPPHHQQRRRRIIIMTTTTNNKKSSRRVVVVAAAADDEEGGGG"
INDEX = {"T": 0, "C": 1, "A": 2, "G": 3, "U": 0}


def translate(s, frame):
    s = (s if frame > 0 else revcomp(s)).upper(); o = abs(frame) - 1
    return "".join(CODE[INDEX[s[i]] * 16 + INDEX[s[i + 1]] * 4 + INDEX[s[i + 2]]] if all(c in INDEX for c in s[i:i + 3]) else "X"
                   for i in range(o, len(s) - 2, 3))


def main():
    d = tempfile.mkdtemp(prefix="seqview-formats-")
    out = os.path.join(d, "out"); os.mkdir(out)
    write_files(d)
    good = ["nuc.fa", "nuc.fasta", "nuc.fna", "prot.faa", "reads.fq", "reads.fastq", "multiline.fastq", "crlf.fa", "crlf.fq",
            "odd.fasta", "nonewline.fa", "chrom.fa", "reads.txt", "multimember.fq.gz"]
    good += [f + ".gz" for f in ["nuc.fa", "nuc.fasta", "nuc.fna", "prot.faa", "reads.fq", "reads.fastq", "multiline.fastq", "crlf.fa", "odd.fasta"]]
    for f in good:
        path = os.path.join(d, f)
        e, recs = expect(path)
        code, stdout, err = run("stats", path, "--json")
        if code != 0: fails.append(f"{f}: stats exit {code}: {err.strip()}"); continue
        g = json.loads(stdout)
        if f.startswith("odd"): check(g["alphabet"] == "nucleotide", f"{f}: detected as {g['alphabet']}")
        if f.startswith("prot"): check(g["alphabet"] == "protein", f"{f}: detected as {g['alphabet']}")
        for k, v in e.items():
            if isinstance(v, float): check(abs(g.get(k, -1) - v) < 1e-6, f"{f}: {k} {g.get(k)} != {v:.6f}")
            else: check(g.get(k) == v, f"{f}: {k} {g.get(k)} != {v}")
        check(sum(g["length_histogram"]["counts"]) == e["records"], f"{f}: length histogram does not add up")
        if e["alphabet"] == "nucleotide" and f != "chrom.fa":
            for motif in ["GATC", "GRAWTC", "ACNNGT"]:
                _, stdout, _ = run("grep", path, motif, "--both-strands", "--json")
                got = sorted((m["record"], m["position"], m["strand"]) for m in json.loads(stdout)["matches"])
                want = sorted((r[0], p, s) for r in recs for p, s in hits(r[1], motif))
                check(got == want, f"{f}: grep {motif}: {len(got)} matches, want {len(want)}")
            once, twice = os.path.join(out, f + ".rc1"), os.path.join(out, f + ".rc2")
            run("revcomp", path, once); run("revcomp", once, twice)
            check([r[1] for r in parse(once)[1]] == [revcomp(r[1]) for r in recs], f"{f}: revcomp differs")
            check([r[1] for r in parse(twice)[1]] == [r[1] for r in recs], f"{f}: revcomp twice differs")
            kept = os.path.join(out, f + ".filtered")
            run("filter", path, kept, "--min-len", "100", "--max-len", "500")
            check([r[0] for r in parse(kept)[1]] == [r[0] for r in recs if 100 <= len(r[1]) <= 500], f"{f}: --min-len/--max-len")
        if e["format"] == "fastq":
            kept = os.path.join(out, f + ".q21.fq.gz")
            run("filter", path, kept, "--min-q", "21")
            want = [r[0] for r in recs if sum(ord(c) - 33 for c in r[2]) / len(r[2]) >= 21]
            check([r[0] for r in parse(kept)[1]] == want, f"{f}: --min-q")
        print(f"ok {f}", flush=True)

    _, recs = parse(os.path.join(d, "nuc.fa"))
    run("translate", os.path.join(d, "nuc.fa"), os.path.join(out, "six.faa"), "--six-frames")
    want = [translate(r[1], frame) for r in recs for frame in (1, 2, 3, -1, -2, -3)]
    check([r[1] for r in parse(os.path.join(out, "six.faa"))[1]] == want, "translate --six-frames differs")

    for f in ["empty.fa", "notseq.fa", "truncated.fq", "truncated.fq.gz", "binary.fa", "badqual.fq", "missing.fa"]:
        code, stdout, err = run("stats", os.path.join(d, f), "--json")
        check(code == 1 and err.strip() and not stdout.strip(), f"{f}: exit {code}, stderr {err.strip()!r}")
        print(f"broken {f}: {err.strip().split(': ', 1)[-1]}")
    check("truncated" in run("stats", os.path.join(d, "truncated.fq.gz"))[2], "truncated gzip not reported as truncated")

    link = os.path.join(out, "link.fa")
    os.symlink(os.path.join(d, "nuc.fa"), link)
    before = open(os.path.join(d, "nuc.fa"), "rb").read()
    code, _, _ = run("revcomp", os.path.join(d, "nuc.fa"), link)
    check(code == 1 and open(os.path.join(d, "nuc.fa"), "rb").read() == before, "wrote through a link to the input")

    if fails:
        print("FAILED:")
        for f in fails: print("  " + f)
        sys.exit(1)
    print("All format checks passed.")


main()
