# Filesystem-Fosfat

## 1. Purpose and format scope

Smaky filesystem, using the Fosfat decoder as the central source baseline. This repository is the designated independent implementation home for InfiltratorOS. **Independent read-only Common V2 development provider. No installable release has been published.**

## 2. On-disk architecture and invariants

Disk geometry, native directory and file records, block chains and format metadata must be decoded by the native engine. The historical FUSE adapter is not a production filesystem authority. The initial reader implements the subset described below.

## 3. Common V2 implementation and capabilities

Native Smaky FOS reader: floppy/hard-disk geometry, checked BD/BL chains, fragmented files, nested directories, ASCII names and volume label. Encoded entries remain enumerable but encoded reads are refused. No allocation query; zero mount free bytes is not a measured free-space count. Hosted and import-free freestanding builds share the native engine and export `infiltrator_filesystem_api_v2`.

## 4. Integrity, mutation, allocation and recovery

Mutation, formatting, check/repair and DEFRAG map/plan/execute/verify/recover require format-specific allocation and ordering invariants, exclusive writable host access and demonstrated interruption recovery. Until qualified, reject them before writing. Preserve native disk semantics and do not add a kernel or FUSE fallback as an alternative filesystem authority.

## 5. Build, installation and release

The target installation is `/System/Filesystems/FOSFAT.so` and `/System/Resources/Documentation/Filesystem-Fosfat.pdf`. Neither file is currently distributed by this repository. Once the implementation qualifies, the intended release is a portable amd64 DEB and CPU-native source-first RUN, with GitHub's automatic source archive. With the pinned Common headers checked out under Libraries, use `make check` for development tests and `make carrier` for the hosted ELF containing its freestanding profile. Neither is an installable release.

## 6. Qualification and limitations

[Passing exact-revision CI](https://github.com/Infiltrator-Projects/Filesystem-Fosfat/actions/runs/45972231768) at `4ece8bed7cdb39da35c8761ef23b5297ab0d39ca`. Constructed media only; historical Smaky media, MOS, non-ASCII names, link target decoding, allocation accounting and signed boot remain unqualified. Write, format, repair, defragmentation and production packages remain outstanding.

## 7. Technical references and provenance

- [Technical reference (PDF)](docs/Fosfat.pdf) — original-format technical reference as one integrated manual and [PDF stream source](docs/Fosfat.pdfstreams); not an operational manual.

- [Pinned central source baseline](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/fosfat)
- [Native design](native/filesystems/fosfat/DESIGN.md)
- [Migration and qualification plan](MIGRATION_STATUS.md)
- [Original-format reference source](docs/Fosfat.pdfstreams) and [PDF](docs/Fosfat.pdf)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

Preserve inherited notices and licences on source extraction; new project-authored code uses GPL-3.0-or-later. The [technical reference](docs/Fosfat-TECHNICAL-REFERENCE.md) expands original-format research. The PDF preserves both older documents as historical appendices; installation remains pending. Central code or test results are not evidence of an independent provider.
