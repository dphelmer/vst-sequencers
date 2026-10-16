#!/usr/bin/env node
// Generates tests/golden.h from the original Max JS source
// (meshuggah-polymeter-core-live-step.js) so PolyriffSim can assert bit-parity
// with the patch. Run: node tools/gen_golden.js [out.h]   (from polyriff/)

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const jsPath = path.resolve(
	__dirname,
	'../../../max-patches/sequencers/generative/meshuggah-polymeter-core-live-step.js');
const outPath = process.argv[2] || path.resolve(__dirname, '../tests/golden.h');

const src = fs.readFileSync(jsPath, 'utf8');
const captured = [];
const ctx = {
	outlet: (lane, ...args) => captured.push({ lane, args }),
	post: () => {},
	Task: function () { this.cancel = () => {}; this.schedule = () => {}; },
	Math,
};
vm.createContext(ctx);
vm.runInContext(src, ctx);

function setParams (p) {
	if ('seed' in p) ctx.seed(p.seed);
	if ('riffLen' in p) ctx.riff_len(p.riffLen);
	if ('hyperBars' in p) ctx.hyper_bars(p.hyperBars);
	if ('density' in p) ctx.density(p.density);
	if ('groupSpread' in p) ctx.group_spread(p.groupSpread);
	if ('pitchLow' in p) ctx.pitch_low(p.pitchLow);
	if ('pitchRange' in p) ctx.pitch_range(p.pitchRange);
	if ('pitchDensity' in p) ctx.pitch_density(p.pitchDensity);
	if ('kickMode' in p) ctx.kick_mode(p.kickMode);
	if ('accentVel' in p) ctx.accent_vel(p.accentVel);
	if ('gatePct' in p) ctx.gate_pct(p.gatePct);
	if ('slidePct' in p) ctx.slide_pct(p.slidePct);
}

function riffOf () {
	ctx.reseed();
	const r = ctx.computeRiff();
	return {
		pitches: Array.from(r.pitches),
		vels: Array.from(r.velocities),
		hits: Array.from(r.isHit),
		accents: Array.from(r.isAccent),
	};
}

function groupsOf (n, spread, seed) {
	return Array.from(ctx.makeGroups(n, spread, ctx.mulberry32(seed)));
}

// Slide rolls are drawn by pushAll in display order (rotation-dependent). Capture
// its "step" messages: with gate 100% a duration > 120 ticks means the 240-tick
// slide bump fired for the source index under that display slot.
function slidesOf (shift) {
	ctx._rotShift = shift;
	ctx.reseed();
	captured.length = 0;
	ctx.pushAll();
	const n = ctx._riffLen;
	const riff = new Array(n).fill(0);
	const kick = new Array(n).fill(0);
	for (const { lane, args } of captured) {
		if (args[0] !== 'step') continue;
		const slot = args[1] - 1;
		if (slot < 0 || slot >= n) continue;
		const src = ((slot - shift) % n + n) % n;
		const vel = args[3];
		const dur = args[4];
		if (lane === 0 && vel > 0) riff[src] = dur > 120 ? 1 : 0;
		if (lane === 1 && vel > 0) kick[src] = dur > 120 ? 1 : 0;
	}
	return { riff, kick };
}

function fnv1a32 (s) {
	let h = 2166136261;
	for (let i = 0; i < s.length; ++i) {
		h ^= s.charCodeAt(i);
		h = Math.imul(h, 16777619) >>> 0;
	}
	return h >>> 0;
}

const lines = [];
const u8 = (name, arr) =>
	lines.push(`static const std::uint8_t ${name}[${arr.length}] = {${arr.join(', ')}};`);
const u32 = (name, arr) =>
	lines.push(`static const std::uint32_t ${name}[${arr.length}] = {${arr.join(', ')}};`);

// --- mulberry32 stream (pins the RNG itself)
const rng = ctx.mulberry32(12345);
const stream = [];
for (let i = 0; i < 8; ++i) stream.push(Math.round(rng() * 4294967296));
u32('kGoldenRng', stream);

