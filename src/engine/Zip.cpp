#include "Zip.h"

#include <array>
#include <cstdint>
#include <vector>

namespace {

// --- Inflate (RFC 1951), after Mark Adler's puff: canonical Huffman codes decoded bit by bit
constexpr int kMaxBits = 15;

struct Input {
    const uint8_t *data;
    size_t size, pos = 0;
    uint32_t buf = 0;
    int count = 0;
    bool error = false;
    int bits(int n)
    {
        uint32_t v = buf;
        while (count < n) {
            if (pos >= size) {
                error = true;
                return 0;
            }
            v |= uint32_t(data[pos++]) << count;
            count += 8;
        }
        buf = v >> n;
        count -= n;
        return int(v & ((1u << n) - 1));
    }
};

struct Huffman {
    std::array<short, kMaxBits + 1> count{};
    std::vector<short> symbol;
};

// The code of each symbol from its length; false when the lengths over-subscribe the code space
bool build(Huffman &h, const short *length, int n)
{
    h.count.fill(0);
    h.symbol.assign(size_t(n), 0);
    for (int s = 0; s < n; ++s) ++h.count[size_t(length[s])];
    if (h.count[0] == n) return true; // no code (a valid, empty distance code)
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[size_t(len)];
        if (left < 0) return false;
    }
    std::array<short, kMaxBits + 1> offs{};
    for (int len = 1; len < kMaxBits; ++len) offs[size_t(len + 1)] = short(offs[size_t(len)] + h.count[size_t(len)]);
    for (int s = 0; s < n; ++s)
        if (length[s] != 0) h.symbol[size_t(offs[size_t(length[s])]++)] = short(s);
    return true;
}

int decode(Input &in, const Huffman &h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= in.bits(1);
        if (in.error) return -1;
        const int count = h.count[size_t(len)];
        if (code - count < first) return h.symbol[size_t(index + (code - first))];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

bool codes(Input &in, std::vector<uint8_t> &out, const Huffman &lencode, const Huffman &distcode)
{
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int symbol = decode(in, lencode);
        if (symbol < 0) return false;
        if (symbol < 256) {
            out.push_back(uint8_t(symbol));
            continue;
        }
        if (symbol == 256) return true;
        symbol -= 257;
        if (symbol >= 29) return false;
        const int len = lbase[symbol] + in.bits(lext[symbol]);
        symbol = decode(in, distcode);
        if (symbol < 0 || symbol >= 30) return false;
        const size_t dist = size_t(dbase[symbol] + in.bits(dext[symbol]));
        if (in.error || dist > out.size()) return false;
        for (int k = 0; k < len; ++k) out.push_back(out[out.size() - dist]);
    }
}

bool stored(Input &in, std::vector<uint8_t> &out)
{
    in.buf = 0; // the rest of the current byte is padding
    in.count = 0;
    if (in.pos + 4 > in.size) return false;
    const unsigned len = in.data[in.pos] | in.data[in.pos + 1] << 8;
    const unsigned nlen = in.data[in.pos + 2] | in.data[in.pos + 3] << 8;
    in.pos += 4;
    if (len != (~nlen & 0xffff) || in.pos + len > in.size) return false;
    out.insert(out.end(), in.data + in.pos, in.data + in.pos + len);
    in.pos += len;
    return true;
}

bool fixedBlock(Input &in, std::vector<uint8_t> &out)
{
    static Huffman lencode, distcode;
    static const bool ready = [] {
        short lengths[288];
        int s = 0;
        for (; s < 144; ++s) lengths[s] = 8;
        for (; s < 256; ++s) lengths[s] = 9;
        for (; s < 280; ++s) lengths[s] = 7;
        for (; s < 288; ++s) lengths[s] = 8;
        build(lencode, lengths, 288);
        for (s = 0; s < 30; ++s) lengths[s] = 5;
        build(distcode, lengths, 30);
        return true;
    }();
    (void)ready;
    return codes(in, out, lencode, distcode);
}

bool dynamicBlock(Input &in, std::vector<uint8_t> &out)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = in.bits(5) + 257, ndist = in.bits(5) + 1, ncode = in.bits(4) + 4;
    if (in.error || nlen > 286 || ndist > 30) return false;
    short lengths[320] = {};
    for (int i = 0; i < ncode; ++i) lengths[order[i]] = short(in.bits(3));
    Huffman lencode, distcode;
    if (in.error || !build(lencode, lengths, 19)) return false;
    int index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(in, lencode);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[index++] = short(symbol);
            continue;
        }
        short len = 0;
        int repeat;
        if (symbol == 16) {
            if (index == 0) return false;
            len = lengths[index - 1];
            repeat = 3 + in.bits(2);
        } else if (symbol == 17) {
            repeat = 3 + in.bits(3);
        } else {
            repeat = 11 + in.bits(7);
        }
        if (in.error || index + repeat > nlen + ndist) return false;
        while (repeat--) lengths[index++] = len;
    }
    if (lengths[256] == 0) return false; // no end-of-block code
    if (!build(lencode, lengths, nlen) || !build(distcode, lengths + nlen, ndist)) return false;
    return codes(in, out, lencode, distcode);
}

