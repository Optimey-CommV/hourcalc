// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// hc::Dec - System.Decimal on 32-bit limbs, the same code on every platform. The value is a 96-bit mantissa, a scale
// 0..28 and a sign (the System.Decimal layout, see hc_core.h). The arithmetic follows the runtime: results are exact when
// they fit; a result that needs more than 96 bits (or a scale above 28) loses its surplus digits with rounding to the
// nearest even digit, where a discarded remainder of exactly one half counts as more than half when anything below it was
// not zero (the "sticky" rule). One place where the runtime is not exact is copied as well: the rounding of a quotient whose
// divisor has more than 64 bits (RoundsUpWide). Everything works on plain arrays of 32-bit limbs, lowest limb first; the longest
// intermediate is a 96-bit mantissa times 10^28 (190 bits), so seven limbs are always enough.

#include "hc_core.h"

namespace hc {

namespace {

typedef uint32_t u32;
typedef uint64_t u64;

const u32 kPow10[10] = { 1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u, 1000000000u };
const u32 kSign = 0x80000000u;
const int kLimbs = 7;       // the longest intermediate
const int kMaxScale = 28;

// w = w * mul + add over n limbs; returns the carry out of the top limb.
u32 MulAdd(u32* w, int n, u32 mul, u32 add)
{
    u64 carry = add;
    for (int i = 0; i < n; ++i) {
        u64 cur = static_cast<u64>(w[i]) * mul + carry;
        w[i] = static_cast<u32>(cur);
        carry = cur >> 32;
    }
    return static_cast<u32>(carry);
}

// w /= d over n limbs (d != 0); returns the remainder.
u32 DivRem(u32* w, int n, u32 d)
{
    u64 rem = 0;
    for (int i = n - 1; i >= 0; --i) {
        u64 cur = (rem << 32) | w[i];
        w[i] = static_cast<u32>(cur / d);
        rem = cur % d;
    }
    return static_cast<u32>(rem);
}

// w += 1 over n limbs; true when it carried out of the top limb.
bool Inc(u32* w, int n)
{
    for (int i = 0; i < n; ++i)
        if (++w[i] != 0) return false;
    return true;
}

// Index of the highest limb that is not zero (0 for a zero).
int Top(const u32* w)
{
    int t = kLimbs - 1;
    while (t > 0 && w[t] == 0) --t;
    return t;
}

// -1, 0, 1 for a <=> b over n limbs.
int Cmp(const u32* a, const u32* b, int n)
{
    for (int i = n - 1; i >= 0; --i)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

// a (+/-)= b over the seven limbs. Subtraction needs a >= b.
void AddW(u32* a, const u32* b)
{
    u64 carry = 0;
    for (int i = 0; i < kLimbs; ++i) {
        u64 cur = static_cast<u64>(a[i]) + b[i] + carry;
        a[i] = static_cast<u32>(cur);
        carry = cur >> 32;
    }
}

void SubW(u32* a, const u32* b)
{
    u64 borrow = 0;
    for (int i = 0; i < kLimbs; ++i) {
        u64 cur = static_cast<u64>(a[i]) - b[i] - borrow;
        a[i] = static_cast<u32>(cur);
        borrow = (cur >> 32) & 1;
    }
}

// w *= 10^k, k = 0..28.
void ScaleUp(u32* w, int k)
{
    while (k > 0) {
        int step = k > 9 ? 9 : k;
        MulAdd(w, kLimbs, kPow10[step], 0);
        k -= step;
    }
}

// The mantissa of a decimal as a seven limb number.
void Load(u32* w, const u32* m)
{
    w[0] = m[0]; w[1] = m[1]; w[2] = m[2];
    w[3] = w[4] = w[5] = w[6] = 0;
}

// num (n limbs) / den (3 limbs, not zero): quotient into quo (n limbs), remainder into rem (3 limbs). A divisor of one limb
// is a plain short division; otherwise the bits are shifted in one by one (at most 128 steps: the quotients that are
// needed are short, the speed of the long divisions does not matter).
void DivMod(const u32* num, int n, const u32* den, u32* quo, u32* rem)
{
    if (den[1] == 0 && den[2] == 0) {
        for (int i = 0; i < n; ++i) quo[i] = num[i];
        rem[0] = DivRem(quo, n, den[0]);
        rem[1] = rem[2] = 0;
        return;
    }
    u32 r[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < n; ++i) quo[i] = 0;
    for (int bit = n * 32 - 1; bit >= 0; --bit) {
        u32 carry = (num[bit >> 5] >> (bit & 31)) & 1u;
        for (int j = 0; j < 4; ++j) {
            u32 next = r[j] >> 31;
            r[j] = (r[j] << 1) | carry;
            carry = next;
        }
        if (r[3] != 0 || Cmp(r, den, 3) >= 0) {
            u64 borrow = 0;
            for (int j = 0; j < 4; ++j) {
                u64 cur = static_cast<u64>(r[j]) - (j < 3 ? den[j] : 0u) - borrow;
                r[j] = static_cast<u32>(cur);
                borrow = (cur >> 32) & 1;
            }
            quo[bit >> 5] |= 1u << (bit & 31);
        }
    }
    rem[0] = r[0]; rem[1] = r[1]; rem[2] = r[2];
}

// The quotient state of a division: q = floor(N * 10^(scale - preferred) / D) and its remainder, one more
// k digits (k = 1..9): q' = q * 10^k + floor(rem * 10^k / D), rem' = rem * 10^k mod D. False (nothing changed)
// when q' no longer fits in 96 bits.
bool Step(u32* q, u32* rem, const u32* d, int k)
{
    u32 t[4] = { rem[0], rem[1], rem[2], 0 };
    MulAdd(t, 4, kPow10[k], 0);
    u32 dq[4], dr[3];
    DivMod(t, 4, d, dq, dr);                 // dq < 10^k: one limb
    u32 w[4] = { q[0], q[1], q[2], 0 };
    MulAdd(w, 4, kPow10[k], dq[0]);
    if (w[3] != 0) return false;
    q[0] = w[0]; q[1] = w[1]; q[2] = w[2];
    rem[0] = dr[0]; rem[1] = dr[1]; rem[2] = dr[2];
    return true;
}

// The rounding decision of a quotient whose divisor has more than 64 bits (its top limb is not zero): does the remainder
// 'rem' (3 limbs, below the divisor 'den') make the quotient round up? 'odd': the quotient is odd (a tie goes to the even one).
// The runtime takes this path in its own way: it shifts the divisor left until the top bit of its top limb is set (and the
// remainder with it), then compares twice the remainder with the divisor limb by limb from the top. When it doubles the
// remainder it loses the carry out of a low or middle limb that holds exactly 0x80000000: the doubled limb is then 0, and the
// carry is only seen for the other values with the top bit set. So twice a remainder with such a limb comes out smaller than it
// is: a quotient that is a hair above one half rounds down, and an exact half rounds down whether the quotient is odd or even.
// (Divisors of 64 bits or less have no such effect.) Not in any specification: found by experiments against the real decimal
// of .NET Framework 4.8 and checked on more than two million near-half divisions and on cases built from the limb values
// 0, 1, 2^31 - 1, 2^31, 2^31 + 1 and 2^32 - 1; see the "wide divisor" tests in tests/native/core_tests.cpp.
bool RoundsUpWide(const u32* rem, const u32* den, bool odd)
{
    int shift = 0;
    for (u32 v = den[2]; (v & kSign) == 0; v <<= 1) ++shift;           // den[2] is not zero
    u32 r[3] = { rem[0], rem[1], rem[2] }, d[3] = { den[0], den[1], den[2] };
    if (shift != 0) {
        d[2] = (d[2] << shift) | (d[1] >> (32 - shift));
        d[1] = (d[1] << shift) | (d[0] >> (32 - shift));
        d[0] <<= shift;
        r[2] = (r[2] << shift) | (r[1] >> (32 - shift));
        r[1] = (r[1] << shift) | (r[0] >> (32 - shift));
        r[0] <<= shift;
    }
    if ((r[2] & kSign) != 0) return true;                               // twice the remainder does not fit in 96 bits
    u32 carry0 = r[0] != kSign ? r[0] >> 31 : 0u;
    u32 carry1 = r[1] != kSign ? r[1] >> 31 : 0u;
    u32 t0 = r[0] << 1;
    u32 t1 = (r[1] << 1) | carry0;
    u32 t2 = (r[2] << 1) | carry1;
    if (t2 != d[2]) return t2 > d[2];
    if (t1 != d[1]) return t1 > d[1];
    if (t0 != d[0]) return t0 > d[0];
    return odd;
}

} // namespace

// ------------------------------------------------------------------ construction

Dec::Dec()
{
    m_[0] = m_[1] = m_[2] = 0;
    flags_ = 0;
}

Dec::Dec(long long v)
{
    unsigned long long a = v < 0 ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
    m_[0] = static_cast<u32>(a);
    m_[1] = static_cast<u32>(a >> 32);
    m_[2] = 0;
    flags_ = v < 0 ? kSign : 0;
}

Dec Dec::Make(const u32* mantissa, int scale, bool negative)
{
    Dec r;
    r.m_[0] = mantissa[0]; r.m_[1] = mantissa[1]; r.m_[2] = mantissa[2];
    r.flags_ = (static_cast<u32>(scale) << 16) | (negative ? kSign : 0u);
    return r;
}

// Brings an exact result w (any size) at 'scale' into 96 bits and a scale of at most 28, the way System.Decimal does it
// (its ScaleResult): the number of digits to drop is estimated from the size of w (a lower bound, the estimate can be
// one short), they are divided away nine at a time while the remainders are kept as a sticky bit; when the quotient still
// does not fit one more digit goes the same way, and only then the last remainder decides the rounding (nearest, ties to
// even, a tie with a sticky bit rounds up); when that rounding carries out of 96 bits one more digit goes as well.
// Overflow when the scale would drop below 0.
Dec Dec::Fit(u32* w, int scale, bool negative)
{
    int top = Top(w);
    int drop = 0;
    if (top > 2) {
        int bits = 0;
        for (u32 v = w[top]; v != 0; v >>= 1) ++bits;
        int excess = top * 32 + bits - 1 - 96;            // position of the top bit, minus 96
        drop = ((excess * 77) >> 8) + 1;                   // times log10(2), rounded down: a lower bound, plus one
    }
    if (scale - drop > kMaxScale) drop = scale - kMaxScale;
    if (drop != 0) {
        u32 sticky = 0, rem = 0, power = 1;
        for (;;) {
            scale -= drop;
            if (scale < 0) throw DecOverflow();
            for (;;) {
                sticky |= rem;
                int step = drop > 9 ? 9 : drop;
                power = kPow10[step];
                rem = DivRem(w, top + 1, power);
                drop -= step;
                if (drop == 0) break;
            }
            top = Top(w);
            if (top > 2) { drop = 1; continue; }           // not enough: one more digit before anything is rounded
            u32 half = power >> 1;
            if (rem >= half && (rem > half || ((w[0] & 1u) | sticky) != 0)) {
                Inc(w, kLimbs);
                top = Top(w);
                if (top > 2) { drop = 1; continue; }        // the rounding carried out of 96 bits
            }
            break;
        }
    }
    return Make(w, scale, negative);
}

Dec Dec::Parse(const std::wstring& s)
{
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == L'-' || s[i] == L'+')) {
        neg = s[i] == L'-';
        ++i;
    }
    u32 m[3] = { 0, 0, 0 };
    int digits = 0;        // digits seen (integer and fraction)
    int scale = 0;         // fraction digits kept
    bool dot = false;
    bool full = false;     // a fraction digit no longer fit: the rest is only rounded in
    bool roundUp = false;
    for (; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'.' && !dot) { dot = true; continue; }
        if (c < L'0' || c > L'9') throw DecFormat();
        ++digits;
        u32 d = static_cast<u32>(c - L'0');
        if (full) continue;
        if (dot && scale >= kMaxScale) {
            // More than 28 fraction digits: System.Decimal rounds the 29th away.
            full = true;
            roundUp = d >= 5;
            continue;
        }
        u32 t[3] = { m[0], m[1], m[2] };
        if (MulAdd(t, 3, 10, d) != 0) {
            if (!dot) throw DecOverflow();
            full = true;
            roundUp = d >= 5;
            continue;
        }
        m[0] = t[0]; m[1] = t[1]; m[2] = t[2];
        if (dot) ++scale;
    }
    if (digits == 0) throw DecFormat();
    if (roundUp && Inc(m, 3)) throw DecOverflow();
    return Make(m, scale, neg);
}

