/*
 * primes.c - GPrimes64, slim prime generator for Windows x64
 * ==================================================================
 * A single native EXE, pure C (C11), no external libraries other
 * than the Windows API (KERNEL32).
 *
 * Methods (selectable via -m/--method):
 *   sieve     Segmented sieve of Eratosthenes (default)
 *   atkin     Sieve of Atkin
 *   sundaram  Sieve of Sundaram
 *   trial     Trial division (6k +/- 1)
 *   miller    Miller-Rabin (deterministic <=64-bit, probabilistic beyond)
 *
 * Output:
 *   -t/--time       ASCII table: prime | compute time (ms)
 *   -j/--threads N  Multithreading (0/--mt = all cores)
 *   -q/--quiet      Only the summary (suppresses the live line)
 *   Always: summary on stderr.
 *
 * Live line (stderr): CPU per thread (color-coded) + RAM. From about 10%
 *   progress additionally: percent / elapsed time / extrapolated total
 *   duration. The extrapolation is recomputed every 5% and colored
 *   according to the trend: red (> previous), green (< previous) or
 *   white (first / within +/-5%).
 *
 * Color-coded threads (ANSI 256):
 *   Each thread gets a color as far apart as possible; grayscale tones
 *   are excluded. A single thread is white. In the live line the
 *   label/value, in the table the vertical bars are colored.
 *
 * Versioning: X.Y.Z
 *   X = full code rewrite, Y = major features, Z = hotfixes
 *
 * Build: see build.bat
 */

#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0601
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <windows.h>
#include <process.h>
#include <intrin.h>
#include <psapi.h>

typedef uint64_t u64;
typedef uint8_t  u8;

#define PRIMES_VERSION "3.2.2"

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

/* Extrapolation colors (ANSI 256) */
#define COL_RED   196
#define COL_GREEN  46
#define COL_WHITE  15

/* Table colors only on a real console (or via PRIMES_COLOR=1). */
static int g_table_color = 0;

/* ==================================================================
 *  Timing (high resolution)
 * ================================================================== */
