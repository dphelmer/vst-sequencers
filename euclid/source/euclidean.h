#pragma once

namespace Euclid {

// Hit at pattern position i for Euclidean rhythm E(pulses, steps) rotated by rot.
// Classic construction: distribute pulses as evenly as possible, pattern starts on a hit.
// Pure function; no SDK dependencies.
inline bool euclideanHit (int pulses, int steps, int rot, int i)
{
	if (steps <= 0 || pulses <= 0)
		return false;
	if (pulses > steps)
		pulses = steps;
	const int j = ((i - rot) % steps + steps) % steps;
	return (j * pulses) % steps < pulses;
}

}
