// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the legal notices, the embedded source archive and the small zip / inflate reader
// that gets the license text out of it.

#include "embedded.h"
#include "version.h"

#include <string.h>

namespace embedded {

const wchar_t* const kProductName = L"HourCalc";
const wchar_t* const kCopyright = L"Copyright (c) 2026 Optimey CommV";
const wchar_t* const kNotice =
    L"This program is free software: you can redistribute it and/or modify it under the terms of "
    L"the GNU General Public License as published by the Free Software Foundation, either version 3 "
    L"of the License, or (at your option) any later version. This program comes with ABSOLUTELY NO WARRANTY.";
const wchar_t* const kVersion = L"" HC_VER_STR;
#ifdef HC_NO_EMBEDDED_SOURCE
// A placeholder until the repository exists: the project page that holds the source code of this version.
const wchar_t* const kSourceUrl = L"https://github.com/Optimey-CommV/hourcalc";
#endif

std::wstring SourceFileName() {
    return std::wstring(kProductName) + L"-" + kVersion + L"-source.zip";
}

bool OpenSource(const uint8_t** data, size_t* size) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    DWORD n = SizeofResource(nullptr, r);
    if (!g || n < 22) return false;
    *data = static_cast<const uint8_t*>(LockResource(g));
    *size = n;
    return *data != nullptr;
}

bool SaveSource(const wchar_t* path) {
    const uint8_t* d;
    size_t n;
    if (!OpenSource(&d, &n)) return false;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, d, (DWORD)n, &written, nullptr);
    CloseHandle(h);
    return ok && written == n;
}

// ------------------------------------------------------------------ inflate (RFC 1951)

