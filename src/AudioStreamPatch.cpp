#include "AudioStreamPatch.h"

#include <string.h>
#include <stdlib.h>

/*
 * Scanning rule, learned the hard way: NOTHING mutates the document until every
 * structural scan is finished.
 *
 * The first cut of this parser NUL-terminated each string as it read it. That
 * works for one field and destroys the rest: strchr() then stops at the NUL, so
 * "params" was invisible after "type" had been read, and "connections" was
 * invisible after the first module id. Every scan below is therefore bounded by
 * an explicit end pointer and never relies on NUL. Strings are captured as
 * (ptr,len) spans and only terminated in a final pass, once nothing else needs
 * to read past them.
 */

namespace {

struct Span { char *p; int n; };

bool span_eq(const Span &a, const Span &b) {
  return a.n == b.n && a.p && b.p && memcmp(a.p, b.p, (size_t)a.n) == 0;
}

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

char *skip_ws(char *p, const char *end) {
  while (p < end && is_ws(*p)) ++p;
  return p;
}

bool is_num_start(char c) {
  return c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9');
}

/** Next unescaped quote in [p,end). */
char *next_quote(char *p, const char *end) {
  while (p < end) {
    if (*p == '\\') { p += 2; continue; }
    if (*p == '"') return p;
    ++p;
  }
  return 0;
}

/** Find `"key"` in [p,end). Returns the opening quote. Bounded; no strchr. */
char *find_key(char *p, const char *end, const char *key) {
  const int klen = (int)strlen(key);
  while (p < end) {
    char *q = next_quote(p, end);
    if (!q) return 0;
    if (q + 1 + klen + 1 <= end &&
        memcmp(q + 1, key, (size_t)klen) == 0 && q[1 + klen] == '"') {
      return q;
    }
    /* Step past this string entirely so a value cannot be read as a key. */
    char *close = next_quote(q + 1, end);
    p = close ? close + 1 : (char *)end;
  }
  return 0;
}

/** From `{` or `[`, the matching close within [open,end). Quote aware. */
char *match_brace(char *open, const char *end) {
  const char o = *open;
  const char c = (o == '{') ? '}' : ']';
  int depth = 0;
  bool in_str = false;
  for (char *p = open; p < end; ++p) {
    if (in_str) {
      if (*p == '\\') { ++p; continue; }
      if (*p == '"') in_str = false;
      continue;
    }
    if (*p == '"') { in_str = true; continue; }
    if (*p == o) ++depth;
    else if (*p == c && --depth == 0) return p;
  }
  return 0;
}

/** Step over `:` after a key to its value. */
char *value_after(char *key_quote, const char *end, const char *key) {
  char *p = key_quote + 1 + (int)strlen(key) + 1;
  p = skip_ws(p, end);
  if (p >= end || *p != ':') return 0;
  return skip_ws(p + 1, end);
}

/** `"key": "string"` as a span. Does NOT mutate. */
bool read_span(char *start, const char *end, const char *key, Span *out) {
  char *k = find_key(start, end, key);
  if (!k) return false;
  char *v = value_after(k, end, key);
  if (!v || v >= end || *v != '"') return false;
  char *close = next_quote(v + 1, end);
  if (!close) return false;
  out->p = v + 1;
  out->n = (int)(close - (v + 1));
  return true;
}

bool read_number(char *start, const char *end, const char *key, float *out) {
  char *k = find_key(start, end, key);
  if (!k) return false;
  char *v = value_after(k, end, key);
  if (!v || v >= end || !is_num_start(*v)) return false;
  *out = (float)strtod(v, 0);
  return true;
}

struct ParamSlot { const char *name; int slot; };

/* Param names are the catalog's own (isystem-stream-canvas.js PARAM_DEFS). A
 * name not listed here is ignored rather than guessed at. */
const ParamSlot kOscSlots[] = {
  { "wave", AUDIO_STREAM_OSC_WAVE },     { "level", AUDIO_STREAM_OSC_LEVEL },
  { "octave", AUDIO_STREAM_OSC_OCTAVE }, { "coarse", AUDIO_STREAM_OSC_COARSE },
  { "detune", AUDIO_STREAM_OSC_DETUNE }, { 0, 0 }
};
const ParamSlot kFilterSlots[] = {
  { "cutoff", AUDIO_STREAM_FILTER_CUTOFF }, { "resonance", AUDIO_STREAM_FILTER_RESONANCE }, { 0, 0 }
};
const ParamSlot kDelaySlots[] = {
  { "time", AUDIO_STREAM_DELAY_TIME }, { "feedback", AUDIO_STREAM_DELAY_FEEDBACK },
  { "wet", AUDIO_STREAM_DELAY_WET },   { 0, 0 }
};
const ParamSlot kDistSlots[] = {
  { "drive", AUDIO_STREAM_DIST_DRIVE }, { "tone", AUDIO_STREAM_DIST_TONE },
  { "wet", AUDIO_STREAM_DIST_WET },     { 0, 0 }
};
const ParamSlot kGainSlots[] = {
  { "gain", AUDIO_STREAM_GAIN_GAIN }, { "mute", AUDIO_STREAM_GAIN_MUTE }, { 0, 0 }
};

const ParamSlot *slots_for(AudioStreamNodeKind k) {
  switch (k) {
    case AUDIO_STREAM_NODE_OSCILLATOR: return kOscSlots;
    case AUDIO_STREAM_NODE_FILTER:     return kFilterSlots;
    case AUDIO_STREAM_NODE_DELAY:      return kDelaySlots;
    case AUDIO_STREAM_NODE_DISTORTION: return kDistSlots;
    case AUDIO_STREAM_NODE_GAIN:       return kGainSlots;
    default: return 0;
  }
}

/** Kind from a type span, without needing it NUL-terminated yet. */
AudioStreamNodeKind kind_of_span(const Span &s) {
  char tmp[64];
  int n = s.n;
  if (n < 0) n = 0;
  if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;
  if (s.p && n > 0) memcpy(tmp, s.p, (size_t)n);
  tmp[n] = '\0';
  return audio_stream_kind_from_type(tmp);
}

}  // namespace

