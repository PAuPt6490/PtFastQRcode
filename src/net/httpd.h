// PtFastQRcode - tiny LAN-only HTTP server.
//
// It exists for one purpose: a picture pasted into the popup becomes a QR code that a
// phone on the same network can open. Everything is served from memory, so no file is
// written, nothing survives a restart and nothing outside the local network can reach it.
//
// Design notes:
//   * Winsock is a plain static import (ws2_32.dll ships with every Windows since 95);
//     the socket is only opened when the image server is switched on, so the tray process
//     never loads the network stack on its own.
//   * WSAAsyncSelect posts its events to the controller window, so there is no extra
//     thread and no shared state: accept and read happen on the UI thread.
//   * Only 192.168/16, 10/8 and 172.16/12 peers are answered; anyone else is dropped
//     before a single byte is parsed.
#pragma once

// Winsock's message hook is the point here: it is what keeps the server on the UI thread.
#define _WINSOCK_DEPRECATED_NO_WARNINGS

// winsock2.h has to come before windows.h, otherwise the old winsock.h wins and every
// symbol below collides with its 1.1 twin.
#include <winsock2.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

// COM is created on demand (WIC). Declared in the global namespace, matching app.h, so
// both units bind to the same definition.
bool ensureCom();

namespace net {

// Posted to the window passed to start(): wParam = socket, LOWORD(lParam) = event.
#define WM_APP_HTTP (WM_APP + 20)

// Binds INADDR_ANY on a free port and registers for socket events. False when the
// network stack or the bind fails.
bool start(HWND window);

void stop();

// How long a published link stays alive, in minutes (1..120). Takes effect on the next
// expiry check, so changing it also shrinks links that are already out.
void setLifetime(int seconds);

// True while the listening socket exists.
bool running();

// True when nothing is cached and no connection is open: the server has nothing left to do
// and can be stopped to give its memory back.
bool idle();

// Printed address for the QR code, e.g. "http://192.168.1.23:51422".
std::wstring baseUrl();

// The IPv4 address the default route uses. Empty when the machine is offline.
std::wstring localAddress();

enum class Kind : uint8_t { Html, Png };

// Publishes one payload under a fresh random token and returns the path ("/s/3f9a"). The
// Png variant is served as "<token>.png" so a page can embed it.
std::wstring publish(const void* data, std::size_t size, Kind kind, bool& ok);

// Serves the request that just arrived on 'socket'. Returns false when the socket was not
// one of the connections this server accepted (a notification from a stale session).
bool handleEvent(WPARAM socket, LPARAM event);

// ---------------------------------------------------------------- helpers shared with the
// popup: picture -> PNG bytes.

// Wraps a published path so a page can be shown on a phone.
std::wstring pageUrl(const std::wstring& path);

// Encodes a bitmap as PNG in memory (false when the bitmap could not be read).
bool encodeBitmapAsPng(HBITMAP bitmap, std::string& png);

// Same, for raw top-down 32bpp BGRA pixels.
bool encodePixelsAsPng(const void* pixels, int width, int height, std::string& png);

}  // namespace net