// --- Zip structure (APPNOTE.TXT)
uint32_t u16(const QByteArray &b, qsizetype at) { return uint8_t(b[at]) | uint32_t(uint8_t(b[at + 1])) << 8; }
uint32_t u32(const QByteArray &b, qsizetype at) { return u16(b, at) | u16(b, at + 2) << 16; }

uint32_t crc32(const QByteArray &b)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    uint32_t c = 0xffffffffu;
    for (const char ch : b) c = table[(c ^ uint8_t(ch)) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

struct Entry {
    QString name;
    uint32_t method = 0, crc = 0, packed = 0, size = 0, local = 0;
};

std::vector<Entry> entries(const QByteArray &a, QString *err)
{
    std::vector<Entry> list;
    // The end of central directory record: in the last 64 KiB + 22 bytes (its comment comes after it)
    qsizetype end = -1;
    for (qsizetype at = a.size() - 22; at >= 0 && at >= a.size() - 22 - 65535; --at)
        if (u32(a, at) == 0x06054b50) {
            end = at;
            break;
        }
    if (end < 0) {
        if (err) *err = QStringLiteral("Not a zip archive");
        return list;
    }
    const uint32_t count = u16(a, end + 10);
    qsizetype at = u32(a, end + 16);
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 46 > a.size() || u32(a, at) != 0x02014b50) {
            if (err) *err = QStringLiteral("Damaged zip directory");
            return {};
        }
        Entry e;
        e.method = u16(a, at + 10);
        e.crc = u32(a, at + 16);
        e.packed = u32(a, at + 20);
        e.size = u32(a, at + 24);
        const uint32_t nameLen = u16(a, at + 28), extraLen = u16(a, at + 30), commentLen = u16(a, at + 32);
        e.local = u32(a, at + 42);
        if (at + 46 + nameLen > a.size()) {
            if (err) *err = QStringLiteral("Damaged zip directory");
            return {};
        }
        e.name = QString::fromUtf8(a.mid(at + 46, nameLen));
        list.push_back(e);
        at += 46 + nameLen + extraLen + commentLen;
    }
    return list;
}

} // namespace

namespace zip {

QByteArray inflate(const QByteArray &deflated, bool *ok)
{
    Input in{reinterpret_cast<const uint8_t *>(deflated.constData()), size_t(deflated.size())};
    std::vector<uint8_t> out;
    bool good = true, last = false;
    while (good && !last) {
        last = in.bits(1);
        const int type = in.bits(2);
        if (in.error) good = false;
        else if (type == 0) good = stored(in, out);
        else if (type == 1) good = fixedBlock(in, out);
        else if (type == 2) good = dynamicBlock(in, out);
        else good = false;
        good = good && !in.error;
    }
    if (ok) *ok = good;
    return good ? QByteArray(reinterpret_cast<const char *>(out.data()), qsizetype(out.size())) : QByteArray();
}

QStringList names(const QByteArray &archive, QString *err)
{
    QStringList l;
    for (const Entry &e : entries(archive, err)) l << e.name;
    return l;
}

QByteArray read(const QByteArray &a, const QString &name, QString *err)
{
    auto fail = [&](const QString &w) {
        if (err) *err = w;
        return QByteArray();
    };
    QString why;
    const std::vector<Entry> list = entries(a, &why);
    if (list.empty()) return fail(why.isEmpty() ? QStringLiteral("Empty zip archive") : why);
    for (const Entry &e : list) {
        if (e.name.compare(name, Qt::CaseInsensitive) != 0) continue;
        const qsizetype at = e.local;
        if (at + 30 > a.size() || u32(a, at) != 0x04034b50) return fail(QStringLiteral("Damaged zip entry: ") + name);
        const qsizetype data = at + 30 + u16(a, at + 26) + u16(a, at + 28);
        if (data + qsizetype(e.packed) > a.size()) return fail(QStringLiteral("Truncated zip entry: ") + name);
        const QByteArray packed = a.mid(data, e.packed);
        QByteArray out;
        if (e.method == 0) {
            out = packed;
        } else if (e.method == 8) {
            bool ok = false;
            out = inflate(packed, &ok);
            if (!ok) return fail(QStringLiteral("Unreadable compressed data: ") + name);
        } else {
            return fail(QStringLiteral("Unsupported zip compression (%1): %2").arg(e.method).arg(name));
        }
        if (uint32_t(out.size()) != e.size || crc32(out) != e.crc) return fail(QStringLiteral("Corrupt zip entry: ") + name);
        return out;
    }
    return fail(QStringLiteral("Not in the archive: ") + name);
}

} // namespace zip