std::wstring Dec::ToInvariant() const
{
    // The decimal digits of the mantissa, nine at a time from the low end.
    u32 m[3] = { m_[0], m_[1], m_[2] };
    wchar_t buf[40];
    int pos = 40;
    while ((m[0] | m[1] | m[2]) != 0) {
        u32 chunk = DivRem(m, 3, 1000000000u);
        for (int i = 0; i < 9; ++i) {
            buf[--pos] = static_cast<wchar_t>(L'0' + chunk % 10);
            chunk /= 10;
        }
    }
    while (pos < 39 && buf[pos] == L'0') ++pos;
    if (pos == 40) buf[--pos] = L'0';
    std::wstring digits(buf + pos, static_cast<size_t>(40 - pos));
    int scale = Scale();
    std::wstring out;
    if ((flags_ & kSign) != 0 && !IsZero()) out += L'-';
    if (scale == 0) {
        out += digits;
        return out;
    }
    if (static_cast<int>(digits.size()) <= scale)
        digits.insert(0, static_cast<size_t>(scale + 1 - static_cast<int>(digits.size())), L'0');
    out.append(digits, 0, digits.size() - static_cast<size_t>(scale));
    out += L'.';
    out.append(digits, digits.size() - static_cast<size_t>(scale), std::wstring::npos);
    return out;
}