static double qpc_ms(void)
{
    static LARGE_INTEGER freq;
    static int init = 0;
    LARGE_INTEGER c;
    if (!init) { QueryPerformanceFrequency(&freq); init = 1; }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static void fmt_hms(double sec, char *buf, size_t n)
{
    if (sec < 0) sec = 0;
    if (sec > 359999.0) sec = 359999.0;          /* max 99:59:59 */
    u64 s = (u64)(sec + 0.5);
    snprintf(buf, n, "%02llu:%02llu:%02llu",
             (unsigned long long)(s / 3600),
             (unsigned long long)((s % 3600) / 60),
             (unsigned long long)(s % 60));
}

/* Unit suffix matching the magnitude. */
static const char *time_unit(double sec)
{
    if (sec < 60.0)   return "sec";
    if (sec < 3600.0) return "min";
    return "h";
}

/* ==================================================================
 *  ANSI 256 colors for threads
 * ================================================================== */
static const int LVL[6] = { 0, 95, 135, 175, 215, 255 };

typedef struct { int idx, r, g, b; } Cand;
static Cand g_cand[216];
static int  g_ncand = -1;

static void build_candidates(void)
{
    if (g_ncand >= 0) return;
    int n = 0;
    for (int i = 0; i < 216; i++) {
        int r = i / 36, g = (i / 6) % 6, b = i % 6;
        if (r == g && g == b) continue;          /* grayscale -> forbidden */
        g_cand[n].idx = 16 + i;
        g_cand[n].r = LVL[r]; g_cand[n].g = LVL[g]; g_cand[n].b = LVL[b];
        n++;
    }
    g_ncand = n;
}

static int dist2(const Cand *a, const Cand *b)
{
    int dr = a->r - b->r, dg = a->g - b->g, db = a->b - b->b;
    return dr * dr + dg * dg + db * db;
}

static void assign_thread_colors(int n, u8 *colors)
{
    if (n <= 0) return;
    if (n == 1) { colors[0] = COL_WHITE; return; }   /* exception: white */

    build_candidates();
    int m = g_ncand;

    int *sel  = (int *)malloc((size_t)n * sizeof(int));
    int *mind = (int *)malloc((size_t)m * sizeof(int));
    char *used = (char *)calloc((size_t)m, 1);
    if (!sel || !mind || !used) {
        for (int i = 0; i < n; i++) colors[i] = COL_WHITE;
        free(sel); free(mind); free(used);
        return;
    }

    int s = 0;
    for (int i = 0; i < m; i++) if (g_cand[i].idx == 196) { s = i; break; }
    sel[0] = s; used[s] = 1;
    for (int i = 0; i < m; i++) mind[i] = dist2(&g_cand[i], &g_cand[s]);

    for (int k = 1; k < n; k++) {
        int best = -1, bd = -1;
        for (int i = 0; i < m; i++)
            if (!used[i] && mind[i] > bd) { bd = mind[i]; best = i; }
        if (best < 0) best = k % m;
        sel[k] = best; used[best] = 1;
        for (int i = 0; i < m; i++)
            if (!used[i]) {
                int d = dist2(&g_cand[i], &g_cand[best]);
                if (d < mind[i]) mind[i] = d;
            }
    }
    for (int k = 0; k < n; k++) colors[k] = (u8)g_cand[sel[k]].idx;

    free(sel); free(mind); free(used);
}

/* ==================================================================
 *  Status line (live line) - shared by monitor and output
 * ================================================================== */
static CRITICAL_SECTION g_status_cs;
static int  g_status_coord = 0;      /* output must preserve the status line */
static char g_status_line[8192];
static int  g_status_len   = 0;      /* content in g_status_line (bytes)     */
static int  g_status_drawn = 0;      /* last written character count         */
static char g_outbuf[262144];        /* buffered prime output                */
static int  g_outlen = 0;

/* Flush the buffered output. The status line is briefly removed and
 * (optionally) restored. Used only ~10x/s or when the buffer is full so the
 * live line does not flicker. */
static void out_flush(int reprint_status)
{
    if (g_outlen > 0) {
        if (g_status_drawn > 0) { fputs("\r\x1b[K", stderr); g_status_drawn = 0; }
        fwrite(g_outbuf, 1, (size_t)g_outlen, stdout);
        fflush(stdout);
        g_outlen = 0;
    }
    if (reprint_status && g_status_len > 0) {
        fwrite(g_status_line, 1, (size_t)g_status_len, stderr);
        g_status_drawn = g_status_len;
        fflush(stderr);
    }
}

/* Write the status line in place (the caller holds the CS).
 * No line clearing: only overwrite and pad for shorter text -> no flicker. */
static void status_render(const char *line, int len)
{
    fputc('\r', stderr);
    fwrite(line, 1, (size_t)len, stderr);
    for (int i = len; i < g_status_drawn; i++) fputc(' ', stderr);
    if (len > g_status_drawn) g_status_drawn = len;
    fflush(stderr);
}

/* Remove the status line and flush the buffer. */
static void status_finish(void)
{
    EnterCriticalSection(&g_status_cs);
    out_flush(0);
    if (g_status_drawn > 0) {
        fputc('\r', stderr);
        for (int i = 0; i < g_status_drawn; i++) fputc(' ', stderr);
        fputc('\r', stderr);
        fflush(stderr);
    }
    g_status_drawn = 0;
    g_status_len = 0;
    LeaveCriticalSection(&g_status_cs);
}

/* Buffer a prime line; the actual output happens batched in out_flush(). */
static void print_prime(u64 p)
{
    if (!g_status_coord) {
        printf("%llu\n", (unsigned long long)p);
        return;
    }
    EnterCriticalSection(&g_status_cs);
    int n = snprintf(g_outbuf + g_outlen, sizeof(g_outbuf) - (size_t)g_outlen,
                     "%llu\n", (unsigned long long)p);
    if (n > 0) g_outlen += n;
    if (g_outlen > (int)sizeof(g_outbuf) - 64) out_flush(1);
    LeaveCriticalSection(&g_status_cs);
}

/* ==================================================================
 *  Common helpers
 * ================================================================== */
static u64 isqrt_u64(u64 n)
{
    u64 res = 0;
    u64 bit = (u64)1 << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}

static void bit_set   (u8 *b, u64 i) { b[i >> 3] |=  (u8)(1u << (i & 7)); }
static void bit_clear (u8 *b, u64 i) { b[i >> 3] &= (u8)~(1u << (i & 7)); }
static int  bit_get   (const u8 *b, u64 i) { return (b[i >> 3] >> (i & 7)) & 1; }
static void bit_toggle(u8 *b, u64 i) { b[i >> 3] ^=  (u8)(1u << (i & 7)); }

#define MAX_SIEVE_BYTES ((size_t)1 << 30)   /* 1 GiB */

static void check_sieve_size(u64 high, const char *method, size_t nbytes)
{
    if (nbytes > MAX_SIEVE_BYTES) {
        fprintf(stderr,
            "Error: method '%s' requires a sieve up to %llu (%llu MB) "
            "and exceeds the limit of %llu MB.\n"
            "       For large upper bounds or ranges use "
            "-m sieve, -m trial or -m miller.\n",
            method, (unsigned long long)high,
            (unsigned long long)(nbytes >> 20),
            (unsigned long long)(MAX_SIEVE_BYTES >> 20));
        exit(1);
    }
}

/* ==================================================================
 *  Output stream
 * ================================================================== */
typedef int (*EmitFn)(u64 prime, void *ctx);   /* return != 0 => abort */

typedef struct {
    int    print;        /* output primes directly                        */
    int    measure;      /* measure per-prime time (for the table)        */
    int    table;        /* output results as an ASCII table              */
    int    buffered;     /* worker buffer (multithreading)                */
    int    count_only;   /* count only, do not buffer primes              */
    u64    stop_at;      /* stop after this many hits (0 = off)           */
    u64    found;
    double last_ms;
    u8     cur_color;
    u64    *bp;
    double *bt;
    u8     *bc;
    size_t  len, cap;
    /* progress for the live line */
    u64          prog_span;
    volatile u64 prog_done;
    double       pending_extra;   /* marking share for the next prime */
} StreamCtx;

static void buf_push(StreamCtx *s, u64 p, double ms)
{
    if (s->len == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 4096;
        u64    *np = (u64 *)realloc(s->bp, nc * sizeof(u64));
        double *nt = s->bt;
        u8     *nb = s->bc;
        if (s->measure) nt = (double *)realloc(s->bt, nc * sizeof(double));
        if (s->table)   nb = (u8 *)realloc(s->bc, nc * sizeof(u8));
        if (!np || (s->measure && !nt) || (s->table && !nb)) {
            fprintf(stderr, "Error: out of memory.\n");
            exit(1);
        }
        s->bp = np; s->bt = nt; s->bc = nb; s->cap = nc;
    }
    s->bp[s->len] = p;
    if (s->measure) s->bt[s->len] = ms;
    if (s->table)   s->bc[s->len] = s->cur_color;
    s->len++;
}

static int emit_final(u64 p, double ms, StreamCtx *s)
{
    if (s->table)
        buf_push(s, p, ms);
    else if (s->print)
        print_prime(p);
    s->found++;
    if (s->stop_at && s->found >= s->stop_at)
        return 1;
    return 0;
}

static int stream_emit(u64 p, void *ctx)
{
    StreamCtx *s = (StreamCtx *)ctx;
    double ms = 0.0;
    if (s->measure) {
        double t = qpc_ms();
        ms = t - s->last_ms + s->pending_extra;
        s->pending_extra = 0.0;
    }
    int r;
    if (s->buffered) {
        if (s->count_only) { s->found++; r = 0; }
        else { buf_push(s, p, ms); r = 0; }
    } else {
        r = emit_final(p, ms, s);
    }
    /* Do not charge output/buffer time (e.g. realloc) to the next prime. */
    if (s->measure) s->last_ms = qpc_ms();
    return r;
}

/* ==================================================================
 *  ASCII table: prime | compute time (colored bars)
 * ================================================================== */
static int digit_count(u64 v) { int d = 1; while (v >= 10) { v /= 10; d++; } return d; }

static void print_rule(int w1, int w2)
{
    putchar('+');
    for (int i = 0; i < w1 + 2; i++) putchar('-');
    putchar('+');
    for (int i = 0; i < w2 + 2; i++) putchar('-');
    putchar('+');
    putchar('\n');
}

static void print_table(const StreamCtx *s)
{
    const char *h1 = "Prime";
    const char *h2 = "Compute time";
    int w1 = (int)strlen(h1);
    int w2 = (int)strlen(h2);
    char b[64];

    for (size_t i = 0; i < s->len; i++) {
        int d = digit_count(s->bp[i]);
        if (d > w1) w1 = d;
        int n = snprintf(b, sizeof b, "%.3f ms", s->bt[i]);
        if (n > w2) w2 = n;
    }

    print_rule(w1, w2);
    printf("| %-*s | %-*s |\n", w1, h1, w2, h2);
    print_rule(w1, w2);
    for (size_t i = 0; i < s->len; i++) {
        char p[32];
        snprintf(p, sizeof p, "%llu", (unsigned long long)s->bp[i]);
        snprintf(b, sizeof b, "%.3f ms", s->bt[i]);
        if (g_table_color) {
            int c = s->bc[i];
            printf("\x1b[38;5;%dm|\x1b[0m %*s \x1b[38;5;%dm|\x1b[0m %*s \x1b[38;5;%dm|\x1b[0m\n",
                   c, w1, p, c, w2, b, c);
        } else {
            printf("| %*s | %*s |\n", w1, p, w2, b);
        }
    }
    print_rule(w1, w2);
    fflush(stdout);
}

/* ==================================================================
 *  Method 1: segmented sieve of Eratosthenes (default)
 * ================================================================== */
typedef struct { u8 *bits; size_t n; } Sieve;

static int  sb_test (const Sieve *s, size_t k) { return (s->bits[k >> 3] >> (k & 7)) & 1; }
static void sb_clear(      Sieve *s, size_t k) { s->bits[k >> 3] &= (u8)~(1u << (k & 7)); }

/* Stride weight of a marking step: the cost of a write grows with the stride
 * (cache-line effect) and saturates at ~512 (64 bytes). */
static u64 stride_weight(u64 p) { return (p < 512) ? p : 512; }

static Sieve sieve_build(u64 limit, StreamCtx *sc, u64 prog_cap)
{
    Sieve s; s.bits = NULL; s.n = 0;
    if (limit < 3) return s;
    size_t n = (size_t)((limit - 1) / 2);
    size_t bytes = (n + 7) / 8;
    s.bits = (u8 *)malloc(bytes);
    s.n = n;
    if (!s.bits) { fprintf(stderr, "Error: out of memory.\n"); exit(1); }
    memset(s.bits, 0xFF, bytes);
    if (n & 7) s.bits[bytes - 1] &= (u8)((1u << (n & 7)) - 1);

    u64 work = 0;                    /* stride-weighted marking work */
    for (size_t k = 0; k < n; k++) {
        u64 p = 2 * (u64)k + 3;
        if (p > limit / p) break;
        if (!sb_test(&s, k)) continue;
        u64 start = p * p, step = 2 * p;
        u64 cnt = (limit - start) / step + 1;
        u64 w = stride_weight(p);
        u64 j = 0;
        for (u64 m = start; m <= limit; m += step, j++) {
            sb_clear(&s, (size_t)((m - 3) / 2));
            if (sc && (j & 0x3FFF) == 0) {
                u64 d = work + j * w;
                sc->prog_done = (d > prog_cap) ? prog_cap : d;
            }
        }
        work += cnt * w;
    }
    if (sc) sc->prog_done = (work > prog_cap) ? prog_cap : work;
    return s;
}

static u64 *sieve_primes(const Sieve *s, u64 limit, size_t *cnt_out)
{
    size_t cnt = limit >= 2 ? 1 : 0;
    for (size_t k = 0; k < s->n; k++) if (sb_test(s, k)) cnt++;
    u64 *arr = (u64 *)malloc((cnt ? cnt : 1) * sizeof(u64));
    if (!arr) { fprintf(stderr, "Error: out of memory.\n"); exit(1); }
    size_t i = 0;
    if (limit >= 2) arr[i++] = 2;
    for (size_t k = 0; k < s->n; k++) if (sb_test(s, k)) arr[i++] = 2 * (u64)k + 3;
    *cnt_out = cnt;
    return arr;
}

/* Exact, stride-weighted total work of the base-prime sieve up to sqrt(root)
 * (identical weighting as in sieve_build -> exact normalization). */
static u64 base_work_total(u64 root)
{
    if (root < 9) return 0;
    u64 sr = isqrt_u64(root);
    if (sr < 3) return 0;
    Sieve s = sieve_build(sr, NULL, 0);
    size_t np = 0;
    u64 *pr = sieve_primes(&s, sr, &np);
    free(s.bits);
    u64 work = 0;
    for (size_t i = 0; i < np; i++) {
        u64 p = pr[i];
        if (p < 3) continue;
        if (p > root / p) break;
        u64 start = p * p, step = 2 * p;
        u64 cnt = (root - start) / step + 1;
        work += cnt * stride_weight(p);
    }
    free(pr);
    return work;
}

static void method_eratosthenes(u64 low, u64 high, EmitFn emit, void *ctx)
{
    StreamCtx *sc = (StreamCtx *)ctx;
    sc->prog_span = high - low + 1;
    sc->prog_done = 0;
    if (high < 2 || low > high) return;
    if (low <= 2 && high >= 2) if (emit(2, ctx)) return;

    u64 start = low < 3 ? 3 : low;
    if ((start & 1) == 0) start++;
    if (start > high) return;

    u64 root = isqrt_u64(high);
    u64 range = high - low + 1;

    /* Building the base primes (sieve up to sqrt(high)) dominates the runtime
     * for large high and a small range. Its stride-weighted work is computed
     * exactly and used as the progress baseline. */
    u64 prog_cap = base_work_total(root);
    sc->prog_span = prog_cap + range;

    size_t np = 0; u64 *bp = NULL;
    if (root >= 3) {
        Sieve base = sieve_build(root, sc, prog_cap);
        bp = sieve_primes(&base, root, &np);
        free(base.bits);
    }
    sc->prog_done = prog_cap;

    /* Adaptive segment: many small blocks for fine progress, but not too
     * small (overhead of the base primes). */
    u64 SEG = range / 256;
    if (SEG < ((u64)1 << 14)) SEG = (u64)1 << 14;
    if (SEG > ((u64)1 << 22)) SEG = (u64)1 << 22;
    if (SEG < 2) SEG = 2;
    if (SEG & 1) SEG++;              /* keep even: segLow stays odd */

    size_t cap = (size_t)(SEG / 2) + 2;
    u8 *flags = (u8 *)malloc(cap);
    if (!flags) { free(bp); fprintf(stderr, "Error: out of memory.\n"); exit(1); }

    for (u64 segLow = start; segLow <= high; segLow += SEG) {
        u64 segHigh = segLow + SEG - 1;
        if (segHigh < segLow || segHigh > high) segHigh = high;
        size_t n = (size_t)((segHigh - segLow) / 2 + 1);
        memset(flags, 1, n);

        double mark_t0 = sc->measure ? qpc_ms() : 0.0;
        for (size_t i = 0; i < np; i++) {
            u64 p = bp[i];
            if (p < 3) continue;
            if (p > root) break;
            u64 m = ((segLow + p - 1) / p) * p;
            if (m < p * p) m = p * p;
            if ((m & 1) == 0) m += p;
            for (u64 x = m; x <= segHigh; x += 2 * p)
                flags[(size_t)((x - segLow) / 2)] = 0;
        }
        double mark_ms = sc->measure ? (qpc_ms() - mark_t0) : 0.0;

        /* Start scan-time measurement without the (batched) marking and
         * distribute the marking time proportionally to the candidate gap
         * across the primes found. */
        u64 seg_span = segHigh - segLow + 1;
        u64 prev = segLow;
        if (sc->measure) sc->last_ms = qpc_ms();

        for (u64 x = segLow; x <= segHigh; x += 2) {
            if (flags[(size_t)((x - segLow) / 2)]) {
                if (sc->measure) {
                    u64 gap = x - prev;
                    sc->pending_extra = mark_ms * (double)gap / (double)seg_span;
                }
                if (emit(x, ctx)) { free(flags); free(bp); return; }
                prev = x;
            }
            if ((x & 0xFFF) == 0) sc->prog_done = prog_cap + (x - low + 1);
        }
        sc->prog_done = prog_cap + (segHigh - low + 1);
    }
    sc->prog_done = prog_cap + range;
    free(flags); free(bp);
}

/* ==================================================================
 *  Method 2: sieve of Atkin (progress roughly weighted)
 * ================================================================== */
static void method_atkin(u64 low, u64 high, EmitFn emit, void *ctx)
{
    StreamCtx *sc = (StreamCtx *)ctx;
    sc->prog_span = high + 1;
    sc->prog_done = 0;
    if (high < 2 || low > high) return;
    if (high > ((u64)1 << 58)) { fprintf(stderr, "Error: Atkin upper bound too large.\n"); exit(1); }

    u64 lim = high;
    size_t nbytes = (size_t)((lim + 8) / 8);
    check_sieve_size(high, "atkin", nbytes);
    u8 *b = (u8 *)calloc(nbytes, 1);
    if (!b) { fprintf(stderr, "Error: out of memory.\n"); exit(1); }

    u64 root = isqrt_u64(lim);
    double span = (double)(high + 1);

    for (u64 x = 1; x <= lim / x; x++) {
        u64 xx = x * x;
        for (u64 y = 1; y <= lim / y; y++) {
            u64 yy = y * y;
            u64 m = 4 * xx + yy;
            if (m <= lim) { u64 r = m % 12; if (r == 1 || r == 5) bit_toggle(b, m); }
            m = 3 * xx + yy;
            if (m <= lim) { if (m % 12 == 7) bit_toggle(b, m); }
            if (x > y) { m = 3 * xx - yy; if (m <= lim && m % 12 == 11) bit_toggle(b, m); }
        }
        sc->prog_done = (u64)(span * 0.70 * (double)x / (double)(root + 1));
    }
    if (lim >= 2) bit_set(b, 2);
    if (lim >= 3) bit_set(b, 3);
    for (u64 p = 5; p <= lim / p; p++) {
        if (bit_get(b, p))
            for (u64 k = p * p; k <= lim; k += p * p) bit_clear(b, k);
        sc->prog_done = (u64)(span * (0.70 + 0.15 * (double)p / (double)(root + 1)));
    }

    u64 start = low < 2 ? 2 : low;
    for (u64 p = start; p <= high; p++) {
        if (bit_get(b, p)) if (emit(p, ctx)) { free(b); return; }
        if ((p & 0x3FF) == 0 && high > start)
            sc->prog_done = (u64)(span * (0.85 + 0.15 * (double)(p - start) / (double)(high - start)));
        if (p == UINT64_MAX) break;
    }
    sc->prog_done = (u64)span;
    free(b);
}

/* ==================================================================
 *  Method 3: sieve of Sundaram (progress roughly weighted)
 * ================================================================== */
static void method_sundaram(u64 low, u64 high, EmitFn emit, void *ctx)
{
    StreamCtx *sc = (StreamCtx *)ctx;
    sc->prog_span = high + 1;
    sc->prog_done = 0;
    if (high < 2 || low > high) return;
    if (low <= 2 && high >= 2) if (emit(2, ctx)) return;
    if (high < 3) return;

    u64 maxn = (high - 1) / 2;
    size_t nbytes = (size_t)((maxn + 8) / 8);
    check_sieve_size(high, "sundaram", nbytes);
    u8 *b = (u8 *)calloc(nbytes, 1);
    if (!b) { fprintf(stderr, "Error: out of memory.\n"); exit(1); }

    double span = (double)(high + 1);
    u64 imax = isqrt_u64(maxn / 2) + 1;

    for (u64 i = 1; ; i++) {
        u64 v0 = 2 * i + 2 * i * i;
        if (v0 > maxn) break;
        for (u64 j = i; ; j++) {
            u64 v = i + j + 2 * i * j;
            if (v > maxn) break;
            bit_set(b, v);
        }
        if (i <= imax) sc->prog_done = (u64)(span * 0.80 * (double)i / (double)imax);
    }
    u64 startn = low >= 3 ? low / 2 : 1;
    if (startn < 1) startn = 1;
    for (u64 n = startn; n <= maxn; n++) {
        if (!bit_get(b, n)) {
            u64 p = 2 * n + 1;
            if (p >= low && p <= high) if (emit(p, ctx)) { free(b); return; }
        }
        if ((n & 0x3FF) == 0 && maxn > startn)
            sc->prog_done = (u64)(span * (0.80 + 0.20 * (double)(n - startn) / (double)(maxn - startn)));
    }
    sc->prog_done = (u64)span;
    free(b);
}

/* ==================================================================
 *  Method 4: trial division (6k +/- 1)
 * ================================================================== */
static int is_prime_trial(u64 n)
{
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    if (n % 3 == 0) return n == 3;
    for (u64 i = 5; i <= n / i; i += 6)
        if (n % i == 0 || n % (i + 2) == 0) return 0;
    return 1;
}

static void method_trial(u64 low, u64 high, EmitFn emit, void *ctx)
{
    StreamCtx *sc = (StreamCtx *)ctx;
    sc->prog_span = (high >= low) ? high - low + 1 : 0;
    sc->prog_done = 0;
    if (high < 2 || low > high) return;
    if (low <= 2 && high >= 2) if (emit(2, ctx)) return;
    u64 n = low < 3 ? 3 : low;
    if ((n & 1) == 0) n++;
    u64 cnt = 0;
    for (; n <= high; n += 2) {
        if (is_prime_trial(n)) if (emit(n, ctx)) return;
        if (((++cnt) & 0x3FF) == 0) sc->prog_done = n - low + 1;
        if (n > UINT64_MAX - 2) break;
    }
    sc->prog_done = high - low + 1;
}

/* ==================================================================
 *  Method 5: Miller-Rabin (deterministic for u64)
 * ================================================================== */
static u64 mulmod(u64 a, u64 b, u64 m)
{
    u64 hi, lo = _umul128(a, b, &hi), rem;
    _udiv128(hi, lo, m, &rem);
    return rem;
}

static u64 powmod(u64 a, u64 e, u64 m)
{
    u64 r = 1; a %= m;
    while (e) { if (e & 1) r = mulmod(r, a, m); a = mulmod(a, a, m); e >>= 1; }
    return r;
}

static int is_prime_mr(u64 n)
{
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    if (n % 3 == 0) return n == 3;
    if (n % 5 == 0) return n == 5;
    u64 d = n - 1; int s = 0;
    while ((d & 1) == 0) { d >>= 1; s++; }
    static const u64 bases[] = { 2, 325, 9375, 28178, 450775, 9780504, 1795265022 };
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        u64 a = bases[i] % n;
        if (a == 0) continue;
        u64 x = powmod(a, d, n);
        if (x == 1 || x == n - 1) continue;
        int witness = 1;
        for (int r = 1; r < s; r++) {
            x = mulmod(x, x, n);
            if (x == n - 1) { witness = 0; break; }
        }
        if (witness) return 0;
    }
    return 1;
}

