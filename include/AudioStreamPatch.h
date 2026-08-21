#pragma once
#ifndef ESPAUDIO_AUDIO_STREAM_PATCH_H
#define ESPAUDIO_AUDIO_STREAM_PATCH_H

#include "AudioStreamGraph.h"

/** Turns a CraftAudio patch into something AudioStreamGraph can render.
 *
 *  The patch is the `kind: isystem-stream` JSON the desk already produces — the
 *  same document the browser previews and the .sth carries. Nothing is
 *  re-encoded on the way to the board, so there is one patch format and no
 *  second dialect to keep in step.
 *
 *  ── Why a hand-rolled scanner ───────────────────────────────────────────────
 *  This library has no dependencies and that is a feature. A general JSON parser
 *  is not needed here: the document is machine-generated and regular, and only
 *  four things are read out of it — module ids, module types, numeric param
 *  values, and the connection pairs. Everything else is skipped. If the patch
 *  format ever stops being machine-generated, replace this with a real parser
 *  rather than growing this one.
 *
 *  Strings are borrowed, not copied: `parse()` writes into a caller-owned
 *  buffer, and the type pointers in the node array point INTO that buffer. Keep
 *  it alive as long as the graph is.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Longest module id the loader will match. Ids are generated (`o1`, `fl1`). */
#ifndef AUDIO_STREAM_PATCH_MAX_ID
#define AUDIO_STREAM_PATCH_MAX_ID 24
#endif

typedef enum {
  AUDIO_STREAM_PATCH_OK = 0,
  AUDIO_STREAM_PATCH_ERR_EMPTY,        /**< nothing to parse */
  AUDIO_STREAM_PATCH_ERR_NO_MODULES,   /**< no "modules" object found */
  AUDIO_STREAM_PATCH_ERR_TOO_MANY,     /**< more nodes or edges than the caps allow */
  AUDIO_STREAM_PATCH_ERR_MALFORMED     /**< structure did not scan */
} AudioStreamPatchStatus;

typedef struct {
  AudioStreamPatchStatus status;
  int node_count;
  int edge_count;
  /** Modules seen but dropped because the node cap was hit. Never silent. */
  int dropped_nodes;
  /** Connections naming a module that is not in the patch, or self-loops. */
  int dropped_edges;
} AudioStreamPatchResult;

/**
 * Parse `json` (NUL-terminated, MUTATED in place to NUL-terminate borrowed
 * strings) into `nodes` / `edges`.
 *
 * Returns a result rather than a bool: a patch that half-loaded must be able to
 * say so, because silently rendering three of five modules is exactly the
 * failure this project keeps trying to avoid.
 */
AudioStreamPatchResult audio_stream_patch_parse(char *json,
                                                AudioStreamNodeDesc *nodes,
                                                int max_nodes,
                                                AudioStreamEdgeDesc *edges,
                                                int max_edges);

/** Human-readable form of a status, for logs and the board console. */
const char *audio_stream_patch_status_text(AudioStreamPatchStatus s);

/** Catalog param name -> slot index, or -1 when the kind has no such param.
 *  The loader's own table, exposed: a controller turns "cutoff" into a slot
 *  number through this rather than keeping a second copy of the mapping. */
int audio_stream_param_slot(AudioStreamNodeKind kind, const char *name);

/** Slot index -> catalog param name, or 0 when the kind has no such slot. The
 *  inverse of the above; together they are what a JS<->C++ cross-check compares. */
const char *audio_stream_param_name(AudioStreamNodeKind kind, int slot);

#ifdef __cplusplus
}
#endif
#endif /* ESPAUDIO_AUDIO_STREAM_PATCH_H */