// --- defaults: seed 12345, N 21, spread 50, density 70, pitch 28/3/30,
//    accent 127, kick mode Follow, slide 0
setParams({ seed: 12345, riffLen: 21, density: 70, groupSpread: 50, pitchLow: 28,
	pitchRange: 3, pitchDensity: 30, kickMode: 1, accentVel: 127, gatePct: 100, slidePct: 0 });
{
	const g = groupsOf(21, 50, 12345);
	lines.push(`static const std::uint8_t kGoldenGroups0[${g.length}] = {${g.join(', ')}};`);
	lines.push(`static const int kGoldenGroups0Count = ${g.length};`);
	const r = riffOf();
	u8('kGoldenPitch0', r.pitches);
	u8('kGoldenRiffVel0', r.vels);
	u8('kGoldenAccent0', r.accents.map(Number));
	// kick lane comes from pushAll's own computation; read it from a capture:
	{
		ctx._rotShift = 0;
		ctx.reseed();
		captured.length = 0;
		ctx.pushAll();
		const kv = new Array(21).fill(0);
		for (const { lane, args } of captured) {
			if (lane === 1 && args[0] === 'step' && args[1] - 1 < 21) kv[args[1] - 1] = args[3];
		}
		u8('kGoldenKickVel0', kv);
	}
}

// --- density 0: only group starts hit (all accents); their pitch rolls still happen
setParams({ density: 0 });
{
	const r = riffOf();
	u8('kGoldenRiffVelD0', r.vels);
	u8('kGoldenPitchD0', r.pitches);
}

// --- kick mode Accent (2): kick only on group starts
setParams({ density: 70, kickMode: 2 });
{
	ctx._rotShift = 0;
	ctx.reseed();
	captured.length = 0;
	ctx.pushAll();
	const kv = new Array(21).fill(0);
	for (const { lane, args } of captured) {
		if (lane === 1 && args[0] === 'step' && args[1] - 1 < 21) kv[args[1] - 1] = args[3];
	}
	u8('kGoldenKickVelM2', kv);
}

// --- second riff profile: seed 999, spread 80, density 55, pitch 40/6/60, accent 100
setParams({ seed: 999, riffLen: 21, density: 55, groupSpread: 80, pitchLow: 40,
	pitchRange: 6, pitchDensity: 60, kickMode: 1, accentVel: 100, slidePct: 0 });
{
	const g = groupsOf(21, 80, 999);
	lines.push(`static const std::uint8_t kGoldenGroups1[${g.length}] = {${g.join(', ')}};`);
	lines.push(`static const int kGoldenGroups1Count = ${g.length};`);
	const r = riffOf();
	u8('kGoldenPitch1', r.pitches);
	u8('kGoldenRiffVel1', r.vels);
	u8('kGoldenAccent1', r.accents.map(Number));
	ctx._rotShift = 0;
	ctx.reseed();
	captured.length = 0;
	ctx.pushAll();
	const kv = new Array(21).fill(0);
	for (const { lane, args } of captured) {
		if (lane === 1 && args[0] === 'step' && args[1] - 1 < 21) kv[args[1] - 1] = args[3];
	}
	u8('kGoldenKickVel1', kv);
}

// --- slides: defaults + slide 40%, rotation shift 0 and 5 (pushAll display order)
setParams({ seed: 12345, riffLen: 21, density: 70, groupSpread: 50, pitchLow: 28,
	pitchRange: 3, pitchDensity: 30, kickMode: 1, accentVel: 127, gatePct: 100, slidePct: 40 });
{
	const s0 = slidesOf(0);
	u8('kGoldenRiffSlide0', s0.riff);
	u8('kGoldenKickSlide0', s0.kick);
	const s5 = slidesOf(5);
	u8('kGoldenRiffSlide5', s5.riff);
	u8('kGoldenKickSlide5', s5.kick);
}

const header = `// Generated by tools/gen_golden.js from meshuggah-polymeter-core-live-step.js.
// Do not edit by hand -- re-run the script after changing the port's expectations.
#pragma once

#include <cstdint>

namespace Polyriff {
namespace golden {

${lines.join('\n')}

constexpr std::uint32_t kGoldenFnvHello = ${fnv1a32('hello')}u; // paramFromString(kSeed, "hello")

}
}
`;

fs.writeFileSync(outPath, header);
console.log(`wrote ${outPath}`);
