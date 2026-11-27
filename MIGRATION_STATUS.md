# Fosfat development qualification

Native Smaky FOS reader: floppy/hard-disk geometry, checked BD/BL chains, fragmented files, nested directories, ASCII names and volume label. Encoded entries remain enumerable but encoded reads are refused. No allocation query; zero mount free bytes is not a measured free-space count.

[Passing hosted, freestanding and sanitizer CI](https://github.com/Infiltrator-Projects/Filesystem-Fosfat/actions/runs/45972231768) at `4ece8bed7cdb39da35c8761ef23b5297ab0d39ca`.

Constructed media only; historical Smaky media, MOS, non-ASCII names, link target decoding, allocation accounting and signed boot remain unqualified.

No mutation, formatting, repair, defragmentation or production release is qualified. The BOOT development matrix separately tests its embedded firmware carrier and namespace reads; matrix inclusion alone is not a successful run or signed firmware boot evidence.
