/**
 * IARM-Bus traceparent transport helpers.
 *
 * IARM has no dependency on any tracing/OTel library here. A traceparent is
 * just an opaque, validated W3C-format string that the caller supplies via
 * IARM_Bus_SetTraceparent() and the receiver reads back via
 * IARM_Bus_GetTraceparent(). IARM only stores, transports and validates the
 * string format - it never calls into a tracer.
 */

#ifndef IARM_TP_H
#define IARM_TP_H

#ifdef OTEL_ENABLED

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Event path ─────────────────────────────────────────────────────────── */

/**
 * Magic byte appended at eventData->data[len] to signal that the next 56 bytes
 * carry a W3C traceparent string (55 chars + NUL terminator).
 * eventData->len is left unchanged, so receivers that read only data[0..len-1]
 * are unaffected when no traceparent was set.
 */
#define IARM_TP_EVENT_MAGIC   ((unsigned char)0xAA)

/** W3C traceparent length: "00-<32hex>-<16hex>-<2hex>" = exactly 55 characters */
#define IARM_TP_LEN            55

/** Bytes appended after user data: 1 (magic) + 55 (traceparent) + 1 (NUL) = 57 */
#define IARM_TP_SUFFIX_SIZE     57

/* ── RPC path ────────────────────────────────────────────────────────────── */

/** Magic identifying an IARM_RPC_TP_Envelope_t as the arg in _BusCall_FuncWrapper. */
#define IARM_RPC_TP_MAGIC       0x52504341U  /* ASCII 'RPCA' */

/**
 * RPC envelope used only when the caller has set an outgoing traceparent via
 * IARM_Bus_SetTraceparent(). _BusCall_FuncWrapper detects the magic, peels the
 * envelope, and calls the registered handler with inner_arg only - the
 * handler never sees the envelope.
 */
typedef struct {
    uint32_t magic;                     /* IARM_RPC_TP_MAGIC          */
    char     traceparent[IARM_TP_LEN + 1]; /* W3C traceparent + NUL   */
    size_t   inner_len;                 /* sizeof(original arg struct) */
    char     inner_arg[];                /* original arg - handler sees */
} IARM_RPC_TP_Envelope_t;

/* ── Validation ──────────────────────────────────────────────────────────── */

/**
 * Validate a W3C traceparent string.
 * Requirements: exactly 55 chars, version field "00-", '-' delimiters in place.
 * Returns 1 if valid, 0 otherwise.
 */
static inline int iarm_tp_valid(const char *tp)
{
    if (!tp) return 0;
    if (strnlen(tp, IARM_TP_LEN + 2) != IARM_TP_LEN) return 0;
    if (tp[0] != '0' || tp[1] != '0' || tp[2] != '-') return 0;
    if (tp[35] != '-' || tp[52] != '-') return 0;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* OTEL_ENABLED */

#endif /* IARM_TP_H */
