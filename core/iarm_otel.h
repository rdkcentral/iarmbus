/**
 * IARM-Bus OpenTelemetry context propagation helpers.
 *
 * Strategy: resolve librdk_otlp.so symbols lazily using dlsym(RTLD_DEFAULT).
 * If the tracer library is not loaded in a process, pointers remain NULL and
 * IARM tracing propagation logic becomes a no-op with near-zero overhead.
 *
 * Build note: libIARMBus does not require direct compile-time references to
 * rdk_otlp_* symbols with this approach.
 */

#ifndef IARM_OTEL_H
#define IARM_OTEL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Event path ─────────────────────────────────────────────────────────── */

/**
 * Magic byte appended at eventData->data[len] to signal that the next 56 bytes
 * carry a W3C traceparent string (55 chars + NUL terminator).
 * eventData->len is left unchanged, so legacy receivers that read only
 * data[0..len-1] are completely unaffected.
 * The shared-memory allocation for the event is enlarged by IARM_OTEL_SUFFIX_SIZE
 * on every BroadcastEvent call (new libIARMBus), so data[len] is always a valid
 * byte for new receivers to read.
 */
#define IARM_OTEL_EVENT_MAGIC   ((unsigned char)0xAA)

/** W3C traceparent length: "00-<32hex>-<16hex>-<2hex>" = exactly 55 characters */
#define IARM_OTEL_TP_LEN        55

/** Bytes appended after user data: 1 (magic) + 55 (traceparent) + 1 (NUL) = 57 */
#define IARM_OTEL_SUFFIX_SIZE   57

/* ── RPC path ────────────────────────────────────────────────────────────── */

/** Magic identifying an IARM_RPC_Envelope_t as the arg in _BusCall_FuncWrapper.
 *  ASCII value of 'RPCA'. */
#define IARM_OTEL_RPC_MAGIC     0x52504341U

/**
 * RPC tracing envelope.
 *
 * A valid current W3C traceparent is automatically wrapped into this envelope
 * by IARM_Bus_Call() when the caller is already inside a traced flow. This
 * preserves the original API contract while allowing the receiver to access the
 * incoming traceparent without changing the handler's argument layout.
 *
 * IARM_Bus_CallWithTracing() remains as a compatibility alias for the same
 * transparent behavior when an older caller explicitly asks for it.
 *
 * _BusCall_FuncWrapper detects the magic, peels the envelope, and calls the
 * registered handler with inner_arg only. The handler never sees the envelope.
 */
typedef struct {
    uint32_t magic;                              /* IARM_OTEL_RPC_MAGIC          */
    char     traceparent[IARM_OTEL_TP_LEN + 1]; /* W3C traceparent + NUL        */
    size_t   inner_len;                          /* sizeof(original arg struct)  */
    char     inner_arg[];                        /* original arg — handler sees  */
} IARM_RPC_Envelope_t;

/* ── Validation ──────────────────────────────────────────────────────────── */

/**
 * Validate a W3C traceparent string.
 * Requirements: exactly 55 chars, version field "00-".
 * Returns 1 if valid, 0 otherwise.
 */
static inline int iarm_tp_valid(const char *tp)
{
    if (!tp) return 0;
    /* Use limit 57 to avoid over-reading the suffix buffer */
    if (strnlen(tp, IARM_OTEL_TP_LEN + 2) != IARM_OTEL_TP_LEN) return 0;
    if (tp[0] != '0' || tp[1] != '0' || tp[2] != '-') return 0;
    return 1;
}

/* ── Optional OTEL symbol resolution (dlsym) ───────────────────────────── */

typedef const char *(*iarm_fn_get_tp_t)(void);

static iarm_fn_get_tp_t s_iarm_get_traceparent = NULL;
static pthread_once_t s_iarm_otel_once = PTHREAD_ONCE_INIT;

static inline void iarm_otel_resolve_symbols(void)
{
    s_iarm_get_traceparent = (iarm_fn_get_tp_t)
        dlsym(RTLD_DEFAULT, "rdk_otlp_get_current_traceparent");
}

static inline const char *iarm_otel_get_current_traceparent(void)
{
    pthread_once(&s_iarm_otel_once, iarm_otel_resolve_symbols);
    if (!s_iarm_get_traceparent) {
        return NULL;
    }
    return s_iarm_get_traceparent();
}

#ifdef __cplusplus
}
#endif

#endif /* IARM_OTEL_H */
