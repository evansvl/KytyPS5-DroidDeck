#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_fex_sparse_backing.py <memoryAddressSpace.inc>")

path = Path(sys.argv[1])
text = path.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    text = text.replace(old, new, 1)


replace_once(
'''\t\tauto* backing = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);\n\t\tif (backing == MAP_FAILED) {\n\t\t\tLOGF_COLOR(Log::Color::Red, "\\t direct-memory backing: writable alias failed: %d\\n",\n\t\t\t           errno);\n\t\t\tclose(m_fd);\n\t\t\tm_fd = -1;\n\t\t\treturn;\n\t\t}\n\t\tm_backing_base = static_cast<uint8_t*>(backing);\n''',
'''\t\t// DroidDeck/FEX: do not create a second contiguous writable alias for the entire\n\t\t// 13.5 GiB PS5 direct-memory backing. FEX must represent that mmap in its x86-64\n\t\t// guest VA space and can reject it even though the sparse memfd itself is valid.\n\t\t// Fixed guest mappings below still map the same memfd at their real PS5 addresses.\n\t\t// CPU-side backing reads/writes use pread/pwrite helpers instead.\n''',
"remove giant Linux writable alias",
)

replace_once(
'''#elif KYTY_PLATFORM == KYTY_PLATFORM_LINUX\n\t\treturn m_fd >= 0 && m_backing_base != nullptr;\n''',
'''#elif KYTY_PLATFORM == KYTY_PLATFORM_LINUX\n\t\treturn m_fd >= 0;\n''',
"Linux backing availability",
)

replace_once(
'''\t\tstd::lock_guard lock(m_mutex);\n\t\tstd::memset(m_backing_base + backing_offset, 0, size);\n\t\treturn true;\n\t}\n\n\tbool TryWriteBacking''',
'''\t\tstd::lock_guard lock(m_mutex);\n#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX\n\t\treturn ZeroFileRange(backing_offset, size);\n#else\n\t\tstd::memset(m_backing_base + backing_offset, 0, size);\n\t\treturn true;\n#endif\n\t}\n\n\tbool TryWriteBacking''',
"ZeroRange",
)

replace_once(
'''\t\tconst auto*     source = static_cast<const uint8_t*>(data);\n\t\tForEachSparseRange(\n\t\t    m_maps, vaddr, size,\n\t\t    [&](const auto& entry, uint64_t start, uint64_t bytes) {\n\t\t\t    std::memcpy(m_backing_base + (entry.backing_offset + start - entry.vaddr),\n\t\t\t                source + (start - vaddr), bytes);\n\t\t    },\n\t\t    [](uint64_t, uint64_t) {});\n\t\treturn true;\n''',
'''\t\tconst auto* source = static_cast<const uint8_t*>(data);\n\t\tbool        ok     = true;\n\t\tForEachSparseRange(\n\t\t    m_maps, vaddr, size,\n\t\t    [&](const auto& entry, uint64_t start, uint64_t bytes) {\n\t\t\t    const auto offset = entry.backing_offset + start - entry.vaddr;\n#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX\n\t\t\t    if (!WriteFileRange(offset, source + (start - vaddr), bytes)) {\n\t\t\t\t    ok = false;\n\t\t\t    }\n#else\n\t\t\t    std::memcpy(m_backing_base + offset, source + (start - vaddr), bytes);\n#endif\n\t\t    },\n\t\t    [](uint64_t, uint64_t) {});\n\t\treturn ok;\n''',
"TryWriteBacking",
)

replace_once(
'''\t\t\tconst auto  offset = entry.backing_offset + current - entry.vaddr;\n\t\t\tstd::memcpy(destination + (current - vaddr), m_backing_base + offset, bytes);\n\t\t\tcurrent += bytes;\n''',
'''\t\t\tconst auto  offset = entry.backing_offset + current - entry.vaddr;\n#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX\n\t\t\tif (!ReadFileRange(offset, destination + (current - vaddr), bytes)) {\n\t\t\t\treturn false;\n\t\t\t}\n#else\n\t\t\tstd::memcpy(destination + (current - vaddr), m_backing_base + offset, bytes);\n#endif\n\t\t\tcurrent += bytes;\n''',
"TryReadBacking copy",
)

