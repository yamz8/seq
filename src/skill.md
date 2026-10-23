---
name: seqview
description: |
  Inspect and transform FASTA and FASTQ sequence files (.fa, .fasta, .fna, .faa,
  .fq, .fastq, gzipped or not) with seqview. Use for ANY request to count
  records or bases, get N50, GC% or read quality, find a DNA or protein motif,
  filter reads by length or quality, reverse complement, or translate sequences.
---

# seqview

`seqview` reads FASTA and FASTQ files, plain or gzipped, and never changes the
file it reads. Every command below needs no display, takes `--json`, exits 0
on success and 1 on failure, and writes errors to stderr. `seqview help
<command>` lists a command's options.

## Commands

- `seqview stats <file> --json`: records, total length, min/max/mean length,
  N50, GC% (nucleotides), mean Phred quality (FASTQ), a length histogram, the
  reads per mean quality, and residue composition for proteins.
- `seqview grep <file> <motif> [--both-strands] --json`: every match of a
  motif, as record ID, 1-based start and end on the forward strand, and strand
  (`+` or `-`). DNA motifs take IUPAC codes (N, R, Y, S, W, K, M, B, D, H, V);
  protein motifs take X for any residue. A motif that is its own reverse
  complement is reported once, on `+`. `--max N` caps the matches (100000).
- `seqview filter <file> <out> [--min-len N] [--max-len N] [--min-q Q] --json`:
  writes the records within the limits to a new file. `--min-q` is the mean
  Phred quality of a read; FASTA records never pass it.
- `seqview revcomp <file> <out> --json`: reverse complements every record,
  reversing FASTQ qualities to match.
- `seqview translate <file> <out> [--frame F | --six-frames] --json`: protein
  FASTA with the standard genetic code; frames 1, 2, 3 and -1, -2, -3.
  Ambiguous codons become X and stops *.

Outputs are written atomically and an output name ending in `.gz` is gzipped.
Commands refuse to write over their input.

## Working with the person

`seqview open <file>` opens the viewer for them, where they can browse the
records, search, translate and find ORFs. It needs their desktop, so do not
wait on it.