// ------------------------------------------------------------------ arithmetic

// Both operands brought to the larger scale (the smaller one times a power of 10), added or subtracted exactly in seven
// limbs, then fitted into 96 bits. The result has the larger scale; a zero keeps the sign of the left operand when the
// operands cancel (it prints without a sign anyway).
Dec Dec::AddSub(const Dec& o, bool subtract) const
{
    bool sa = (flags_ & kSign) != 0;
    bool sb = ((o.flags_ & kSign) != 0) != subtract;
    int s1 = Scale(), s2 = o.Scale();
    u32 x[kLimbs], y[kLimbs];
    Load(x, m_);
    Load(y, o.m_);
    int scale = s1;
    if (s1 < s2) { scale = s2; ScaleUp(x, s2 - s1); }
    else if (s2 < s1) ScaleUp(y, s1 - s2);
    bool neg = sa;
    if (sa == sb) {
        AddW(x, y);
    } else if (Cmp(x, y, kLimbs) >= 0) {
        SubW(x, y);
    } else {
        SubW(y, x);
        for (int i = 0; i < kLimbs; ++i) x[i] = y[i];
        neg = sb;
    }
    return Fit(x, scale, neg);
}

Dec Dec::operator+(const Dec& o) const { return AddSub(o, false); }
Dec Dec::operator-(const Dec& o) const { return AddSub(o, true); }

