#pragma once
namespace armxbox {
// Normal host code only. Never install as, or call from, a signal handler.
void traceLine(const char *);
} // namespace armxbox