const char *audio_stream_patch_status_text(AudioStreamPatchStatus s) {
  switch (s) {
    case AUDIO_STREAM_PATCH_OK: return "ok";
    case AUDIO_STREAM_PATCH_ERR_EMPTY: return "empty patch";
    case AUDIO_STREAM_PATCH_ERR_NO_MODULES: return "no modules object";
    case AUDIO_STREAM_PATCH_ERR_TOO_MANY: return "patch exceeds the node or edge cap";
    case AUDIO_STREAM_PATCH_ERR_MALFORMED: return "patch did not scan";
    default: return "unknown";
  }
}

AudioStreamPatchResult audio_stream_patch_parse(char *json,
                                                AudioStreamNodeDesc *nodes,
                                                int max_nodes,
                                                AudioStreamEdgeDesc *edges,
                                                int max_edges) {
  AudioStreamPatchResult r;
  memset(&r, 0, sizeof(r));

  if (!json || !*json || !nodes || max_nodes <= 0) {
    r.status = AUDIO_STREAM_PATCH_ERR_EMPTY;
    return r;
  }
  if (max_edges < 0) max_edges = 0;
  if (max_nodes > AUDIO_STREAM_GRAPH_MAX_NODES) max_nodes = AUDIO_STREAM_GRAPH_MAX_NODES;

  char *const doc_end = json + strlen(json);

  char *mk = find_key(json, doc_end, "modules");
  if (!mk) {
    r.status = AUDIO_STREAM_PATCH_ERR_NO_MODULES;
    return r;
  }
  char *mv = value_after(mk, doc_end, "modules");
  if (!mv || *mv != '{') {
    r.status = AUDIO_STREAM_PATCH_ERR_MALFORMED;
    return r;
  }
  char *m_end = match_brace(mv, doc_end);
  if (!m_end) {
    r.status = AUDIO_STREAM_PATCH_ERR_MALFORMED;
    return r;
  }

  Span ids[AUDIO_STREAM_GRAPH_MAX_NODES];
  Span types[AUDIO_STREAM_GRAPH_MAX_NODES];
  memset(ids, 0, sizeof(ids));
  memset(types, 0, sizeof(types));

  /* Walk entries by brace, not by comma — a nested params object must not be
   * mistaken for the next module. */
  char *p = mv + 1;
  while (p < m_end) {
    p = skip_ws(p, m_end);
    if (p < m_end && *p == ',') { ++p; continue; }
    if (p >= m_end || *p != '"') break;

    char *id_start = p + 1;
    char *id_close = next_quote(id_start, m_end);
    if (!id_close) break;

    char *body = skip_ws(id_close + 1, m_end);
    if (body >= m_end || *body != ':') break;
    body = skip_ws(body + 1, m_end);
    if (body >= m_end || *body != '{') break;
    char *body_end = match_brace(body, m_end + 1);
    if (!body_end || body_end > m_end) break;

    if (r.node_count >= max_nodes) {
      /* Cap hit. Count and keep scanning, so the number reported is the real
       * total rather than "the cap plus one". */
      r.dropped_nodes++;
      p = body_end + 1;
      continue;
    }

    Span type = { 0, 0 };
    read_span(body, body_end, "type", &type);

    AudioStreamNodeDesc &d = nodes[r.node_count];
    memset(&d, 0, sizeof(d));
    d.kind = kind_of_span(type);
    d.type = "";  /* filled in the termination pass below */

    const ParamSlot *slots = slots_for(d.kind);
    if (slots) {
      char *pk = find_key(body, body_end, "params");
      char *pv = pk ? value_after(pk, body_end, "params") : 0;
      if (pv && *pv == '{') {
        char *p_end = match_brace(pv, body_end + 1);
        if (p_end && p_end <= body_end) {
          for (int si = 0; slots[si].name; ++si) {
            char *nk = find_key(pv, p_end, slots[si].name);
            if (!nk) continue;
            char *nv = value_after(nk, p_end, slots[si].name);
            if (!nv) continue;
            float f = 0.0f;
            bool got = false;
            if (*nv == '{') {
              /* The desk stores a descriptor, not a bare number. */
              char *nv_end = match_brace(nv, p_end + 1);
              if (nv_end && nv_end <= p_end) got = read_number(nv, nv_end, "value", &f);
            } else if (is_num_start(*nv)) {
              f = (float)strtod(nv, 0);
              got = true;
            }
            if (got) {
              d.p[slots[si].slot] = f;
              d.p_set[slots[si].slot] = true;
            }
          }
        }
      }
    }

    ids[r.node_count].p = id_start;
    ids[r.node_count].n = (int)(id_close - id_start);
    types[r.node_count] = type;
    r.node_count++;
    p = body_end + 1;
  }

  if (r.node_count == 0) {
    r.status = AUDIO_STREAM_PATCH_ERR_NO_MODULES;
    return r;
  }

  /* Connections. Still no mutation — ids are compared as spans. */
  char *ck = find_key(json, doc_end, "connections");
  char *cv = ck ? value_after(ck, doc_end, "connections") : 0;
  if (cv && *cv == '[' && edges && max_edges > 0) {
    char *c_end = match_brace(cv, doc_end);
    if (c_end) {
      char *q = cv + 1;
      while (q < c_end) {
        q = skip_ws(q, c_end);
        if (q < c_end && *q == ',') { ++q; continue; }
        if (q >= c_end || *q != '{') break;
        char *e_end = match_brace(q, c_end + 1);
        if (!e_end || e_end > c_end) break;

        Span fa = { 0, 0 }, tb = { 0, 0 };
        char *fk = find_key(q, e_end, "from");
        char *fv = fk ? value_after(fk, e_end, "from") : 0;
        if (fv && *fv == '{') {
          char *fe = match_brace(fv, e_end + 1);
          if (fe && fe <= e_end) read_span(fv, fe, "module", &fa);
        }
        char *tk = find_key(q, e_end, "to");
        char *tv = tk ? value_after(tk, e_end, "to") : 0;
        if (tv && *tv == '{') {
          char *te = match_brace(tv, e_end + 1);
          if (te && te <= e_end) read_span(tv, te, "module", &tb);
        }

        int ai = -1, bi = -1;
        for (int i = 0; i < r.node_count; ++i) {
          if (ai < 0 && span_eq(ids[i], fa)) ai = i;
          if (bi < 0 && span_eq(ids[i], tb)) bi = i;
        }

        if (ai < 0 || bi < 0 || ai == bi) {
          /* A wire to a module that is not here, or to itself. Counted, never
           * silently swallowed — it usually means a truncated patch. */
          r.dropped_edges++;
        } else if (r.edge_count >= max_edges) {
          r.dropped_edges++;
        } else {
          edges[r.edge_count].from = (uint8_t)ai;
          edges[r.edge_count].to = (uint8_t)bi;
          r.edge_count++;
        }
        q = e_end + 1;
      }
    }
  }

  /* Only now, with every scan finished, terminate the borrowed type strings. */
  for (int i = 0; i < r.node_count; ++i) {
    if (types[i].p && types[i].n >= 0) {
      types[i].p[types[i].n] = '\0';
      nodes[i].type = types[i].p;
    } else {
      nodes[i].type = "";
    }
  }

  r.status = (r.dropped_nodes > 0) ? AUDIO_STREAM_PATCH_ERR_TOO_MANY : AUDIO_STREAM_PATCH_OK;
  return r;
}

int audio_stream_param_slot(AudioStreamNodeKind kind, const char *name) {
  const ParamSlot *t = slots_for(kind);
  if (!t || !name) return -1;
  for (int i = 0; t[i].name; ++i)
    if (strcmp(t[i].name, name) == 0) return t[i].slot;
  return -1;
}

const char *audio_stream_param_name(AudioStreamNodeKind kind, int slot) {
  const ParamSlot *t = slots_for(kind);
  if (!t) return 0;
  for (int i = 0; t[i].name; ++i)
    if (t[i].slot == slot) return t[i].name;
  return 0;
}