Dec Dec::operator*(const Dec& o) const
{
    u32 p[kLimbs] = { 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 3; ++i) {
        u64 carry = 0;
        for (int j = 0; j < 3; ++j) {
            u64 cur = static_cast<u64>(m_[i]) * o.m_[j] + p[i + j] + carry;
            p[i + j] = static_cast<u32>(cur);
            carry = cur >> 32;
        }
        p[i + 3] = static_cast<u32>(carry);
    }
    // The runtime has two paths. Two mantissas of 32 bits or less (a product of at most 64 bits) keep the scale of a zero
    // product (0.00m * 5m is 0.00) but give a plain 0 when 20 or more digits have to be dropped; larger operands give a plain 0
    // for a zero product. A non-zero product that rounds to zero keeps scale 28.
    int scale = Scale() + o.Scale();
    if ((m_[1] | m_[2] | o.m_[1] | o.m_[2]) == 0) {
        if (scale - kMaxScale >= 20) return Dec();
    } else if (Top(p) == 0 && p[0] == 0) {
        return Dec();
    }
    return Fit(p, scale, ((flags_ ^ o.flags_) & kSign) != 0);
}

// The quotient starts as the whole number N / D at the preferred scale p = (scale of N) - (scale of D), which can be
// negative. While a remainder is left the quotient grows by up to nine digits at a time (rem * 10^k / D), as far as the
// scale 28 and the 96 bits allow; when a step of k digits does not fit, k - 1 are tried, so the quotient ends at the
// largest scale at which it fits. What remains is rounded to even (twice the remainder against the divisor; with a divisor
// of more than 64 bits the way the runtime does it, see RoundsUpWide). A quotient
// that needed a remainder is stripped of its trailing zeros (down to scale 0); an exact one keeps its scale, which is
// at least p, and when p is negative the quotient is scaled up to scale 0 (an overflow if that does not fit).
Dec Dec::operator/(const Dec& o) const
{
    if (o.IsZero()) throw DecDivideByZero();
    int scale = Scale() - o.Scale();
    u32 q[3], rem[3];
    DivMod(m_, 3, o.m_, q, rem);
    bool inexact = false;
    for (;;) {
        bool exact = (rem[0] | rem[1] | rem[2]) == 0;
        if (!exact) inexact = true;
        int want = exact ? -scale : kMaxScale - scale;
        if (want > 9) want = 9;
        if (want <= 0) break;
        int k = want;
        while (k > 0 && !Step(q, rem, o.m_, k)) --k;
        if (k == 0) break;
        scale += k;
    }
    if (scale < 0) throw DecOverflow();
    if ((rem[0] | rem[1] | rem[2]) != 0) {
        bool up;
        if (o.m_[2] != 0) {
            up = RoundsUpWide(rem, o.m_, (q[0] & 1u) != 0);
        } else {
            u32 twice[4] = { rem[0] << 1, (rem[1] << 1) | (rem[0] >> 31), (rem[2] << 1) | (rem[1] >> 31), rem[2] >> 31 };
            u32 d[4] = { o.m_[0], o.m_[1], o.m_[2], 0 };
            int c = Cmp(twice, d, 4);
            up = c > 0 || (c == 0 && (q[0] & 1u) != 0);
        }
        if (up && Inc(q, 3)) {
            // The rounding carried out of 96 bits (the mantissa was 2^96 - 1): one digit less, 2^96 / 10 rounded, which is
            // what rounding the exact quotient at the smaller scale gives too.
            if (scale == 0) throw DecOverflow();
            q[0] = 0x9999999Au; q[1] = 0x99999999u; q[2] = 0x19999999u;
            --scale;
        }
    }
    if (inexact) {
        while (scale > 0) {
            u32 t[3] = { q[0], q[1], q[2] };
            if (DivRem(t, 3, 10) != 0) break;
            q[0] = t[0]; q[1] = t[1]; q[2] = t[2];
            --scale;
        }
    }
    return Make(q, scale, ((flags_ ^ o.flags_) & kSign) != 0);
}