static void method_miller(u64 low, u64 high, EmitFn emit, void *ctx)
{
    StreamCtx *sc = (StreamCtx *)ctx;
    sc->prog_span = (high >= low) ? high - low + 1 : 0;
    sc->prog_done = 0;
    if (high < 2 || low > high) return;
    if (low <= 2 && high >= 2) if (emit(2, ctx)) return;
    u64 n = low < 3 ? 3 : low;
    if ((n & 1) == 0) n++;
    u64 cnt = 0;
    for (; n <= high; n += 2) {
        if (is_prime_mr(n)) if (emit(n, ctx)) return;
        if (((++cnt) & 0x3FF) == 0) sc->prog_done = n - low + 1;
        if (n > UINT64_MAX - 2) break;
    }
    sc->prog_done = high - low + 1;
}

/* ==================================================================
 *  Method selection
 * ================================================================== */
typedef void (*MethodFn)(u64 low, u64 high, EmitFn emit, void *ctx);
typedef struct { const char *name; const char *alias; MethodFn fn; const char *desc; } MethodDef;

static const MethodDef METHODS[] = {
    { "sieve",    "eratosthenes", method_eratosthenes, "Segmented sieve of Eratosthenes (default)" },
    { "atkin",    NULL,           method_atkin,        "Sieve of Atkin" },
    { "sundaram", NULL,           method_sundaram,     "Sieve of Sundaram" },
    { "trial",    "probe",        method_trial,        "Trial division (6k +/- 1)" },
    { "miller",   "millerrabin",  method_miller,       "Miller-Rabin (deterministic <=64-bit, probabilistic beyond)" },
};
static const size_t NMETHODS = sizeof(METHODS) / sizeof(METHODS[0]);

