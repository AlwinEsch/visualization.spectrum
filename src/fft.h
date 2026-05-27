#pragma once

#define N 512 /* size of the DFT */

void calc_freq(const float data[N], float freq[N / 2]);