Dec Dec::operator-() const
{
    Dec r = *this;
    r.flags_ ^= kSign;
    return r;
}

// Exact comparison of the values (a zero has no sign), without any overflow.
int Dec::Compare(const Dec& o) const
{
    bool za = IsZero(), zb = o.IsZero();
    if (za && zb) return 0;
    bool na = !za && (flags_ & kSign) != 0, nb = !zb && (o.flags_ & kSign) != 0;
    if (za) return nb ? 1 : -1;
    if (zb) return na ? -1 : 1;
    if (na != nb) return na ? -1 : 1;
    u32 x[kLimbs], y[kLimbs];
    Load(x, m_);
    Load(y, o.m_);
    int s1 = Scale(), s2 = o.Scale();
    if (s1 < s2) ScaleUp(x, s2 - s1);
    else if (s2 < s1) ScaleUp(y, s1 - s2);
    int c = Cmp(x, y, kLimbs);
    return na ? -c : c;
}

bool Dec::operator==(const Dec& o) const { return Compare(o) == 0; }
bool Dec::operator!=(const Dec& o) const { return Compare(o) != 0; }
bool Dec::operator<(const Dec& o) const { return Compare(o) < 0; }
bool Dec::operator<=(const Dec& o) const { return Compare(o) <= 0; }
bool Dec::operator>(const Dec& o) const { return Compare(o) > 0; }
bool Dec::operator>=(const Dec& o) const { return Compare(o) >= 0; }