static MethodFn resolve_method(const char *name, int *ok)
{
    for (size_t i = 0; i < NMETHODS; i++)
        if (name && (!strcmp(name, METHODS[i].name) ||
                     (METHODS[i].alias && !strcmp(name, METHODS[i].alias)))) {
            *ok = 1; return METHODS[i].fn;
        }
    *ok = 0; return method_eratosthenes;
}

static void list_methods(FILE *out)
{
    fprintf(out, "Available methods (-m/--method):\n");
    for (size_t i = 0; i < NMETHODS; i++) {
        fprintf(out, "  %-9s %s", METHODS[i].name, METHODS[i].desc);
        if (METHODS[i].alias) fprintf(out, " (alias: %s)", METHODS[i].alias);
        if (i == 0) fprintf(out, "  [default]");
        fputc('\n', out);
    }
}

/* ==================================================================
 *  Live progress: CPU per thread + RAM + extrapolation
 * ================================================================== */
typedef struct {
    volatile LONG  stop;
    HANDLE        *handles;
    const u8      *colors;
    StreamCtx    **streams;
    int            n;
    int            enabled;
    int            draw;            /* draw the status line                 */
    int            wide;            /* 1 = 128-bit range, 0 = 64-bit range   */
    int            title_active;    /* update the console title              */
    int            have_title;      /* console title was saved              */
    char           orig_title[512];
    double         start_ms;
    double         last_expected;   /* ms, 0 = no extrapolation yet */
    int            last_color;
    double         next_pct;        /* next threshold in percent    */
} Monitor;

static u64 ft_to_u64(const FILETIME *ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft->dwLowDateTime;
    u.HighPart = ft->dwHighDateTime;
    return u.QuadPart;
}

static u64 thread_cpu_100ns(HANDLE h)
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
    return ft_to_u64(&k) + ft_to_u64(&u);
}

static int handle_is_console(DWORD std)
{
    DWORD mode;
    return GetConsoleMode(GetStdHandle(std), &mode) != 0;
}

static int progress_forced(void)
{
    const char *e = getenv("PRIMES_PROGRESS");
    return e && e[0] != '\0' && strcmp(e, "0") != 0;
}

static unsigned __stdcall monitor_main(void *p)
{
    Monitor *m = (Monitor *)p;
    if (!m->enabled || m->n <= 0) return 0;

    int n = m->n;
    int w = 1; for (int t = n; t >= 10; t /= 10) w++;
    int frame = 0;                    /* rotating separator | / - \ */
    int tick = 0;                     /* monitor ticks (10 = 1 second) */

    u64 *prev_cpu = (u64 *)calloc((size_t)n, sizeof(u64));
    if (!prev_cpu) return 0;
    for (int i = 0; i < n; i++) prev_cpu[i] = thread_cpu_100ns(m->handles[i]);
    double prev_wall = qpc_ms();

    while (!m->stop) {
        Sleep(100);
        if (m->stop) break;

        double now = qpc_ms();
        double dw = now - prev_wall; if (dw < 1.0) dw = 1.0;

        char line[8192];
        int off = snprintf(line, sizeof line, "CPU/Thread:");
        double pct_sum = 0.0;
        for (int i = 0; i < n && off < (int)sizeof line - 64; i++) {
            u64 c = thread_cpu_100ns(m->handles[i]);
            u64 dc = c - prev_cpu[i];
            prev_cpu[i] = c;
            double pct = 100.0 * ((double)dc / 10000.0) / dw;
            if (pct > 100.0) pct = 100.0;
            if (pct < 0.0) pct = 0.0;
            pct_sum += pct;
            int col = m->colors ? m->colors[i] : COL_WHITE;
            off += snprintf(line + off, sizeof line - off,
                            " \x1b[38;5;%dmT%0*d=%3.0f%%\x1b[0m",
                            col, w, i + 1, pct);
        }
        PROCESS_MEMORY_COUNTERS pmc;
        pmc.cb = sizeof(pmc);
        char spin = "|/-\\"[(frame++) & 3];
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            off += snprintf(line + off, sizeof line - off,
                            " %c RAM: %.1f MB", spin, (double)pmc.WorkingSetSize / 1048576.0);

        /* Progress / extrapolation.
         * Based on the numbers processed in the threads (prog_done/prog_span),
         * not on the number of emitted primes. */
        u64 done = 0, span = 0;
        for (int i = 0; i < n; i++) {
            if (m->streams && m->streams[i]) {
                done += m->streams[i]->prog_done;
                span += m->streams[i]->prog_span;
            }
        }
        double elapsed = now - m->start_ms;
        double pct = 0.0;
        if (span > 0) {
            double frac = (double)done / (double)span;
            if (frac > 1.0) frac = 1.0;
            pct = frac * 100.0;

            if (frac > 0.0 && pct >= m->next_pct) {
                double raw = elapsed / frac;
                /* Smoothed estimate (EMA) */
                double expected = (m->last_expected > 0.0)
                                  ? (0.5 * raw + 0.5 * m->last_expected) : raw;
                int col;
                if (m->last_expected <= 0.0) {
                    col = COL_WHITE;                 /* first extrapolation */
                } else {
                    double dev = fabs(expected - m->last_expected) / m->last_expected;
                    if (dev <= 0.05)        col = COL_WHITE;
                    else if (expected > m->last_expected) col = COL_RED;
                    else                    col = COL_GREEN;
                }
                m->last_expected = expected;
                m->last_color = col;
                m->next_pct += 5.0;
                while (m->next_pct <= pct) m->next_pct += 5.0;
            }

            /* Continuous upward correction: the real time must never overtake
             * the (extrapolated) total duration. */
            if (m->last_expected > 0.0 && frac > 0.0 && elapsed > m->last_expected) {
                double need = elapsed / frac;
                double smoothed = 0.5 * need + 0.5 * m->last_expected;
                if (smoothed < elapsed) smoothed = elapsed;
                m->last_expected = smoothed;
                m->last_color = COL_RED;
            }
        }

        /* Always show the extrapolation; before the first extrapolation all
         * number positions are replaced by '-'. The unit follows the
         * magnitude (sec/min/h). */
        if (off < (int)sizeof line - 96) {
            char ps[24], es[24], ts[24];
            const char *pstr, *estr, *tstr, *eunit, *tunit;
            if (m->last_expected > 0.0) {
                snprintf(ps, sizeof ps, "%.0f%%", pct);
                fmt_hms(elapsed / 1000.0, es, sizeof es);
                fmt_hms(m->last_expected / 1000.0, ts, sizeof ts);
                pstr = ps; estr = es; tstr = ts;
                eunit = time_unit(elapsed / 1000.0);
                tunit = time_unit(m->last_expected / 1000.0);
            } else {
                pstr = "--%"; estr = "--:--:--"; tstr = "--:--:--";
                eunit = "sec"; tunit = "sec";
            }
            off += snprintf(line + off, sizeof line - off,
                            " | %s / %s %s of \x1b[38;5;%dm%s %s\x1b[0m",
                            pstr, estr, eunit, m->last_color, tstr, tunit);
        }

        /* Console window title: average CPU over all threads, once per second. */
        tick++;
        if (m->title_active && (tick % 10 == 0)) {
            double avg = n > 0 ? pct_sum / (double)n : 0.0;
            char tbuf[128];
            snprintf(tbuf, sizeof tbuf, "gprimes64.exe @%sBit - CPU %.0f%%",
                     m->wide ? "128" : "64", avg);
            SetConsoleTitleA(tbuf);
        }

        if (m->draw) {
            if (off > (int)sizeof line - 1) off = (int)sizeof line - 1;
            EnterCriticalSection(&g_status_cs);
            out_flush(0);             /* output buffered primes */
            memcpy(g_status_line, line, (size_t)off);
            g_status_line[off] = '\0';
            g_status_len = off;
            status_render(line, off);
            LeaveCriticalSection(&g_status_cs);
        }
        prev_wall = now;
    }

    free(prev_cpu);
    return 0;
}

