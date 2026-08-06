#pragma once

#include <functional>

// Runs body on a detached thread with a stack big enough for the JSON parser
// and the HTTP client.
//
// libnx hands std::thread a small stack and nlohmann's recursive-descent parser
// overruns it on a feed-sized document. The failure does not look like a parse
// error: it is a data abort with a backtrace full of unrelated frames, because
// the unwinder is walking a smashed stack. Hence an explicit stack size.
void runDetached(std::function<void()> body);
