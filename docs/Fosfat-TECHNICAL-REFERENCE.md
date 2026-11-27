# Fosfat: technical reference

## Scope and evidence

Fosfat is the name of a reader for the native Smaky FOS filesystem, not a Microsoft FAT variant. Smaky computers were developed in Switzerland; the media contain a boot region, system catalogue, file-list records, block descriptors and data tranches. Mathieu Schroeter's libfosfat credits Pierre Arnaud and Epsitec for assistance and documentation. The following field descriptions are independently expressed from the pinned libfosfat reader, commit 289c7eb838dd5e6305c98f09aa375746a6043d86, libfosfat/fosfat.c. They establish a reader-oriented description, not the complete proprietary original writer specification. Unknown fields must be preserved and native free-space, formatting and transactional algorithms remain unresolved.

## Address units and media detection

Logical FOS blocks contain 256 bytes. Multi-byte numeric fields use big-endian byte order. All offsets in the field inventories below are zero-based byte offsets within their containing record, unless stated otherwise. An image-relative block address equals (logical block number + boot blocks) times 256. Floppy media use 16 boot blocks and hard disks use 32 in the reference reader. The logical system-list descriptor is block 1 and its first list block is block 2. For floppy media these lie at byte offsets 4352 and 4608, respectively; for hard disks they lie at 8448 and 8704. A probe must bound both candidate addresses, confirm that the descriptor references block 2, and compare the four-byte check identifiers. Do not infer geometry from file extension alone; report ambiguity if multiple interpretations satisfy checks.

The original decoder separates native directory attribute bit 12 and the block-list, file-list and descriptor structures from its pointer-bearing host cache/list objects. Its FUSE presentation is not an on-disk structure or a production implementation requirement.

## Volume block zero

Logical block 0 has 256 bytes. Offset 0 length 44 is the system-folder material; offset 44 length 16 is the disk name; offset 60 length 4 is the check identifier; offset 64 length 172 is message material. Offset 236 is the change indicator, 237 the boot-check indicator, 238 length 4 the old check identifier, 242 length 4 the new check identifier, and 246 length 10 reserved. The upstream names do not establish a safe transition protocol. Do not rewrite the change fields or regenerate check identifiers from assumptions. Interpret the bounded name and retain raw bytes. The actual disk size must be obtained from the supplied medium rather than extrapolated from a name or message.

## Block-list catalogue layout

Each block list (BL) contains four 60-byte file records occupying offsets 0, 60, 120 and 180. Offset 240 is a four-byte next-BL number, 244 the four-byte check identifier, 248 the four-byte previous-BL number and 252 four reserved bytes. Block descriptor runs enumerate directory list blocks in the reference reader; the next/previous fields are additional consistency evidence rather than justification to follow arbitrary addresses. Check that catalogue blocks lie inside the volume and are not boot sectors. When both descriptor traversal and catalogue links are available, compare them and flag contradictions; do not repair by guessing which is correct.

## File record field inventory

In each 60-byte BL record: offset 0 length 16 name; 16 type; 17 open state; 18 length 4 attributes; 22 length 4 length in blocks; 26 length 2 last-block length; 28 length 3 creation date; 31 length 3 creation time; 34 length 3 write date; 37 length 3 write time; 40 length 3 read date; 43 length 3 read time; 46 length 3 security identifier; 49 close-mode field; 50 length 4 block descriptor pointer; 54 length 4 byte length; 58 length 2 code field. Names are bounded byte strings, not assumed UTF-8. A zero first name byte is used by libfosfat as a deleted-record indicator; distinguish blank slots and damaged records before recovery. The byte length is the reported logical size. Compare it against actual descriptor coverage.

## Block descriptor field inventory

A block descriptor (BD) contains 256 bytes. Offset 0 length 4 next descriptor, 4 length 4 previous descriptor, 8 length 2 tranche count, 10 length 168 array of 42 four-byte starting blocks, 178 length 16 name, 194 length 42 array of 42 one-byte block counts, 236 length 4 reserved, 240 length 2 last tranche byte count, 242 length 2 hash field, 244 length 2 number of BL blocks, 246 length 4 check identifier, 250 length 4 prior-descriptor block offset, 254 length 2 reserved. Reject counts greater than 42 before accessing the arrays. For each populated index, decode the start and count together. Check that each run ends within the image and does not overlap metadata unless the object is itself a directory. Follow next-descriptor links until zero, with cycle and length bounds.

## File data translation and example