typedef struct { Monitor mon; HANDLE thread; } MonitorHandle;

static void monitor_start(MonitorHandle *mh, HANDLE *handles, const u8 *colors,
                          StreamCtx **streams, int n, int live, int title, int wide)
{
    memset(&mh->mon, 0, sizeof mh->mon);
    mh->thread = NULL;
    mh->mon.handles = handles;
    mh->mon.colors  = colors;
    mh->mon.streams = streams;
    mh->mon.n = n;
    mh->mon.wide = wide;
    mh->mon.start_ms = qpc_ms();
    mh->mon.last_color = COL_WHITE;
    mh->mon.next_pct = 10.0;
    int stderrcon = handle_is_console(STD_ERROR_HANDLE);
    int stdoutcon = handle_is_console(STD_OUTPUT_HANDLE);
    int hascon = GetConsoleWindow() != NULL || stderrcon || stdoutcon;
    mh->mon.draw = live && (stderrcon || progress_forced());
    mh->mon.title_active = title && hascon;
    mh->mon.enabled = n > 0 && (mh->mon.draw || mh->mon.title_active);
    if (mh->mon.title_active) {
        if (GetConsoleTitleA(mh->mon.orig_title, (DWORD)sizeof mh->mon.orig_title) > 0)
            mh->mon.have_title = 1;
        char tbuf[128];
        snprintf(tbuf, sizeof tbuf, "gprimes64.exe @%sBit - CPU 0%%", wide ? "128" : "64");
        SetConsoleTitleA(tbuf);
    }
    if (mh->mon.enabled)
        mh->thread = (HANDLE)_beginthreadex(NULL, 0, monitor_main, &mh->mon, 0, NULL);
}

static void monitor_stop(MonitorHandle *mh)
{
    if (mh->thread) {
        InterlockedExchange(&mh->mon.stop, 1);
        WaitForSingleObject(mh->thread, INFINITE);
        CloseHandle(mh->thread);
        mh->thread = NULL;
    }
    if (mh->mon.have_title)
        SetConsoleTitleA(mh->mon.orig_title);
}

/* ==================================================================
 *  Multithreading
 * ================================================================== */
typedef struct { MethodFn fn; u64 low, high; StreamCtx ctx; } ThreadArg;

static unsigned __stdcall thread_main(void *p)
{
    ThreadArg *a = (ThreadArg *)p;
    a->ctx.buffered = 1;
    a->ctx.print    = 0;
    a->ctx.stop_at  = 0;
    a->ctx.last_ms  = qpc_ms();
    a->fn(a->low, a->high, stream_emit, &a->ctx);
    return 0;
}

static int resolve_threads(int requested)
{
    if (requested > 0) return requested;
    SYSTEM_INFO si; GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
    return n < 1 ? 1 : n;
}

static void run_method(MethodFn fn, u64 low, u64 high, int threads,
                       StreamCtx *out, int live)
{
    g_status_coord = 0;

    if (threads <= 1 || low > high) {
        u8 color = COL_WHITE;                     /* single thread: white */
        HANDLE one = OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
        HANDLE handles[1];
        int n = 0;
        if (one) { handles[0] = one; n = 1; }
        StreamCtx *streams1[1];
        streams1[0] = out;
        MonitorHandle mh;
        monitor_start(&mh, handles, &color, streams1, n, live, 1, 0);

        /* Preserve the status line only when writing concurrently to the same
         * console (stdout). Then reserve the bottom line. */
        g_status_coord = mh.mon.enabled && out->print && handle_is_console(STD_OUTPUT_HANDLE);

        out->buffered = 0;
        out->cur_color = color;
        out->last_ms = qpc_ms();
        fn(low, high, stream_emit, out);

        g_status_coord = 0;
        monitor_stop(&mh);
        status_finish();
        if (one) CloseHandle(one);
        if (out->table) print_table(out);
        return;
    }

    u64 total = high - low + 1;
    u64 span = total / (u64)threads;
    if (total % (u64)threads != 0) span++;
    if (span == 0) span = 1;

    ThreadArg *args = (ThreadArg *)calloc((size_t)threads, sizeof(ThreadArg));
    uintptr_t *hs   = (uintptr_t *)calloc((size_t)threads, sizeof(uintptr_t));
    if (!args || !hs) { fprintf(stderr, "Error: out of memory.\n"); exit(1); }
    int nargs = 0;

    for (int t = 0; t < threads; t++) {
        u64 lo = low + (u64)t * span;
        if (lo > high) break;
        u64 hi = lo + span - 1;
        if (hi < lo || hi > high) hi = high;

        ThreadArg *a = &args[nargs];
        a->fn = fn; a->low = lo; a->high = hi;
        memset(&a->ctx, 0, sizeof a->ctx);
        a->ctx.measure    = out->measure;
        a->ctx.count_only = out->count_only;
        a->ctx.last_ms    = qpc_ms();

        hs[nargs] = _beginthreadex(NULL, 0, thread_main, a, 0, NULL);
        if (!hs[nargs]) { fprintf(stderr, "Error: could not create thread.\n"); exit(1); }
        nargs++;
    }

    u8 *colors = (u8 *)malloc((size_t)(nargs ? nargs : 1) * sizeof(u8));
    assign_thread_colors(nargs, colors);

    StreamCtx **streams = (StreamCtx **)malloc((size_t)(nargs ? nargs : 1) * sizeof(StreamCtx *));
    for (int t = 0; t < nargs; t++) streams[t] = &args[t].ctx;

    HANDLE *hhandles = (HANDLE *)calloc((size_t)(nargs ? nargs : 1), sizeof(HANDLE));
    for (int t = 0; t < nargs; t++) hhandles[t] = (HANDLE)hs[t];
    MonitorHandle mh;
    monitor_start(&mh, hhandles, colors, streams, nargs, live, 1, 0);

    for (int t = 0; t < nargs; t++)
        WaitForSingleObject((HANDLE)hs[t], INFINITE);

    monitor_stop(&mh);
    status_finish();

    if (out->count_only) {
        u64 sum = 0;
        for (int t = 0; t < nargs; t++) sum += args[t].ctx.found;
        out->found = sum;
        if (out->stop_at && out->found > out->stop_at) out->found = out->stop_at;
    } else {
        int done = 0;
        for (int t = 0; t < nargs && !done; t++) {
            StreamCtx *s = &args[t].ctx;
            out->cur_color = colors[t];
            for (size_t i = 0; i < s->len; i++) {
                double ms = s->measure ? s->bt[i] : 0.0;
                if (emit_final(s->bp[i], ms, out)) { done = 1; break; }
            }
        }
    }
    if (out->table) print_table(out);

    for (int t = 0; t < nargs; t++) {
        CloseHandle((HANDLE)hs[t]);
        free(args[t].ctx.bp);
        free(args[t].ctx.bt);
        free(args[t].ctx.bc);
    }
    free(args); free(hs); free(hhandles); free(colors); free(streams);
}

/* ==================================================================
 *  Upper bound for the n-th prime: n*(ln n + ln ln n)
 * ================================================================== */
static u64 nth_prime_upper(u64 n)
{
    static const u64 first_primes[] = { 0, 2, 3, 5, 7, 11 };
    if (n < 6) return first_primes[n];
    double dn = (double)n;
    double l = log(dn);
    double ll = log(l);
    double est = dn * (l + ll) + 10.0;
    if (est >= 18446744073709551615.0) return UINT64_MAX;
    return (u64)est;
}

/* ==================================================================
 *  Arguments
 * ================================================================== */
static void print_help(FILE *out)
{
    fprintf(out,
        "GPrimes64 " PRIMES_VERSION " - slim prime generator (Win x64)\n"
        "\n"
        "Usage:\n"
        "  gprimes64 <N>                 All primes up to and including N\n"
        "  gprimes64 -c <N>              The first N primes\n"
        "  gprimes64 -r <A> <B>          All primes in the range A to B\n"
        "\n"
        "Options:\n"
        "  -l, --limit <N>               Same as 'gprimes64 <N>'\n"
        "  -c, --count <N>               The first N primes\n"
        "  -r, --range <A> <B>           Primes in the range A..B\n"
        "  -m, --method <name>           Computation method (default: sieve)\n"
        "  -t, --time                    ASCII table: prime | compute time\n"
        "  -j, --threads <N>             Use N threads (1 = off, default)\n"
        "      --mt                      As many threads as CPU cores (-j 0)\n"
        "      --list-methods            List available methods\n"
        "  -q, --quiet                   Output only the summary\n"
        "  -h, --help                    Show this help\n"
        "  -v, --version                 Show version\n"
        "\n"
        "A summary is always printed. In all modes except -q a live line is\n"
        "shown on stderr: CPU load per thread (color-coded), RAM, and from\n"
        "about 10%% progress: percent / elapsed time / extrapolated total\n"
        "duration (red = longer, green = shorter than before).\n"
        "\n"
        "Number range: 0 to 340282366920938463463374607431768211455 (2^128-1).\n"
        "Values beyond 64-bit are handled with 128-bit arithmetic; there only\n"
        "-m miller is available (probabilistic).\n"
        "For very large numbers or small ranges near large values, -m miller\n"
        "is much better suited than the sieve (the sieve builds base primes\n"
        "up to sqrt(N) and needs a lot of memory).\n"
        "\n"
        "Examples:\n"
        "  gprimes64 -t 100\n"
        "  gprimes64 -m atkin -q 100000000\n"
        "  gprimes64 -m sieve -j 8 -q 1000000000\n");
}

