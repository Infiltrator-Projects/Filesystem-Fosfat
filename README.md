# Filesystem-Fosfat

Independent Smaky filesystem provider for InfiltratorOS. Target: one native C/C++ engine and one distributable `FOSFAT.so`.

**Status: documentation only; source extraction pending.** [Central source](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/fosfat). Original Skywalker13/Fosfat userspace decoder remains centrally, pinned at 289c7eb838dd5e6305c98f09aa375746a6043d86; its FUSE adapter is not a target runtime. No independent driver or release exists.

## Common V2

Use only `infiltrator_filesystem_api_v2` from [Libraries](https://github.com/Infiltrator-Projects/Libraries). Own disk geometry, directory and file records, block chains and native metadata in the same engine for hosted and freestanding execution. No FUSE, separate per-format kernel/EFI driver or legacy ABI.

## Build and install

Planned: `/System/Filesystems/FOSFAT.so` and `/System/Resources/Documentation/Filesystem-Fosfat.pdf`. Generic amd64 .deb, source-downloading CPU-native .run and GitHub-generated source ZIP only. No build or installer exists yet.

See [migration status](MIGRATION_STATUS.md) and [engineering philosophy](docs/PHILOSOPHY.md).