Within a regular file, each populated tranche represents consecutive 256-byte data blocks. Traverse descriptors and their tranche arrays in logical order. The pinned reader uses the BD's final byte count for the last block of the final tranche in each descriptor, rather than blindly treating all allocated blocks as full. Require that count to be within 0..256 and reconcile with the BL record's byte length. Example: a two-block tranche starting at block 100 on floppy media begins at byte 29696. If the final byte count is 20, its logical coverage is 276 bytes; the last 20 bytes start at byte 29952. For random reads, subtract preceding tranche coverage from the requested offset and intersect the remaining range with the current run. The result must never contain slack bytes beyond EOF.

## Directory hierarchy and names

The system list is the catalogue entry point. A directory's BD describes runs of BL blocks, not ordinary file data. Enumerate four records per BL, apply native visibility and object-type rules, and resolve nested directory descriptors through their recorded pointers. The same system-list reference can recur within a directory; a recursive enumerator must track visited descriptor identities and treat repeated system references appropriately. Case-insensitive comparison and lowercasing are behaviours of the reference host reader; preserve native spelling and avoid inventing a case-folding or character-set table. Resolve paths component by component, never by treating a directory byte length as a conventional inode count.

## Attributes, dates and symbolic references

The attribute word has observed flags at bit 0 exclusive open, 1 multiple open, 12 directory, 13 visible, 17 encoded and 24 link. The reference tests the type byte against mask 0xF8 for system classification. Preserve all other flags and the security and open-state fields. Dates are packed BCD day, month, two-digit year; times are BCD hour, minute, second. The host reader interprets years below 70 as 2000 onward and other years as 1900 onward; that pivot is a host policy, not a stored century. Validate decimal nibbles and calendar ranges. For link objects the reference reader starts the path at byte 3 of the first data block, translates colons to slashes and trims the final slash-delimited component. Treat this as observed conversion, not a complete native link encoding specification.

## Allocation, integrity and recovery boundaries

Reachable allocation can be reconstructed by recording every catalogue, descriptor and payload run, checking duplicate ownership, range violations, conflicting metadata and inconsistent file sizes. That reconstruction is not a native free-space map: unreferenced blocks may be reserved, orphaned or otherwise allocated. The reference reader compares check identifiers across BL and BD structures; this is not evidence of a payload CRC, journal or atomic commit. A forensic tool should preserve the raw image, report all contradictions, and extract recoverable file data into a separate destination. Deleted entries may retain descriptor addresses but blocks can already have been reassigned. Do not infer the original free-list format, allocation algorithms, check-number transition, media formatting or write ordering from the read-only implementation.

## Independent reader validation

Use authentic floppy and hard-disk images as fixtures, with the pinned libfosfat reader as a comparison oracle. Verify block-size and boot-prefix detection, all four BL slots, multi-descriptor and multi-tranche files, final partial blocks, nested directories, repeated system references, deleted names and BCD dates. Inject truncated blocks, a tranche count of 43, cyclic descriptors, out-of-range runs, inconsistent check identifiers and mismatched file lengths. Record image hashes before and after read-only operation. Compare directory names, byte lengths and extracted payload hashes. A native writer, formatter, repairer or defragmenter requires original allocator and recovery evidence before qualification; an independent reader does not establish those capabilities.

## Implementation and qualification boundary

The independent provider uses Common V2: https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md . Negotiate actual operation masks and media profiles. Distinguish unsupported, corrupt, I/O and stale-handle results. Require bounded host I/O, exclusive unmounted ownership and durable flush before mutation. Test Media and Defragmenter consume the same engine, not separate format parsers. Hosted and freestanding binaries, relocation, recovery, BOOT, kernel integration and packages require separate exact-source evidence. Current support belongs to the repository README and qualification records. The intended package paths are /System/Filesystems/ and /System/Resources/Documentation/; the reference is not an installation or release-status claim.

## Reference and remaining research

Primary implementation evidence: https://github.com/Skywalker13/Fosfat/blob/289c7eb838dd5e6305c98f09aa375746a6043d86/libfosfat/fosfat.c . Relevant functions include media detection, block-list and descriptor decoding, file extraction, directory enumeration and attribute conversion. Original Smaky documentation and known native images should be used to verify field meanings, especially the block-zero transition fields, system allocation, the hash and code fields, and native write ordering. No source-code listing is reproduced here. This chapter does not establish provider implementation or qualification.

Preserved upstream baseline: Skywalker13/Fosfat 289c7eb838dd5e6305c98f09aa375746a6043d86; original Smaky images are required for independent validation.

Retained source: https://github.com/Infiltrator-Projects/Filesystems/blob/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/fosfat/userspace/libfosfat/fosfat.c (blob 42ace99fea).