typedef enum { P_OK, P_EMPTY, P_INVALID, P_RANGE } ParseStatus;

static ParseStatus parse_u64(const char *s, u64 *out)
{
    if (!s || !*s) return P_EMPTY;
    if (s[0] == '-') return P_INVALID;            /* no negative numbers */
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno == ERANGE) return P_RANGE;          /* > 2^64-1 */
    if (errno != 0 || end == s || *end != '\0') return P_INVALID;
    *out = (u64)v;
    return P_OK;
}

static int parse_int(const char *s, int *out)
{
    u64 v;
    if (parse_u64(s, &v) != P_OK || v > 0x7FFFFFFF) return 0;
    *out = (int)v;
    return 1;
}

/* Clear message for numeric arguments (distinguishes overflow from invalid). */
static void report_number_error(const char *what, const char *val, ParseStatus st)
{
    if (st == P_RANGE)
        fprintf(stderr,
            "Error: %s %s is too large (max. 340282366920938463463374607431768211455 = 2^128-1).\n"
            "       For very large numbers use single-number primality tests: -m miller.\n",
            what, val);
    else
        fprintf(stderr, "Error: invalid number for %s: %s\n", what, val);
}

/* Feasibility check for the sieve method: building the base primes up to
 * sqrt(high) needs a lot of memory (bitset + prime array per thread). If the
 * estimated requirement exceeds the available RAM, abort with a clear message. */
static void check_sieve_feasible(u64 high, int threads)
{
    if (high < 3) return;
    double sr = sqrt((double)high);
    double per = sr / 16.0 + (sr / log(sr)) * 8.0;   /* bitset + prime array */
    if (threads < 1) threads = 1;
    double total = per * (double)threads;

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return;
    double avail = (double)ms.ullAvailPhys;
    if (avail <= 0.0) return;

    if (total > avail * 0.75) {
        fprintf(stderr,
            "Error: upper bound %llu is too large for the sieve method.\n"
            "       Estimated base-prime memory ~%.2f GB per thread x %d = ~%.2f GB,\n"
            "       but only ~%.2f GB are available.\n"
            "       Please use -m miller or -m trial (or reduce -j).\n",
            (unsigned long long)high, per / 1073741824.0, threads,
            total / 1073741824.0, avail / 1073741824.0);
        exit(1);
    }
}

/* ==================================================================
 *  128-bit support (for inputs beyond 64-bit)
 * ================================================================== */
typedef struct { u64 lo, hi; } u128;

