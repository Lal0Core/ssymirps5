// Full-screen screenshots, one bounded background job. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace screenshot
{
uint32_t PollButtons(uint32_t raw, double now); // called under the input mutex
bool TakeMenuRequest(); // consumes a touchpad release after at most 1 second, independent of captures being enabled
void Configure(bool enabled);                 // presentation thread
bool Pending();
void Submit(const uint8_t* pixels, int w, int h, bool bgra=false);
void Fail();
void Shutdown();
}