namespace {

struct InflateState {
    const uint8_t* in;
    size_t inLen, inPos;
    uint8_t* out;
    size_t outLen, outPos;
    uint32_t bitBuf;
    int bitCnt;
};

struct Huffman {
    uint16_t count[16];
    uint16_t symbol[288];
};

int Bits(InflateState* s, int need) {
    uint32_t val = s->bitBuf;
    while (s->bitCnt < need) {
        if (s->inPos >= s->inLen) return -1;
        val |= (uint32_t)s->in[s->inPos++] << s->bitCnt;
        s->bitCnt += 8;
    }
    s->bitBuf = val >> need;
    s->bitCnt -= need;
    return (int)(val & ((1u << need) - 1));
}

int Decode(InflateState* s, const Huffman* h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int b = Bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

int Construct(Huffman* h, const uint16_t* length, int n) {
    for (int len = 0; len <= 15; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (uint16_t)sym;
    return left;
}

const uint16_t kLens[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint16_t kLext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t kDists[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const uint16_t kDext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool Codes(InflateState* s, const Huffman* lc, const Huffman* dc) {
    for (;;) {
        int sym = Decode(s, lc);
        if (sym < 0) return false;
        if (sym < 256) {
            if (s->outPos >= s->outLen) return false;
            s->out[s->outPos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int eb = Bits(s, kLext[sym]);
            if (eb < 0) return false;
            int len = kLens[sym] + eb;
            int ds = Decode(s, dc);
            if (ds < 0 || ds >= 30) return false;
            eb = Bits(s, kDext[ds]);
            if (eb < 0) return false;
            size_t dist = (size_t)kDists[ds] + (size_t)eb;
            if (dist > s->outPos || s->outPos + (size_t)len > s->outLen) return false;
            for (int i = 0; i < len; i++, s->outPos++) s->out[s->outPos] = s->out[s->outPos - dist];
        }
    }
}

bool Fixed(InflateState* s) {
    Huffman lc, dc;
    uint16_t lengths[288];
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    Construct(&lc, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    Construct(&dc, lengths, 30);
    return Codes(s, &lc, &dc);
}

bool Dynamic(InflateState* s) {
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen = Bits(s, 5), ndist = Bits(s, 5), ncode = Bits(s, 4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return false;
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > 286 || ndist > 30) return false;
    uint16_t lengths[320];
    int index = 0;
    for (; index < ncode; index++) {
        int b = Bits(s, 3);
        if (b < 0) return false;
        lengths[order[index]] = (uint16_t)b;
    }
    for (; index < 19; index++) lengths[order[index]] = 0;
    Huffman lc, dc;
    if (Construct(&lc, lengths, 19) != 0) return false;
    index = 0;
    while (index < nlen + ndist) {
        int sym = Decode(s, &lc);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[index++] = (uint16_t)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (index == 0) return false;
                len = lengths[index - 1];
                rep = Bits(s, 2);
                if (rep < 0) return false;
                rep += 3;
            } else if (sym == 17) {
                rep = Bits(s, 3);
                if (rep < 0) return false;
                rep += 3;
            } else {
                rep = Bits(s, 7);
                if (rep < 0) return false;
                rep += 11;
            }
            if (index + rep > nlen + ndist) return false;
            while (rep--) lengths[index++] = (uint16_t)len;
        }
    }
    if (lengths[256] == 0) return false;
    int err = Construct(&lc, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return false;
    err = Construct(&dc, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return false;
    return Codes(s, &lc, &dc);
}

}  // namespace

bool Inflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
    InflateState s = { in, inLen, 0, out, outLen, 0, 0, 0 };
    int last;
    do {
        last = Bits(&s, 1);
        int type = Bits(&s, 2);
        if (last < 0 || type < 0) return false;
        if (type == 0) {
            s.bitBuf = 0;
            s.bitCnt = 0;
            if (s.inPos + 4 > s.inLen) return false;
            unsigned len = s.in[s.inPos] | (s.in[s.inPos + 1] << 8);
            unsigned nlen = s.in[s.inPos + 2] | (s.in[s.inPos + 3] << 8);
            s.inPos += 4;
            if (len != (~nlen & 0xFFFF) || s.inPos + len > s.inLen || s.outPos + len > s.outLen) return false;
            memcpy(s.out + s.outPos, s.in + s.inPos, len);
            s.inPos += len;
            s.outPos += len;
        } else if (type == 1) {
            if (!Fixed(&s)) return false;
        } else if (type == 2) {
            if (!Dynamic(&s)) return false;
        } else {
            return false;
        }
    } while (!last);
    return s.outPos == outLen;
}

// ------------------------------------------------------------------ zip

namespace {

uint32_t U16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// Finds the central directory: returns its offset and the number of entries.
bool Directory(const uint8_t* zip, size_t size, size_t* offset, size_t* count) {
    if (size < 22) return false;
    size_t i = size - 22;
    for (;; i--) {
        if (U32(zip + i) == 0x06054b50u) break;
        if (i == 0 || size - i > 22 + 65535) return false;
    }
    *count = U16(zip + i + 10);
    *offset = U32(zip + i + 16);
    return *offset < size;
}

}  // namespace

bool ZipNames(const uint8_t* zip, size_t size, std::vector<std::string>& names) {
    size_t off, count;
    if (!Directory(zip, size, &off, &count)) return false;
    for (size_t e = 0; e < count; e++) {
        if (off + 46 > size || U32(zip + off) != 0x02014b50u) return false;
        size_t nlen = U16(zip + off + 28), xlen = U16(zip + off + 30), clen = U16(zip + off + 32);
        if (off + 46 + nlen > size) return false;
        names.push_back(std::string(reinterpret_cast<const char*>(zip + off + 46), nlen));
        off += 46 + nlen + xlen + clen;
    }
    return true;
}

bool ZipRead(const uint8_t* zip, size_t size, const char* name, std::vector<uint8_t>& out) {
    size_t off, count;
    if (!Directory(zip, size, &off, &count)) return false;
    size_t want = strlen(name);
    for (size_t e = 0; e < count; e++) {
        if (off + 46 > size || U32(zip + off) != 0x02014b50u) return false;
        unsigned method = U16(zip + off + 10);
        size_t csize = U32(zip + off + 20), usize = U32(zip + off + 24);
        size_t nlen = U16(zip + off + 28), xlen = U16(zip + off + 30), clen = U16(zip + off + 32);
        size_t local = U32(zip + off + 42);
        if (off + 46 + nlen > size) return false;
        if (nlen == want && memcmp(zip + off + 46, name, want) == 0) {
            if (local + 30 > size || U32(zip + local) != 0x04034b50u) return false;
            size_t data = local + 30 + U16(zip + local + 26) + U16(zip + local + 28);
            if (data + csize > size) return false;
            out.resize(usize);
            if (method == 0) {
                if (csize != usize) return false;
                if (usize) memcpy(out.data(), zip + data, usize);
                return true;
            }
            if (method == 8) return Inflate(zip + data, csize, out.data(), usize);
            return false;
        }
        off += 46 + nlen + xlen + clen;
    }
    return false;
}

#ifdef HC_NO_EMBEDDED_SOURCE
// RCDATA 102: the size of LICENSE (uint32, little endian), then its raw deflate stream (build-app.ps1).
static bool LicenseBytes(std::vector<uint8_t>& raw) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(102), RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    DWORD n = SizeofResource(nullptr, r);
    if (!g || n < 5) return false;
    const uint8_t* p = static_cast<const uint8_t*>(LockResource(g));
    if (!p) return false;
    size_t size = U32(p);
    if (size == 0 || size > (1u << 22)) return false;
    raw.resize(size);
    return Inflate(p + 4, n - 4, raw.data(), size);
}
#else
static bool LicenseBytes(std::vector<uint8_t>& raw) {
    const uint8_t* zip;
    size_t size;
    return OpenSource(&zip, &size) && ZipRead(zip, size, "LICENSE", raw);
}
#endif

bool ReadLicense(std::wstring& text) {
    std::vector<uint8_t> raw;
    if (!LicenseBytes(raw) || raw.empty()) return false;
    int n = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(raw.data()), (int)raw.size(), nullptr, 0);
    if (n <= 0) return false;
    text.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(raw.data()), (int)raw.size(), &text[0], n);
    return true;
}

}  // namespace embedded