replace_once(
'''\t\tCopySparseRange(m_maps, vaddr, data, size,\n\t\t                [this](const auto& entry, uint64_t start) {\n\t\t\t                return m_backing_base + (entry.backing_offset + start - entry.vaddr);\n\t\t                },\n\t\t                gap);\n\t\treturn true;\n''',
'''#if KYTY_PLATFORM == KYTY_PLATFORM_LINUX\n\t\tauto* destination = static_cast<uint8_t*>(data);\n\t\tbool  ok          = true;\n\t\tForEachSparseRange(\n\t\t    m_maps, vaddr, size,\n\t\t    [&](const auto& entry, uint64_t start, uint64_t bytes) {\n\t\t\t    const auto offset = entry.backing_offset + start - entry.vaddr;\n\t\t\t    if (!ReadFileRange(offset, destination + (start - vaddr), bytes)) {\n\t\t\t\t    ok = false;\n\t\t\t    }\n\t\t    },\n\t\t    [&](uint64_t start, uint64_t bytes) { gap(start, destination + (start - vaddr), bytes); });\n\t\treturn ok;\n#else\n\t\tCopySparseRange(m_maps, vaddr, data, size,\n\t\t                [this](const auto& entry, uint64_t start) {\n\t\t\t                return m_backing_base + (entry.backing_offset + start - entry.vaddr);\n\t\t                },\n\t\t                gap);\n\t\treturn true;\n#endif\n''',
"TryReadSparseBacking",
)

replace_once(
'''\tint m_fd = -1;\n#endif\n\n\tuint8_t*                     m_backing_base = nullptr;\n''',
'''\tbool ReadFileRange(uint64_t offset, void* data, uint64_t size) const {\n\t\tauto* output = static_cast<uint8_t*>(data);\n\t\tuint64_t done = 0;\n\t\twhile (done < size) {\n\t\t\tconst auto chunk = static_cast<size_t>(std::min<uint64_t>(size - done, 0x40000000ull));\n\t\t\tconst auto result = pread(m_fd, output + done, chunk, static_cast<off_t>(offset + done));\n\t\t\tif (result < 0 && errno == EINTR) {\n\t\t\t\tcontinue;\n\t\t\t}\n\t\t\tif (result <= 0) {\n\t\t\t\treturn false;\n\t\t\t}\n\t\t\tdone += static_cast<uint64_t>(result);\n\t\t}\n\t\treturn true;\n\t}\n\n\tbool WriteFileRange(uint64_t offset, const void* data, uint64_t size) const {\n\t\tconst auto* input = static_cast<const uint8_t*>(data);\n\t\tuint64_t done = 0;\n\t\twhile (done < size) {\n\t\t\tconst auto chunk = static_cast<size_t>(std::min<uint64_t>(size - done, 0x40000000ull));\n\t\t\tconst auto result = pwrite(m_fd, input + done, chunk, static_cast<off_t>(offset + done));\n\t\t\tif (result < 0 && errno == EINTR) {\n\t\t\t\tcontinue;\n\t\t\t}\n\t\t\tif (result <= 0) {\n\t\t\t\treturn false;\n\t\t\t}\n\t\t\tdone += static_cast<uint64_t>(result);\n\t\t}\n\t\treturn true;\n\t}\n\n\tbool ZeroFileRange(uint64_t offset, uint64_t size) const {\n\t\tstd::array<uint8_t, 64 * 1024> zeros {};\n\t\tuint64_t done = 0;\n\t\twhile (done < size) {\n\t\t\tconst auto chunk = std::min<uint64_t>(size - done, zeros.size());\n\t\t\tif (!WriteFileRange(offset + done, zeros.data(), chunk)) {\n\t\t\t\treturn false;\n\t\t\t}\n\t\t\tdone += chunk;\n\t\t}\n\t\treturn true;\n\t}\n\n\tint m_fd = -1;\n#endif\n\n\tuint8_t*                     m_backing_base = nullptr;\n''',
"Linux file-backed helpers",
)

path.write_text(text)
print(f"patched {path}")