static u128 u128_from_u64(u64 v) { u128 r; r.lo = v; r.hi = 0; return r; }
static int  u128_is_zero(u128 a) { return a.lo == 0 && a.hi == 0; }
static int  u128_is_even(u128 a) { return (a.lo & 1) == 0; }
static int  u128_eq(u128 a, u128 b) { return a.lo == b.lo && a.hi == b.hi; }
static int  u128_cmp(u128 a, u128 b) {
    if (a.hi != b.hi) return a.hi < b.hi ? -1 : 1;
    if (a.lo != b.lo) return a.lo < b.lo ? -1 : 1;
    return 0;
}
static u128 u128_add(u128 a, u128 b) {
    u128 r; r.lo = a.lo + b.lo; r.hi = a.hi + b.hi + (r.lo < a.lo); return r;
}
static u128 u128_sub(u128 a, u128 b) {
    u128 r; r.lo = a.lo - b.lo; r.hi = a.hi - b.hi - (a.lo < b.lo); return r;
}
static void u128_add_u64(u128 *a, u64 d) {
    u64 old = a->lo; a->lo += d; if (a->lo < old) a->hi++;
}
static u128 u128_shr1(u128 a) {
    u128 r; r.lo = (a.lo >> 1) | (a.hi << 63); r.hi = a.hi >> 1; return r;
}
static u128 u128_mul_u64(u128 a, u64 m) {
    u64 hi; u64 lo = _umul128(a.lo, m, &hi);
    u128 r; r.lo = lo; r.hi = a.hi * m + hi;
    return r;
}
static u64 u128_bit(u128 a, int i) {
    return (i >= 64) ? ((a.hi >> (i - 64)) & 1) : ((a.lo >> i) & 1);
}
/* Divide by a small divisor (<= 2^32); returns the remainder, v = quotient. */
static u64 u128_divmod_small(u128 *v, u64 d) {
    u128 q; q.lo = 0; q.hi = 0;
    u64 rem = 0;
    for (int i = 127; i >= 0; i--) {
        rem = (rem << 1) | u128_bit(*v, i);
        if (rem >= d) {
            rem -= d;
            if (i >= 64) q.hi |= ((u64)1 << (i - 64));
            else         q.lo |= ((u64)1 << i);
        }
    }
    *v = q;
    return rem;
}
static void u128_to_dec(u128 v, char *buf, size_t n) {
    char tmp[48]; int i = 0;
    if (u128_is_zero(v)) tmp[i++] = '0';
    else { u128 t = v; while (!u128_is_zero(t)) { u64 r = u128_divmod_small(&t, 10); tmp[i++] = (char)('0' + r); } }
    if ((size_t)i >= n) i = (int)n - 1;
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

/* Parse a decimal into u128. *fits64 is set if the value is <= 2^64-1. */
static const char *U128_MAX_DEC = "340282366920938463463374607431768211455";
static ParseStatus parse_u128(const char *s, u128 *out, int *fits64) {
    if (!s || !*s) return P_EMPTY;
    if (s[0] == '-') return P_INVALID;
    const char *p = s;
    while (*p == '0') p++;
    size_t len = strlen(p);
    if (len == 0) { *out = u128_from_u64(0); if (fits64) *fits64 = 1; return P_OK; }
    if (len > 39) return P_RANGE;
    if (len == 39 && strcmp(p, U128_MAX_DEC) > 0) return P_RANGE;
    u128 v = u128_from_u64(0);
    for (size_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') return P_INVALID;
        u64 carry;
        u64 lo10 = _umul128(v.lo, 10, &carry);
        v.hi = v.hi * 10 + carry;
        v.lo = lo10;
        u128_add_u64(&v, (u64)(p[i] - '0'));
    }
    *out = v;
    if (fits64) *fits64 = (v.hi == 0);
    return P_OK;
}

/* --- 128-bit primality (Miller-Rabin, probabilistic beyond 64-bit) --- */
static u128 u128_addmod(u128 a, u128 b, u128 m) {
    u128 s = u128_add(a, b);
    if (u128_cmp(s, m) >= 0) s = u128_sub(s, m);
    return s;
}
static u128 u128_mulmod(u128 a, u128 b, u128 m) {
    u128 r = u128_from_u64(0);
    while (!u128_is_zero(b)) {
        if (b.lo & 1) r = u128_addmod(r, a, m);
        a = u128_addmod(a, a, m);
        b = u128_shr1(b);
    }
    return r;
}
static u128 u128_powmod(u128 a, u128 e, u128 m) {
    u128 r = u128_from_u64(1);
    while (!u128_is_zero(e)) {
        if (e.lo & 1) r = u128_mulmod(r, a, m);
        a = u128_mulmod(a, a, m);
        e = u128_shr1(e);
    }
    return r;
}
static int is_prime128_mr(u128 n) {
    if (n.hi == 0) return is_prime_mr(n.lo);   /* deterministic for 64-bit */
    static const u64 bases[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    u128 one = u128_from_u64(1);
    u128 nm1 = u128_sub(n, one);
    u128 d = nm1; int s = 0;
    while (u128_is_even(d)) { d = u128_shr1(d); s++; }
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        u128 a = u128_from_u64(bases[i]);
        if (u128_cmp(a, n) >= 0) break;
        u128 x = u128_powmod(a, d, n);
        if (u128_eq(x, one) || u128_eq(x, nm1)) continue;
        int witness = 1;
        for (int r = 1; r < s; r++) {
            x = u128_mulmod(x, x, n);
            if (u128_eq(x, nm1)) { witness = 0; break; }
        }
        if (witness) return 0;
    }
    return 1;
}

/* Upper bound for the n-th prime in 128-bit (for n up to 2^64-1). */
static u128 nth_prime_upper128(u64 n) {
    double dn = (double)n;
    double l = log(dn), ll = log(l);
    double est = dn * (l + ll) * 1.10 + 100.0;
    double two64 = 18446744073709551616.0;
    u64 hi = (u64)(est / two64);
    u64 lo = (u64)(est - (double)hi * two64);
    u128 r; r.hi = hi; r.lo = lo;
    return r;
}

/* --- output/table for 128-bit primes --- */
typedef struct {
    int    print;
    int    table;
    int    measure;
    int    buffered;     /* buffer primes (for the MT merge) */
    u64    stop_at;
    u64    found;
    u8     cur_color;
    u128  *bp;
    double *bt;
    u8    *bc;
    size_t  len, cap;
    StreamCtx prog;   /* progress only (read by the monitor) */
} Ctx128;

static void buf_push128(Ctx128 *c, u128 p, double ms) {
    if (c->len == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 4096;
        u128   *np = (u128 *)realloc(c->bp, nc * sizeof(u128));
        double *nt = c->measure ? (double *)realloc(c->bt, nc * sizeof(double)) : c->bt;
        u8     *nb = c->table ? (u8 *)realloc(c->bc, nc * sizeof(u8)) : c->bc;
        if (!np || (c->measure && !nt) || (c->table && !nb)) { fprintf(stderr, "Error: out of memory.\n"); exit(1); }
        c->bp = np; c->bt = nt; c->bc = nb; c->cap = nc;
    }
    c->bp[c->len] = p;
    if (c->measure) c->bt[c->len] = ms;
    if (c->table)   c->bc[c->len] = c->cur_color;
    c->len++;
}

static void print_u128(u128 v) {
    char buf[48]; u128_to_dec(v, buf, sizeof buf);
    if (!g_status_coord) { fputs(buf, stdout); fputc('\n', stdout); return; }
    EnterCriticalSection(&g_status_cs);
    int n = snprintf(g_outbuf + g_outlen, sizeof(g_outbuf) - (size_t)g_outlen, "%s\n", buf);
    if (n > 0) g_outlen += n;
    if (g_outlen > (int)sizeof(g_outbuf) - 64) out_flush(1);
    LeaveCriticalSection(&g_status_cs);
}

static void print_table128(const Ctx128 *c) {
    const char *h1 = "Prime";
    const char *h2 = "Compute time";
    int w1 = (int)strlen(h1);
    int w2 = (int)strlen(h2);
    char b[64], p[48];
    for (size_t i = 0; i < c->len; i++) {
        u128_to_dec(c->bp[i], p, sizeof p);
        int d = (int)strlen(p);
        if (d > w1) w1 = d;
        int n = snprintf(b, sizeof b, "%.3f ms", c->bt[i]);
        if (n > w2) w2 = n;
    }
    print_rule(w1, w2);
    printf("| %-*s | %-*s |\n", w1, h1, w2, h2);
    print_rule(w1, w2);
    for (size_t i = 0; i < c->len; i++) {
        u128_to_dec(c->bp[i], p, sizeof p);
        snprintf(b, sizeof b, "%.3f ms", c->bt[i]);
        if (g_table_color) {
            int col = c->bc[i];
            printf("\x1b[38;5;%dm|\x1b[0m %*s \x1b[38;5;%dm|\x1b[0m %*s \x1b[38;5;%dm|\x1b[0m\n",
                   col, w1, p, col, w2, b, col);
        } else {
            printf("| %*s | %*s |\n", w1, p, w2, b);
        }
    }
    print_rule(w1, w2);
    fflush(stdout);
}

static void method128_miller(u128 low, u128 high, Ctx128 *c) {
    u128 width = u128_sub(high, low);
    u64 span = (width.hi == 0) ? (width.lo / 2 + 1) : UINT64_MAX;
    c->prog.prog_span = span;
    c->prog.prog_done = 0;

    int collect = c->table || c->buffered;
    if (u128_cmp(low, u128_from_u64(2)) <= 0 && u128_cmp(high, u128_from_u64(2)) >= 0) {
        if (collect) buf_push128(c, u128_from_u64(2), 0.0);
        else if (c->print) print_u128(u128_from_u64(2));
        c->found++;
        if (c->stop_at && c->found >= c->stop_at) { c->prog.prog_done = span; return; }
    }

    u128 n = low;
    if (u128_cmp(n, u128_from_u64(3)) < 0) n = u128_from_u64(3);
    if (u128_is_even(n)) u128_add_u64(&n, 1);
    u64 cnt = 0;
    while (u128_cmp(n, high) <= 0) {
        double t0 = c->measure ? qpc_ms() : 0.0;
        int prime = is_prime128_mr(n);
        if (prime) {
            double dt = c->measure ? (qpc_ms() - t0) : 0.0;
            if (collect) buf_push128(c, n, dt);
            else if (c->print) print_u128(n);
            c->found++;
            if (c->stop_at && c->found >= c->stop_at) { c->prog.prog_done = span; return; }
        }
        if (++cnt > span) cnt = span;
        if ((cnt & 0x3FF) == 0) c->prog.prog_done = cnt;
        if (n.hi == UINT64_MAX) break;      /* avoid wrap near 2^128 */
        u128_add_u64(&n, 2);
    }
    c->prog.prog_done = span;
}

typedef struct { u128 low, high; Ctx128 ctx; } ThreadArg128;

static unsigned __stdcall thread128_main(void *p) {
    ThreadArg128 *a = (ThreadArg128 *)p;
    a->ctx.buffered = 1;
    a->ctx.print = 0;
    a->ctx.table = 0;
    method128_miller(a->low, a->high, &a->ctx);
    return 0;
}

static void run128(u128 low, u128 high, int threads, Ctx128 *out, int live) {
    if (threads <= 1 || u128_cmp(low, high) >= 0) {
        HANDLE one = OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
        HANDLE handles[1];
        int n = 0;
        if (one) { handles[0] = one; n = 1; }
        u8 color = COL_WHITE;
        StreamCtx *streams1[1];
        streams1[0] = &out->prog;
        MonitorHandle mh;
        monitor_start(&mh, handles, &color, streams1, n, live, 1, 1);
        g_status_coord = mh.mon.draw && out->print && handle_is_console(STD_OUTPUT_HANDLE);
        out->prog.prog_span = 0;
        out->prog.prog_done = 0;
        method128_miller(low, high, out);
        g_status_coord = 0;
        monitor_stop(&mh);
        status_finish();
        if (one) CloseHandle(one);
        if (out->table) print_table128(out);
        return;
    }

    /* Split [low, high] into contiguous chunks by value. */
    u128 width = u128_sub(high, low);
    u128 chunk = width;
    u64 rem = u128_divmod_small(&chunk, (u64)threads);
    if (rem != 0) u128_add_u64(&chunk, 1);
    if (u128_is_zero(chunk)) chunk = u128_from_u64(1);

    ThreadArg128 *args = (ThreadArg128 *)calloc((size_t)threads, sizeof(ThreadArg128));
    uintptr_t *hs = (uintptr_t *)calloc((size_t)threads, sizeof(uintptr_t));
    if (!args || !hs) { fprintf(stderr, "Error: out of memory.\n"); exit(1); }
    int nargs = 0;
    for (int t = 0; t < threads; t++) {
        u128 lo = u128_add(low, u128_mul_u64(chunk, (u64)t));
        if (u128_cmp(lo, high) > 0) break;
        u128 hi = u128_sub(u128_add(lo, chunk), u128_from_u64(1));
        if (u128_cmp(hi, high) > 0) hi = high;
        ThreadArg128 *a = &args[nargs];
        a->low = lo; a->high = hi;
        memset(&a->ctx, 0, sizeof(Ctx128));
        a->ctx.measure = out->measure;
        hs[nargs] = _beginthreadex(NULL, 0, thread128_main, a, 0, NULL);
        if (!hs[nargs]) { fprintf(stderr, "Error: could not create thread.\n"); exit(1); }
        nargs++;
    }

    u8 *colors = (u8 *)malloc((size_t)(nargs ? nargs : 1) * sizeof(u8));
    assign_thread_colors(nargs, colors);
    StreamCtx **streams = (StreamCtx **)malloc((size_t)(nargs ? nargs : 1) * sizeof(StreamCtx *));
    for (int t = 0; t < nargs; t++) streams[t] = &args[t].ctx.prog;
    HANDLE *hhandles = (HANDLE *)calloc((size_t)(nargs ? nargs : 1), sizeof(HANDLE));
    for (int t = 0; t < nargs; t++) hhandles[t] = (HANDLE)hs[t];

    MonitorHandle mh;
    monitor_start(&mh, hhandles, colors, streams, nargs, live, 1, 1);

    for (int t = 0; t < nargs; t++) WaitForSingleObject((HANDLE)hs[t], INFINITE);
    monitor_stop(&mh);
    status_finish();

    if (!out->print && !out->table) {
        u64 sum = 0;
        for (int t = 0; t < nargs; t++) sum += args[t].ctx.found;
        out->found = sum;
        if (out->stop_at && out->found > out->stop_at) out->found = out->stop_at;
    } else {
        int done = 0;
        for (int t = 0; t < nargs && !done; t++) {
            Ctx128 *w = &args[t].ctx;
            out->cur_color = colors[t];
            for (size_t i = 0; i < w->len; i++) {
                if (out->table) buf_push128(out, w->bp[i], w->measure ? w->bt[i] : 0.0);
                else if (out->print) print_u128(w->bp[i]);
                out->found++;
                if (out->stop_at && out->found >= out->stop_at) { done = 1; break; }
            }
        }
    }
    if (out->table) print_table128(out);

    for (int t = 0; t < nargs; t++) {
        CloseHandle((HANDLE)hs[t]);
        free(args[t].ctx.bp); free(args[t].ctx.bt); free(args[t].ctx.bc);
    }
    free(args); free(hs); free(hhandles); free(colors); free(streams);
}

/* ==================================================================
 *  main
 * ================================================================== */
enum { MODE_LIMIT, MODE_FIRST, MODE_RANGE };

int main(int argc, char **argv)
{
    int mode = MODE_LIMIT;
    int quiet = 0, table = 0;
    int threads_opt = 1;
    const char *method_name = "sieve";
    u64 limit = 0, nfirst = 0, rangelow = 0, rangehigh = 0;
    u128 limit128 = { 0, 0 }, nfirst128 = { 0, 0 };
    u128 rangelow128 = { 0, 0 }, rangehigh128 = { 0, 0 };
    int lim_fit = 1, cnt_fit = 1, rlo_fit = 1, rhi_fit = 1;
    int have_action = 0;

    InitializeCriticalSection(&g_status_cs);

    /* Enable ANSI colors on Windows consoles */
    DWORD cm;
    if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &cm))
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), cm | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    if (GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &cm))
        SetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), cm | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    {
        const char *e = getenv("PRIMES_COLOR");
        g_table_color = handle_is_console(STD_OUTPUT_HANDLE) ||
                        (e && e[0] != '\0' && strcmp(e, "0") != 0);
    }

    if (argc < 2) { print_help(stderr); return 1; }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { print_help(stdout); return 0; }
        if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("GPrimes64 %s\n", PRIMES_VERSION); return 0; }
        if (!strcmp(a, "--list-methods")) { list_methods(stdout); return 0; }
        if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) { quiet = 1; continue; }
        if (!strcmp(a, "-t") || !strcmp(a, "--time")) { table = 1; continue; }
        if (!strcmp(a, "--mt")) { threads_opt = 0; continue; }
        if (!strcmp(a, "-m") || !strcmp(a, "--method")) {
            if (++i >= argc) { fprintf(stderr, "Error: -m/--method expects a name.\n"); return 1; }
            method_name = argv[i]; continue;
        }
        if (!strcmp(a, "-j") || !strcmp(a, "--threads")) {
            if (++i >= argc || !parse_int(argv[i], &threads_opt) || threads_opt < 0) {
                fprintf(stderr, "Error: -j/--threads expects a number >= 0.\n"); return 1;
            }
            continue;
        }
        if (!strcmp(a, "-l") || !strcmp(a, "--limit")) {
            if (++i >= argc) { fprintf(stderr, "Error: -l/--limit expects a number.\n"); return 1; }
            ParseStatus st = parse_u128(argv[i], &limit128, &lim_fit);
            if (st != P_OK) { report_number_error("limit (-l)", argv[i], st); return 1; }
            limit = lim_fit ? limit128.lo : 0;
            mode = MODE_LIMIT; have_action = 1; continue;
        }
        if (!strcmp(a, "-c") || !strcmp(a, "--count")) {
            if (++i >= argc) { fprintf(stderr, "Error: -c/--count expects a number.\n"); return 1; }
            ParseStatus st = parse_u128(argv[i], &nfirst128, &cnt_fit);
            if (st != P_OK) { report_number_error("count (-c)", argv[i], st); return 1; }
            nfirst = cnt_fit ? nfirst128.lo : 0;
            mode = MODE_FIRST; have_action = 1; continue;
        }
        if (!strcmp(a, "-r") || !strcmp(a, "--range")) {
            if (i + 2 >= argc) { fprintf(stderr, "Error: -r/--range expects two numbers A B.\n"); return 1; }
            ParseStatus sa = parse_u128(argv[i + 1], &rangelow128, &rlo_fit);
            if (sa != P_OK) { report_number_error("range bound A (-r)", argv[i + 1], sa); return 1; }
            ParseStatus sb = parse_u128(argv[i + 2], &rangehigh128, &rhi_fit);
            if (sb != P_OK) { report_number_error("range bound B (-r)", argv[i + 2], sb); return 1; }
            rangelow  = rlo_fit ? rangelow128.lo : 0;
            rangehigh = rhi_fit ? rangehigh128.lo : 0;
            i += 2; mode = MODE_RANGE; have_action = 1; continue;
        }
        if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "Unknown option: %s\n", a); print_help(stderr); return 1;
        }
        {
            ParseStatus st = parse_u128(a, &limit128, &lim_fit);
            if (st != P_OK) { report_number_error("limit", a, st); return 1; }
            limit = lim_fit ? limit128.lo : 0;
        }
        mode = MODE_LIMIT; have_action = 1;
    }

    if (!have_action) { print_help(stderr); return 1; }
    if (table && quiet) {
        fprintf(stderr,
            "Error: -q/--quiet and -t/--time cannot be combined because "
            "-q outputs only the summary.\n");
        return 1;
    }

    int method_ok = 0;
    MethodFn mf = resolve_method(method_name, &method_ok);
    if (!method_ok) {
        fprintf(stderr, "Unknown method: %s\n\n", method_name);
        list_methods(stderr);
        return 1;
    }

    int threads = resolve_threads(threads_opt);

    /* Decide whether 128-bit handling is required. */
    int use128 = 0;
    if (mode == MODE_LIMIT)      use128 = !lim_fit;
    else if (mode == MODE_RANGE) use128 = !rlo_fit || !rhi_fit;
    else {                       /* MODE_FIRST */
        if (!cnt_fit) use128 = 1;
        else if (nfirst >= 6 && nth_prime_upper(nfirst) == UINT64_MAX) use128 = 1;
    }

    if (use128) {
        if (strcmp(method_name, "miller") != 0) {
            fprintf(stderr,
                "Error: method '%s' is not available for numbers beyond 64-bit; use -m miller.\n",
                method_name);
            return 1;
        }
        u128 lo128 = { 0, 0 }, hi128 = { 0, 0 };
        u64 stop = 0;
        if (mode == MODE_LIMIT) {
            lo128 = u128_from_u64(0);
            hi128 = limit128;
        } else if (mode == MODE_RANGE) {
            lo128 = rangelow128; hi128 = rangehigh128;
            if (u128_cmp(lo128, hi128) > 0) { u128 t = lo128; lo128 = hi128; hi128 = t; }
        } else {
            if (!cnt_fit) {
                fprintf(stderr, "Error: count beyond 64-bit is not supported (the requested number of primes must fit in 64-bit).\n");
                return 1;
            }
            lo128 = u128_from_u64(0);
            hi128 = nth_prime_upper128(nfirst128.lo);
            stop = nfirst128.lo;
        }

        Ctx128 out128;
        memset(&out128, 0, sizeof out128);
        out128.print   = !quiet && !table;
        out128.table   = table;
        out128.measure = table;
        out128.stop_at = stop;
        out128.cur_color = COL_WHITE;

        double t0 = qpc_ms();
        run128(lo128, hi128, threads, &out128, !quiet);
        double t1 = qpc_ms();

        if (quiet)
            printf("Count: %llu, Time: %.3f s, Method: %s, Threads: %d\n",
                   (unsigned long long)out128.found, (t1 - t0) / 1000.0, method_name, threads);
        else
            fprintf(stderr, "Count: %llu, Time: %.3f s, Method: %s, Threads: %d\n",
                    (unsigned long long)out128.found, (t1 - t0) / 1000.0, method_name, threads);

        free(out128.bp); free(out128.bt); free(out128.bc);
        DeleteCriticalSection(&g_status_cs);
        return 0;
    }

    u64 low = 0, high = 0;
    if (mode == MODE_LIMIT) { low = 0; high = limit; }
    else if (mode == MODE_RANGE) {
        if (rangelow > rangehigh) { u64 t = rangelow; rangelow = rangehigh; rangehigh = t; }
        low = rangelow; high = rangehigh;
    } else { low = 0; high = nth_prime_upper(nfirst); }

    if (mf == method_eratosthenes)
        check_sieve_feasible(high, threads);

    StreamCtx out;
    memset(&out, 0, sizeof out);
    out.measure    = table;
    out.table      = table;
    out.print      = !quiet && !table;
    out.count_only = !out.print && !out.table;
    out.stop_at    = (mode == MODE_FIRST) ? nfirst : 0;
    out.cur_color  = COL_WHITE;
    out.last_ms    = qpc_ms();

    int live = !quiet;                 /* live line in all modes except -q */

    double t0 = qpc_ms();
    run_method(mf, low, high, threads, &out, live);
    double t1 = qpc_ms();

    if (quiet) {
        /* -q: only the summary (on stdout). */
        printf("Count: %llu, Time: %.3f s, Method: %s, Threads: %d\n",
               (unsigned long long)out.found, (t1 - t0) / 1000.0, method_name, threads);
    } else {
        fprintf(stderr, "Count: %llu, Time: %.3f s, Method: %s, Threads: %d\n",
                (unsigned long long)out.found, (t1 - t0) / 1000.0, method_name, threads);
    }

    DeleteCriticalSection(&g_status_cs);
    return 0;
}
