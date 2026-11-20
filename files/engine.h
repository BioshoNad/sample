// ============================================================================
//  LiteEngine  -  a tiny, dependency-free 3D game engine for Windows
//  Platform : Win32 + OpenGL 1.x (fixed pipeline)  - no SDL, no GLFW, no GLEW
//  Provides : window + GL context, input, timing, math helpers, cube/box
//             drawing, procedural textures, bitmap text, 2D overlay helpers.
//  The game (main.cpp) is built ON TOP of this header.
// ============================================================================
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace lite {

// ------------------------------------------------------------------ math ---
constexpr float PI = 3.14159265f;
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float frand() { return (float)rand() / (float)RAND_MAX; }
inline float frange(float a, float b) { return a + (b - a) * frand(); }
inline float wrapAngle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

// -------------------------------------------------------------- platform ---
struct Platform {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    int w = 1280, h = 720;
    bool running = true;
    bool down[256] = {};
    bool pressed[256] = {};
    GLuint fontSmall = 0, fontBig = 0;
    LARGE_INTEGER freq, last;
};
inline Platform& plat() { static Platform p; return p; }

inline bool key(int vk) { return plat().down[vk & 255]; }
inline bool tapped(int vk) { return plat().pressed[vk & 255]; }

inline LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Platform& p = plat();
    switch (m) {
    case WM_CLOSE: p.running = false; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_KEYDOWN:
        if (wp < 256) { if (!p.down[wp]) p.pressed[wp] = true; p.down[wp] = true; }
        return 0;
    case WM_KEYUP:
        if (wp < 256) p.down[wp] = false;
        return 0;
    case WM_KILLFOCUS: memset(p.down, 0, sizeof(p.down)); return 0;
    case WM_SIZE: p.w = LOWORD(lp); p.h = HIWORD(lp); return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

inline GLuint makeFont(HDC hdc, int height) {
    HFONT f = CreateFontA(-height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          FF_DONTCARE | DEFAULT_PITCH, "Consolas");
    SelectObject(hdc, f);
    GLuint base = glGenLists(96);
    wglUseFontBitmapsA(hdc, 32, 96, base);
    return base;
}

inline bool init(const char* title, int w, int h) {
    Platform& p = plat();
    p.w = w; p.h = h;
    HINSTANCE hi = GetModuleHandleA(nullptr);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "LiteEngineWnd";
    RegisterClassA(&wc);
    RECT r = {0, 0, w, h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    p.hwnd = CreateWindowExA(0, "LiteEngineWnd", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, hi, nullptr);
    if (!p.hwnd) return false;
    p.hdc = GetDC(p.hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(p.hdc, &pfd);
    if (!pf || !SetPixelFormat(p.hdc, pf, &pfd)) return false;
    p.rc = wglCreateContext(p.hdc);
    if (!p.rc || !wglMakeCurrent(p.hdc, p.rc)) return false;
    typedef BOOL(WINAPI * SwapFn)(int);
    SwapFn sw = (SwapFn)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    if (sw) sw(1);  // vsync
    p.fontSmall = makeFont(p.hdc, 15);
    p.fontBig = makeFont(p.hdc, 54);
    QueryPerformanceFrequency(&p.freq);
    QueryPerformanceCounter(&p.last);
    return true;
}

// Pump window messages, compute delta time. Returns false when the window closed.
inline bool pump(float& dt) {
    Platform& p = plat();
    memset(p.pressed, 0, sizeof(p.pressed));
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) p.running = false;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    dt = (float)(now.QuadPart - p.last.QuadPart) / (float)p.freq.QuadPart;
    p.last = now;
    if (dt > 0.05f) dt = 0.05f;
    return p.running;
}
inline void present() { SwapBuffers(plat().hdc); }
inline void quit() { plat().running = false; }

// -------------------------------------------------------------- renderer ---
inline void begin3D(float fov, float zn, float zf) {
    Platform& p = plat();
    glViewport(0, 0, p.w, p.h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(fov, (double)p.w / (double)(p.h ? p.h : 1), zn, zf);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}
inline void begin2D() {
    Platform& p = plat();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, p.w, p.h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
inline void end2D() {
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

inline void rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.f) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}
inline void text(float x, float y, const std::string& s, float r = 1, float g = 1, float b = 1) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    glListBase(plat().fontSmall - 32);
    glCallLists((GLsizei)s.size(), GL_UNSIGNED_BYTE, s.c_str());
}
inline void textBig(float x, float y, const std::string& s, float r = 1, float g = 1, float b = 1) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    glListBase(plat().fontBig - 32);
    glCallLists((GLsizei)s.size(), GL_UNSIGNED_BYTE, s.c_str());
}
inline float textWidthBig(const std::string& s) { return (float)s.size() * 29.f; }

// unit cube centred on the origin (-0.5..0.5), with normals
inline void cube() {
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);  glVertex3f(-.5f, -.5f, .5f);  glVertex3f(.5f, -.5f, .5f);   glVertex3f(.5f, .5f, .5f);   glVertex3f(-.5f, .5f, .5f);
    glNormal3f(0, 0, -1); glVertex3f(.5f, -.5f, -.5f);  glVertex3f(-.5f, -.5f, -.5f); glVertex3f(-.5f, .5f, -.5f); glVertex3f(.5f, .5f, -.5f);
    glNormal3f(1, 0, 0);  glVertex3f(.5f, -.5f, .5f);   glVertex3f(.5f, -.5f, -.5f);  glVertex3f(.5f, .5f, -.5f);  glVertex3f(.5f, .5f, .5f);
    glNormal3f(-1, 0, 0); glVertex3f(-.5f, -.5f, -.5f); glVertex3f(-.5f, -.5f, .5f);  glVertex3f(-.5f, .5f, .5f);  glVertex3f(-.5f, .5f, -.5f);
    glNormal3f(0, 1, 0);  glVertex3f(-.5f, .5f, .5f);   glVertex3f(.5f, .5f, .5f);    glVertex3f(.5f, .5f, -.5f);  glVertex3f(-.5f, .5f, -.5f);
    glNormal3f(0, -1, 0); glVertex3f(-.5f, -.5f, -.5f); glVertex3f(.5f, -.5f, -.5f);  glVertex3f(.5f, -.5f, .5f);   glVertex3f(-.5f, -.5f, .5f);
    glEnd();
}
// axis-aligned box: centre + full size
inline void box(float cx, float cy, float cz, float sx, float sy, float sz) {
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glScalef(sx, sy, sz);
    cube();
    glPopMatrix();
}

// Procedural window texture for buildings (8x8 windows, ~30% lit)
inline GLuint makeWindowTexture() {
    const int S = 64;
    static unsigned char px[S * S * 4];
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    for (int cy = 0; cy < 8; cy++)
        for (int cx = 0; cx < 8; cx++) {
            bool lit = frand() < 0.30f;
            unsigned char wr = lit ? 255 : 45, wg = lit ? 215 : 60, wb = lit ? 120 : 88;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    unsigned char* o = &px[((cy * 8 + y) * S + (cx * 8 + x)) * 4];
                    bool win = x >= 2 && x <= 5 && y >= 2 && y <= 5;
                    o[0] = win ? wr : 225; o[1] = win ? wg : 225; o[2] = win ? wb : 232; o[3] = 255;
                }
        }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gluBuild2DMipmaps(GL_TEXTURE_2D, 4, S, S, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return tex;
}

// world position -> window pixel (call while the 3D matrices are still active)
inline bool project(float x, float y, float z, float& sx, float& sy) {
    GLdouble mv[16], pr[16], wx, wy, wz;
    GLint vp[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, mv);
    glGetDoublev(GL_PROJECTION_MATRIX, pr);
    glGetIntegerv(GL_VIEWPORT, vp);
    if (gluProject(x, y, z, mv, pr, vp, &wx, &wy, &wz) == GL_FALSE) return false;
    if (wz >= 1.0) return false;
    sx = (float)wx;
    sy = (float)(plat().h - wy);
    return true;
}

}  // namespace lite
