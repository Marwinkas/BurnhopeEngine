#pragma once

namespace burnhope {

// Spawn the desktop default handler. False when the process does not start.
bool desktopOpen(const char* path);

// Show paths in the session file manager. Falls back to opening the parent folder.
void desktopReveal(const char* const* paths, int count);

} // namespace burnhope