bool Dec::IsZero() const
{
    return (m_[0] | m_[1] | m_[2]) == 0;
}

bool Dec::IsNegative() const
{
    return (flags_ & kSign) != 0 && !IsZero();
}

Dec Dec::Abs() const
{
    Dec r = *this;
    r.flags_ &= ~kSign;
    return r;
}

// decimal.Truncate: the fraction digits are cut off and the scale is 0.
Dec Dec::Truncate() const
{
    u32 m[3] = { m_[0], m_[1], m_[2] };
    for (int s = Scale(); s > 0;) {
        int chunk = s > 9 ? 9 : s;
        DivRem(m, 3, kPow10[chunk]);
        s -= chunk;
    }
    return Make(m, 0, (flags_ & kSign) != 0);
}

// Math.Round(value, places, MidpointRounding.AwayFromZero): a value that already has no more than
// 'places' fraction digits is returned unchanged (its scale stays); otherwise the surplus digits are
// divided away and the result has exactly 'places' fraction digits.
Dec Dec::RoundAwayFromZero(int places) const
{
    if (places < 0) places = 0;
    if (places > kMaxScale) places = kMaxScale;
    int diff = Scale() - places;
    if (diff <= 0) return *this;
    u32 m[3] = { m_[0], m_[1], m_[2] };
    u32 lastRem = 0, lastDiv = 1;
    while (diff > 0) {
        int chunk = diff > 9 ? 9 : diff;
        lastDiv = kPow10[chunk];
        lastRem = DivRem(m, 3, lastDiv);
        diff -= chunk;
    }
    if (lastRem >= lastDiv / 2) Inc(m, 3);   // the quotient is at least 10 times smaller: no overflow
    return Make(m, places, (flags_ & kSign) != 0);
}

// Math.Round(value, places): midpoint to the nearest even digit.
Dec Dec::RoundToEven(int places) const
{
    if (places < 0) places = 0;
    if (places > kMaxScale) places = kMaxScale;
    int diff = Scale() - places;
    if (diff <= 0) return *this;
    u32 m[3] = { m_[0], m_[1], m_[2] };
    u32 lastRem = 0, lastDiv = 1;
    bool sticky = false;   // a non-zero digit below the last chunk
    while (diff > 0) {
        int chunk = diff > 9 ? 9 : diff;
        if (lastRem != 0) sticky = true;
        lastDiv = kPow10[chunk];
        lastRem = DivRem(m, 3, lastDiv);
        diff -= chunk;
    }
    u32 half = lastDiv / 2;
    bool up;
    if (lastRem > half) up = true;
    else if (lastRem < half) up = false;
    else up = sticky || (m[0] & 1u) != 0;
    if (up) Inc(m, 3);
    return Make(m, places, (flags_ & kSign) != 0);
}

// (long)value: truncates toward zero; OverflowException in the reference when it does not fit.
long long Dec::ToInt64() const
{
    u32 m[3] = { m_[0], m_[1], m_[2] };
    for (int s = Scale(); s > 0;) {
        int chunk = s > 9 ? 9 : s;
        DivRem(m, 3, kPow10[chunk]);
        s -= chunk;
    }
    if (m[2] != 0) throw DecOverflow();
    unsigned long long v = (static_cast<unsigned long long>(m[1]) << 32) | m[0];
    if (flags_ & kSign) {
        if (v > 0x8000000000000000ull) throw DecOverflow();
        return static_cast<long long>(0ull - v);
    }
    if (v > 0x7FFFFFFFFFFFFFFFull) throw DecOverflow();
    return static_cast<long long>(v);
}

} // namespace hc
