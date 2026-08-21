#pragma once
#ifndef ESPAUDIO_H
#define ESPAUDIO_H
#include "AudioAnalyzeAdc.h"
#include "AudioDbeamCc1.h"
#include "AudioSynthWavetable8.h"
#include "AudioSynthHarp8.h"
#include "AudioEffectBasic8.h"
#include "AudioGranularOla.h"
#include "AudioButtonGpio.h"
#include "AudioMidiUsb.h"
#include "AudioOutputI2S.h"
#include "AudioCodec.h"

/* Renders a CraftAudio patch as DATA. Needs isystem_dsp_kernels.h exported
 * from CraftAudio on your include path — this library ships the interpreter,
 * you bring the kernels. Guarded so the rest of EspAudio still builds without
 * it. Define ESPAUDIO_WITH_STREAM_GRAPH to pull it in. */
#ifdef ESPAUDIO_WITH_STREAM_GRAPH
#include "AudioStreamGraph.h"
#include "AudioStreamPatch.h"
/* Live param writes over SysEx. Same guard: it references the graph, so it
 * is only useful where the kernels are present. */
#include "AudioStreamControl.h"
#endif
#endif
