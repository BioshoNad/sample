// ============================================================================
//  LITE CITY  (beta)  -  sample open-world game running on LiteEngine
//  Drive, shoot, evade police, shop, work jobs and pay your bills.
// ============================================================================
#include "engine.h"
#include <algorithm>
#include <string>
#include <vector>
using namespace lite;

// ------------------------------------------------------------- constants ---
static const int   N = 8;                      // N x N city blocks
static const float PITCH = 62.f;               // block + road
static const float HALF = N * PITCH * 0.5f;    // 248
static const float DAY_LEN = 240.f;            // seconds per full day
static const int   MAXCARS = 64, MAXPEDS = 40;
enum { S_SUPER, S_AMMO, S_JOB, S_BILLS, S_HOSP };

struct Building { float x, z, hx, hz, h, r, g, b; };
struct Spot { int type; float x, z, r, g, b; const char* name; };
struct Car {
    bool used = false, dead = false;
    int kind = 0;  // 0 traffic, 1 police, 2 owned/player, 3 stalled
    float x = 0, z = 0, heading = 0, speed = 0, hp = 100, r = 1, g = 1, b = 1;
    int axis = 0, dir = 1;
    float lineC = 0, maxSpd = 12, stuck = 0, rev = 0, revSteer = 1, cool = 0, wreck = 0, idle = 0, despawn = 0;
};
struct Ped { bool alive = false; float x = 0, z = 0, heading = 0, t = 0, fear = 0, timer = 0, r = 1, g = 1, b = 1, phase = 0; };
struct Particle { float x, y, z, vx, vy, vz, life, maxLife, r, g, b; };
struct Job { bool active = false; float x = 0, z = 0, time = 0; int reward = 0; };
struct Toast { std::string s; float t; };
struct Game {
    float x = 0, z = 0, heading = 0, health = 100, heat = 0, billsDue = 0, billTimer = 0, clock = 0;
    float deadTimer = 0, shotCool = 0, tracerT = 0, tx0 = 0, tz0 = 0, tx1 = 0, tz1 = 0;
    float camH = 0, walk = 0, copCool = 0, trafCool = 0, hurt = 0, lastHealth = 100, uptime = 0;
    int money = 500, ammo = 24, driving = -1, state = 0, prevStars = 0;  // state: 0 play, 1 dead, 2 evicted
    bool paused = false;
    Job job;
};

static Game g;
static std::vector<Building> blds;
static std::vector<Spot> spots;
static Car cars[MAXCARS];
static Ped peds[MAXPEDS];
static std::vector<Particle> parts;
static std::vector<Toast> toasts;
static GLuint cityList = 0, winTex = 0;

static void toast(const std::string& s) {
    toasts.push_back({s, 4.f});
    if (toasts.size() > 5) toasts.erase(toasts.begin());
}
static void addHeat(float v) { g.heat = std::min(100.f, g.heat + v); }
static int stars() { return g.heat <= 0.01f ? 0 : std::min(5, 1 + (int)(g.heat / 20.f)); }
static float blockC(int i) { return (i - N / 2 + 0.5f) * PITCH; }
static float dist2(float ax, float az, float bx, float bz) { return sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz)); }

// ---------------------------------------------------------------- world ----
static void addSpecial(int bi, int bj, int type, float r, float gg, float b, const char* name) {
    float cx = blockC(bi), cz = blockC(bj);
    blds.push_back({cx, cz, 19.f, 19.f, 9.f, r, gg, b});
    spots.push_back({type, cx, cz - 22.f, r, gg, b, name});
}

static void drawBuilding(const Building& b) {
    float x0 = b.x - b.hx, x1 = b.x + b.hx, z0 = b.z - b.hz, z1 = b.z + b.hz, h = b.h;
    float u = (b.hx * 2) / 16.f, w = (b.hz * 2) / 16.f, v = h / 20.f;
    glColor3f(b.r, b.g, b.b);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, winTex);
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);  glTexCoord2f(0, 0); glVertex3f(x0, 0, z1); glTexCoord2f(u, 0); glVertex3f(x1, 0, z1); glTexCoord2f(u, v); glVertex3f(x1, h, z1); glTexCoord2f(0, v); glVertex3f(x0, h, z1);
    glNormal3f(0, 0, -1); glTexCoord2f(0, 0); glVertex3f(x1, 0, z0); glTexCoord2f(u, 0); glVertex3f(x0, 0, z0); glTexCoord2f(u, v); glVertex3f(x0, h, z0); glTexCoord2f(0, v); glVertex3f(x1, h, z0);
    glNormal3f(1, 0, 0);  glTexCoord2f(0, 0); glVertex3f(x1, 0, z1); glTexCoord2f(w, 0); glVertex3f(x1, 0, z0); glTexCoord2f(w, v); glVertex3f(x1, h, z0); glTexCoord2f(0, v); glVertex3f(x1, h, z1);
    glNormal3f(-1, 0, 0); glTexCoord2f(0, 0); glVertex3f(x0, 0, z0); glTexCoord2f(w, 0); glVertex3f(x0, 0, z1); glTexCoord2f(w, v); glVertex3f(x0, h, z1); glTexCoord2f(0, v); glVertex3f(x0, h, z0);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glColor3f(b.r * 0.55f, b.g * 0.55f, b.b * 0.55f);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0); glVertex3f(x0, h, z1); glVertex3f(x1, h, z1); glVertex3f(x1, h, z0); glVertex3f(x0, h, z0);
    glEnd();
}

static void buildWorld() {
    srand(1234);
    winTex = makeWindowTexture();
    // special blocks (shops, job office, bills office, hospital)
    addSpecial(1, 1, S_SUPER, 0.20f, 0.80f, 0.30f, "SUPERMARKET");
    addSpecial(6, 1, S_AMMO, 0.95f, 0.50f, 0.10f, "AMMO STORE");
    addSpecial(4, 3, S_JOB, 0.20f, 0.50f, 0.95f, "JOB CENTER");
    addSpecial(1, 6, S_BILLS, 0.95f, 0.85f, 0.20f, "BILLS OFFICE");
    addSpecial(6, 6, S_HOSP, 0.95f, 0.95f, 0.95f, "HOSPITAL");
    // regular blocks: 2x2 lots
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            bool special = false;
            for (const Spot& s : spots)
                if (fabsf(s.x - blockC(i)) < 1 && fabsf(s.z + 22.f - blockC(j)) < 1) special = true;
            if (special) continue;
            for (int a = -1; a <= 1; a += 2)
                for (int c = -1; c <= 1; c += 2) {
                    float h = (frand() < 0.2f) ? frange(8, 16) : frange(14, 58);
                    float tone = frange(0.55f, 0.9f);
                    blds.push_back({blockC(i) + a * 11.f, blockC(j) + c * 11.f, 9.5f, 9.5f, h,
                                    tone * frange(0.9f, 1.05f), tone * frange(0.92f, 1.0f), tone * frange(0.95f, 1.1f)});
                }
        }
    // compile the static city into a display list
    cityList = glGenLists(1);
    glNewList(cityList, GL_COMPILE);
    glBegin(GL_QUADS);  // ground
    glColor3f(0.22f, 0.34f, 0.20f); glNormal3f(0, 1, 0);
    glVertex3f(-700, -0.05f, -700); glVertex3f(700, -0.05f, -700); glVertex3f(700, -0.05f, 700); glVertex3f(-700, -0.05f, 700);
    // roads
    glColor3f(0.13f, 0.13f, 0.14f);
    for (int k = 0; k <= N; k++) {
        float L = k * PITCH - HALF, e = HALF + 7.f;
        glVertex3f(-e, 0.02f, L - 7); glVertex3f(e, 0.02f, L - 7); glVertex3f(e, 0.02f, L + 7); glVertex3f(-e, 0.02f, L + 7);
        glVertex3f(L - 7, 0.02f, -e); glVertex3f(L + 7, 0.02f, -e); glVertex3f(L + 7, 0.02f, e); glVertex3f(L - 7, 0.02f, e);
    }
    // lane dashes
    glColor3f(0.9f, 0.85f, 0.4f);
    for (int k = 0; k <= N; k++) {
        float L = k * PITCH - HALF;
        for (float t = -HALF; t < HALF; t += 10.f) {
            float d = fmodf(t + HALF, PITCH);
            if (std::min(d, PITCH - d) < 9.f) continue;
            glVertex3f(t, 0.04f, L - 0.18f); glVertex3f(t + 4, 0.04f, L - 0.18f); glVertex3f(t + 4, 0.04f, L + 0.18f); glVertex3f(t, 0.04f, L + 0.18f);
            glVertex3f(L - 0.18f, 0.04f, t); glVertex3f(L + 0.18f, 0.04f, t); glVertex3f(L + 0.18f, 0.04f, t + 4); glVertex3f(L - 0.18f, 0.04f, t + 4);
        }
    }
    glEnd();
    // sidewalk slabs
    glColor3f(0.55f, 0.55f, 0.55f);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) box(blockC(i), 0.075f, blockC(j), 48.f, 0.3f, 48.f);
    for (const Building& b : blds) drawBuilding(b);
    glEndList();
}

// collision: push a circle out of all buildings. returns true if pushed.
static bool pushOut(float& x, float& z, float r, float* nx = nullptr, float* nz = nullptr) {
    bool hit = false;
    float ax = 0, az = 0;
    for (const Building& b : blds) {
        if (fabsf(x - b.x) > b.hx + r || fabsf(z - b.z) > b.hz + r) continue;
        float cx = clampf(x, b.x - b.hx, b.x + b.hx), cz = clampf(z, b.z - b.hz, b.z + b.hz);
        float dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
        if (d2 >= r * r) continue;
        float px, pz;
        if (d2 > 1e-6f) {
            float d = sqrtf(d2);
            px = dx / d * (r - d); pz = dz / d * (r - d);
        } else {
            float ex = b.hx - fabsf(x - b.x), ez = b.hz - fabsf(z - b.z);
            if (ex < ez) { px = (x < b.x ? -1.f : 1.f) * (ex + r); pz = 0; }
            else { pz = (z < b.z ? -1.f : 1.f) * (ez + r); px = 0; }
        }
        x += px; z += pz; ax += px; az += pz; hit = true;
    }
    if (hit && nx) {
        float l = sqrtf(ax * ax + az * az);
        *nx = l > 1e-6f ? ax / l : 0; *nz = l > 1e-6f ? az / l : 0;
    }
    return hit;
}

static float rayBox(float ox, float oz, float dx, float dz, const Building& b) {
    float tmin = 0, tmax = 1e9f;
    float o[2] = {ox, oz}, d[2] = {dx, dz}, lo[2] = {b.x - b.hx, b.z - b.hz}, hi[2] = {b.x + b.hx, b.z + b.hz};
    for (int i = 0; i < 2; i++) {
        if (fabsf(d[i]) < 1e-6f) { if (o[i] < lo[i] || o[i] > hi[i]) return 1e9f; }
        else {
            float t1 = (lo[i] - o[i]) / d[i], t2 = (hi[i] - o[i]) / d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1); tmax = std::min(tmax, t2);
            if (tmin > tmax) return 1e9f;
        }
    }
    return tmin;
}
static bool losClear(float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return true;
    dx /= len; dz /= len;
    for (const Building& b : blds) if (rayBox(ax, az, dx, dz, b) < len) return false;
    return true;
}

// -------------------------------------------------------------- effects ----
static void burst(float x, float y, float z, int n, float r, float gg, float b, float spd) {
    for (int i = 0; i < n; i++) {
        Particle p;
        float a = frand() * 2 * PI, s = frand() * spd;
        p.x = x; p.y = y; p.z = z; p.vx = cosf(a) * s; p.vz = sinf(a) * s; p.vy = frange(2.f, spd);
        p.life = p.maxLife = frange(0.5f, 1.4f); p.r = r; p.g = gg; p.b = b;
        parts.push_back(p);
    }
}

// ----------------------------------------------------------------- cars ----
static int freeCar() { for (int i = 0; i < MAXCARS; i++) if (!cars[i].used) return i; return -1; }

static void setLanePos(Car& c, float a) {
    c.heading = (c.axis == 0) ? (c.dir > 0 ? PI * 0.5f : -PI * 0.5f) : (c.dir > 0 ? 0.f : PI);
    float rcx = -cosf(c.heading), rcz = sinf(c.heading);
    if (c.axis == 0) { c.x = a; c.z = c.lineC + 3.6f * rcz; }
    else { c.z = a; c.x = c.lineC + 3.6f * rcx; }
}

static void spawnTraffic(bool farFromPlayer) {
    int i = freeCar();
    if (i < 0) return;
    for (int tries = 0; tries < 15; tries++) {
        Car c;
        c.used = true; c.kind = 0; c.hp = 100;
        c.axis = rand() % 2; c.dir = (rand() % 2) ? 1 : -1;
        c.lineC = (rand() % (N + 1)) * PITCH - HALF;
        c.r = frange(0.2f, 1.f); c.g = frange(0.2f, 1.f); c.b = frange(0.2f, 1.f);
        c.maxSpd = frange(9.f, 15.f); c.speed = c.maxSpd;
        setLanePos(c, frange(-HALF, HALF));
        if (dist2(c.x, c.z, g.x, g.z) < (farFromPlayer ? 90.f : 35.f)) continue;
        cars[i] = c;
        return;
    }
}

static void spawnPolice() {
    int i = freeCar();
    if (i < 0) return;
    for (int tries = 0; tries < 25; tries++) {
        Car c;
        c.used = true; c.kind = 1; c.hp = 150;
        c.axis = rand() % 2; c.dir = (rand() % 2) ? 1 : -1;
        c.lineC = (rand() % (N + 1)) * PITCH - HALF;
        setLanePos(c, frange(-HALF, HALF));
        float d = dist2(c.x, c.z, g.x, g.z);
        if (d < 90.f || d > 170.f) continue;
        c.heading = atan2f(g.x - c.x, g.z - c.z);
        c.speed = 8.f; c.r = 0.08f; c.g = 0.08f; c.b = 0.1f;
        cars[i] = c;
        return;
    }
}

static void explode(int i, bool byPlayer) {
    Car& c = cars[i];
    c.dead = true; c.wreck = 14.f; c.speed = 0;
    burst(c.x, 1.2f, c.z, 45, 1.f, 0.55f, 0.1f, 9.f);
    burst(c.x, 1.0f, c.z, 25, 0.2f, 0.2f, 0.2f, 6.f);
    float d = dist2(c.x, c.z, g.x, g.z);
    if (g.driving == i) {
        g.health -= 55.f; g.driving = -1; g.x = c.x + 3.f; g.z = c.z;
        toast("Your car exploded!");
    } else if (d < 7.f && g.state == 0) {
        g.health -= 40.f * (1.f - d / 7.f);
    }
    if (byPlayer) {
        if (c.kind == 1) { addHeat(30); toast("Officer down!"); }
        else addHeat(15);
    }
}
static void damageCar(int i, float d, bool byPlayer) {
    Car& c = cars[i];
    if (c.dead) return;
    c.hp -= d;
    if (c.hp <= 0) explode(i, byPlayer);
}

static void moveCar(int idx, float dt) {
    Car& c = cars[idx];
    c.x += sinf(c.heading) * c.speed * dt;
    c.z += cosf(c.heading) * c.speed * dt;
    float nx = 0, nz = 0;
    if (pushOut(c.x, c.z, 2.0f, &nx, &nz)) {
        float fx = sinf(c.heading), fz = cosf(c.heading);
        float into = fabsf(fx * nx + fz * nz);
        float impact = fabsf(c.speed) * into;
        if (impact > 7.f) {
            damageCar(idx, (impact - 7.f) * 3.f, idx == g.driving);
            if (idx == g.driving) g.health -= (impact - 7.f) * 0.5f;
            burst(c.x, 1.f, c.z, 6, 1.f, 0.8f, 0.3f, 4.f);
        }
        c.speed *= (1.f - 0.8f * into);
    }
    c.x = clampf(c.x, -HALF - 14, HALF + 14);
    c.z = clampf(c.z, -HALF - 14, HALF + 14);
}

static void driveInput(Car& c, float dt) {
    bool fw = key('W') || key(VK_UP), bk = key('S') || key(VK_DOWN);
    bool lf = key('A') || key(VK_LEFT), rt = key('D') || key(VK_RIGHT), hb = key(VK_SPACE);
    const float MAXF = 36.f, MAXR = 10.f;
    if (fw) { if (c.speed < 0) c.speed += 32 * dt; else c.speed += (16.f * (1.f - c.speed / MAXF) + 3.f) * dt; }
    else if (bk) { if (c.speed > 0.5f) c.speed -= 30 * dt; else c.speed -= 9 * dt; }
    else { float d = 5.f * dt; if (fabsf(c.speed) < d) c.speed = 0; else c.speed -= (c.speed > 0 ? d : -d); }
    if (hb) { float d = 28 * dt; if (fabsf(c.speed) < d) c.speed = 0; else c.speed -= (c.speed > 0 ? d : -d); }
    c.speed = clampf(c.speed, -MAXR, MAXF);
    float s = (lf ? 1.f : 0.f) - (rt ? 1.f : 0.f);
    c.heading += s * (hb ? 1.6f : 1.f) * 2.1f / (1.f + fabsf(c.speed) / 16.f) * clampf(c.speed / 4.f, -1.f, 1.f) * dt;
}

static void updateTraffic(Car& c, int idx, float dt) {
    float fx = sinf(c.heading), fz = cosf(c.heading), rx = -cosf(c.heading), rz = sinf(c.heading);
    bool block = false;
    for (int j = 0; j < MAXCARS && !block; j++) {
        if (j == idx || !cars[j].used) continue;
        float dx = cars[j].x - c.x, dz = cars[j].z - c.z, al = dx * fx + dz * fz;
        if (al < 0.5f || al > 14.f) continue;
        if (fabsf(dx * rx + dz * rz) < 2.6f) block = true;
    }
    for (int j = 0; j < MAXPEDS && !block; j++) {
        if (!peds[j].alive) continue;
        float dx = peds[j].x - c.x, dz = peds[j].z - c.z, al = dx * fx + dz * fz;
        if (al < 0.5f || al > 10.f) continue;
        if (fabsf(dx * rx + dz * rz) < 1.8f) block = true;
    }
    if (g.state == 0 && g.driving < 0) {  // pedestrian player
        float dx = g.x - c.x, dz = g.z - c.z, al = dx * fx + dz * fz;
        if (al > 0.5f && al < 10.f && fabsf(dx * rx + dz * rz) < 1.8f) block = true;
    }
    float target = block ? 0.f : c.maxSpd;
    c.speed += clampf(target - c.speed, -25.f * dt, 8.f * dt);
    float a = ((c.axis == 0) ? c.x : c.z) + c.speed * dt * c.dir;
    if (a > HALF + 12) a = -HALF - 12;
    if (a < -HALF - 12) a = HALF + 12;
    setLanePos(c, a);
}

static void updatePolice(Car& c, int idx, float dt) {
    int st = stars();
    if (st == 0) {
        c.speed += clampf(0.f - c.speed, -10.f * dt, 0.f);
        c.despawn += dt;
        if (c.despawn > 6.f) c.used = false;
        moveCar(idx, dt);
        return;
    }
    c.despawn = 0;
    float dx = g.x - c.x, dz = g.z - c.z, d = sqrtf(dx * dx + dz * dz);
    float diff = wrapAngle(atan2f(dx, dz) - c.heading);
    float steer = clampf(diff * 2.f, -1.f, 1.f);
    float target = (d < 7.f) ? 0.f : 22.f + st * 2.5f;
    if (c.rev > 0) { c.rev -= dt; target = -7.f; steer = c.revSteer; }
    c.speed += clampf(target - c.speed, -26.f * dt, 15.f * dt);
    c.heading += steer * 2.4f * clampf(fabsf(c.speed) / 5.f, 0.f, 1.f) * (c.speed >= 0 ? 1.f : -1.f) * dt;
    if (fabsf(c.speed) < 2.f && d > 10.f && c.rev <= 0) {
        c.stuck += dt;
        if (c.stuck > 0.7f) { c.rev = 1.1f; c.stuck = 0; c.revSteer = (rand() % 2) ? 1.f : -1.f; }
    } else if (c.rev <= 0) c.stuck = 0;
    moveCar(idx, dt);
    // gunfire at the player
    if (g.state == 0 && d < 34.f && losClear(c.x, c.z, g.x, g.z)) {
        float dps = (1.2f + st * 0.25f) * (1.f - d / 40.f) * (g.driving >= 0 ? 0.5f : 1.f);
        g.health -= dps * dt;
    }
    // melee bump on foot
    if (g.state == 0 && g.driving < 0 && d < 2.8f && fabsf(c.speed) > 3.f) {
        g.health -= 22.f * dt * 4.f;
    }
}

// ----------------------------------------------------------------- peds ----
static void spawnPed(Ped& p, bool anywhere) {
    for (int tries = 0; tries < 20; tries++) {
        int i = rand() % N, j = rand() % N, side = rand() % 4;
        float cx = blockC(i), cz = blockC(j), e = 22.4f, t = frange(-22.f, 22.f);
        p.x = (side == 0) ? cx + t : (side == 1) ? cx + t : (side == 2) ? cx - e : cx + e;
        p.z = (side == 0) ? cz - e : (side == 1) ? cz + e : cz + t;
        if (side == 3) p.z = cz + t;
        if (anywhere || dist2(p.x, p.z, g.x, g.z) > 25.f) break;
    }
    p.alive = true; p.heading = frange(-PI, PI); p.t = frange(1, 5); p.fear = 0;
    p.r = frange(0.2f, 1.f); p.g = frange(0.2f, 1.f); p.b = frange(0.2f, 1.f);
}

static void killPed(Ped& p, bool byPlayer) {
    p.alive = false; p.timer = 15.f;
    burst(p.x, 1.f, p.z, 10, 0.7f, 0.05f, 0.05f, 3.f);
    if (byPlayer) {
        int cash = 10 + rand() % 51;
        g.money += cash;
        addHeat(12);
        toast("+$" + std::to_string(cash));
    }
}

// ----------------------------------------------------------------- game ----
static void shoot() {
    g.ammo--; g.shotCool = 0.18f; addHeat(3.f);
    float fx = sinf(g.heading), fz = cosf(g.heading), ox = g.x + fx * 0.6f, oz = g.z + fz * 0.6f;
    float bt = 60.f;
    for (const Building& b : blds) { float t = rayBox(ox, oz, fx, fz, b); if (t < bt) bt = t; }
    int hitCar = -1, hitPed = -1;
    for (int i = 0; i < MAXCARS; i++) {
        const Car& c = cars[i];
        if (!c.used || c.dead || i == g.driving) continue;
        float rx = c.x - ox, rz = c.z - oz, al = rx * fx + rz * fz, lat = fabsf(rx * fz - rz * fx);
        if (al > 0 && al < bt && lat < 1.6f + al * 0.05f) { bt = al; hitCar = i; hitPed = -1; }
    }
    for (int i = 0; i < MAXPEDS; i++) {
        const Ped& p = peds[i];
        if (!p.alive) continue;
        float rx = p.x - ox, rz = p.z - oz, al = rx * fx + rz * fz, lat = fabsf(rx * fz - rz * fx);
        if (al > 0 && al < bt && lat < 0.7f + al * 0.05f) { bt = al; hitPed = i; hitCar = -1; }
    }
    g.tracerT = 0.06f; g.tx0 = ox; g.tz0 = oz; g.tx1 = ox + fx * bt; g.tz1 = oz + fz * bt;
    for (int i = 0; i < MAXPEDS; i++)  // gunfire scares pedestrians
        if (peds[i].alive && dist2(peds[i].x, peds[i].z, g.x, g.z) < 35.f) peds[i].fear = 5.f;
    if (hitPed >= 0) {
        burst(peds[hitPed].x, 1.2f, peds[hitPed].z, 4, 0.7f, 0.05f, 0.05f, 2.f);
        killPed(peds[hitPed], true);
    } else if (hitCar >= 0) {
        Car& c = cars[hitCar];
        burst(c.x, 1.2f, c.z, 5, 1.f, 0.9f, 0.4f, 3.f);
        if (c.kind == 0) { c.kind = 3; addHeat(5.f); }
        if (c.kind == 1) addHeat(8.f);
        damageCar(hitCar, 30.f, true);
    }
}

static void startJob() {
    for (int tries = 0; tries < 30; tries++) {
        int i = rand() % N, j = rand() % N;
        float x = blockC(i), z = blockC(j) - 22.f;
        bool special = false;
        for (const Spot& s : spots) if (fabsf(s.x - x) < 1 && fabsf(s.z - z) < 1) special = true;
        float d = dist2(x, z, g.x, g.z);
        if (special || d < 140.f) continue;
        g.job.active = true; g.job.x = x; g.job.z = z;
        g.job.time = d / 9.f + 30.f; g.job.reward = 80 + (int)(d * 0.6f);
        toast("Job accepted: deliver the cargo by car!");
        return;
    }
}

static int nearestSpot(float maxD) {
    int best = -1; float bd = maxD;
    for (size_t i = 0; i < spots.size(); i++) {
        float d = dist2(spots[i].x, spots[i].z, g.x, g.z);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

static void doInteract(int si) {
    switch (spots[si].type) {
    case S_SUPER:
        if (g.money < 25) toast("Not enough cash ($25)");
        else if (g.health >= 100) toast("You are not hungry");
        else { g.money -= 25; g.health = std::min(100.f, g.health + 50.f); toast("Ate a burger: +50 health, -$25"); }
        break;
    case S_AMMO:
        if (g.money < 60) toast("Not enough cash ($60)");
        else { g.money -= 60; g.ammo += 30; toast("Bought 30 rounds, -$60"); }
        break;
    case S_JOB:
        if (g.job.active) toast("Finish your current job first");
        else startJob();
        break;
    case S_BILLS:
        if (g.billsDue <= 0) toast("No bills due. Nice.");
        else {
            int pay = std::min(g.money, (int)g.billsDue);
            if (pay <= 0) toast("You have no cash!");
            else { g.money -= pay; g.billsDue -= pay; toast("Paid $" + std::to_string(pay) + " in bills"); }
        }
        break;
    case S_HOSP:
        if (g.money < 50) toast("Treatment costs $50");
        else if (g.health >= 100) toast("You are healthy");
        else { g.money -= 50; g.health = 100; toast("Treated: full health, -$50"); }
        break;
    }
}

static void resetGame() {
    for (int i = 0; i < MAXCARS; i++) cars[i] = Car();
    for (int i = 0; i < MAXPEDS; i++) peds[i] = Ped();
    parts.clear(); toasts.clear();
    float clock = DAY_LEN * 0.08f;
    g = Game();
    g.clock = clock;
    g.x = -9.f; g.z = 0.f; g.heading = 0.f; g.camH = 0.f;
    // player's own car
    Car pc; pc.used = true; pc.kind = 2; pc.x = -5.5f; pc.z = 2.f; pc.heading = 0.f; pc.hp = 120;
    pc.r = 0.85f; pc.g = 0.1f; pc.b = 0.1f;
    cars[0] = pc;
    for (int i = 0; i < 22; i++) spawnTraffic(false);
    for (int i = 0; i < MAXPEDS; i++) spawnPed(peds[i], false);
    toast("Welcome to LITE CITY. Steal a car, earn cash, pay your bills.");
}

static void respawnPlayer() {
    float hx = 0, hz = 0;
    for (const Spot& s : spots) if (s.type == S_HOSP) { hx = s.x; hz = s.z - 5.f; }
    g.x = hx; g.z = hz; g.health = 60; g.heat = 0; g.driving = -1; g.state = 0;
    int fee = std::min(g.money, 150);
    g.money -= fee; g.lastHealth = g.health;
    for (int i = 0; i < MAXCARS; i++) if (cars[i].kind == 1) cars[i].used = false;
    toast("Treated at the hospital. Fee: $" + std::to_string(fee));
}

static void updateWorld(float dt) {
    // --- cars
    for (int i = 0; i < MAXCARS; i++) {
        Car& c = cars[i];
        if (!c.used) continue;
        if (c.cool > 0) c.cool -= dt;
        if (c.dead) {
            c.wreck -= dt;
            if (frand() < 0.5f) burst(c.x, 1.f, c.z, 1, 0.9f, 0.4f, 0.1f, 2.f);
            if (c.wreck <= 0) c.used = false;
            continue;
        }
        if (i == g.driving && g.state == 0) { driveInput(c, dt); moveCar(i, dt); }
        else if (c.kind == 0) updateTraffic(c, i, dt);
        else if (c.kind == 1) updatePolice(c, i, dt);
        else {  // owned-but-parked or stalled
            float d = 8.f * dt;
            if (fabsf(c.speed) < d) c.speed = 0; else c.speed -= (c.speed > 0 ? d : -d);
            moveCar(i, dt);
            if (c.kind == 3) {
                c.idle += dt;
                if (c.idle > 25.f && dist2(c.x, c.z, g.x, g.z) > 70.f) c.used = false;
            }
        }
    }
    // --- car vs car
    for (int i = 0; i < MAXCARS; i++) {
        if (!cars[i].used || cars[i].dead) continue;
        for (int j = i + 1; j < MAXCARS; j++) {
            if (!cars[j].used || cars[j].dead) continue;
            Car &a = cars[i], &b = cars[j];
            if (a.kind == 0 && b.kind == 0) continue;
            float dx = a.x - b.x, dz = a.z - b.z, d = sqrtf(dx * dx + dz * dz);
            if (d >= 4.2f || d < 0.001f) continue;
            float nx = dx / d, nz = dz / d, push = (4.2f - d) * 0.5f;
            a.x += nx * push; a.z += nz * push; b.x -= nx * push; b.z -= nz * push;
            float vax = sinf(a.heading) * a.speed, vaz = cosf(a.heading) * a.speed;
            float vbx = sinf(b.heading) * b.speed, vbz = cosf(b.heading) * b.speed;
            float closing = (vbx - vax) * nx + (vbz - vaz) * nz;
            if (closing > 2.f && a.cool <= 0 && b.cool <= 0) {
                a.cool = b.cool = 0.3f;
                bool pa = (i == g.driving), pb = (j == g.driving);
                damageCar(i, closing * 1.6f, pb);
                damageCar(j, closing * 1.6f, pa);
                a.speed *= 0.65f; b.speed *= 0.65f;
                burst((a.x + b.x) * 0.5f, 1.f, (a.z + b.z) * 0.5f, 6, 1.f, 0.8f, 0.3f, 4.f);
                if (pa || pb) {
                    Car& o = pa ? b : a;
                    g.health -= closing * 0.25f;
                    if (o.kind == 0) { o.kind = 3; if (closing > 4.f) addHeat(8.f); }
                    if (o.kind == 1) addHeat(5.f);
                }
                if ((a.kind == 1 && pb) || (b.kind == 1 && pa)) g.health -= closing * 0.6f;
            }
        }
    }
    // --- peds
    for (int i = 0; i < MAXPEDS; i++) {
        Ped& p = peds[i];
        if (!p.alive) { p.timer -= dt; if (p.timer <= 0) spawnPed(p, false); continue; }
        p.t -= dt;
        float speed = 1.3f;
        if (p.fear > 0) { p.fear -= dt; speed = 4.8f; p.heading = atan2f(p.x - g.x, p.z - g.z); }
        else if (p.t <= 0) { p.heading = frange(-PI, PI); p.t = frange(2.f, 6.f); }
        p.x += sinf(p.heading) * speed * dt; p.z += cosf(p.heading) * speed * dt;
        p.phase += speed * 2.2f * dt;
        pushOut(p.x, p.z, 0.5f);
        p.x = clampf(p.x, -HALF - 10, HALF + 10); p.z = clampf(p.z, -HALF - 10, HALF + 10);
        for (int j = 0; j < MAXCARS; j++) {  // run over
            const Car& c = cars[j];
            if (!c.used || c.dead || fabsf(c.speed) < 4.f) continue;
            float d = dist2(c.x, c.z, p.x, p.z);
            if (d < 10.f && fabsf(c.speed) > 8.f) p.fear = 3.f;
            if (d < 2.3f) { killPed(p, j == g.driving); break; }
        }
    }
    // --- particles
    for (size_t i = 0; i < parts.size();) {
        Particle& p = parts[i];
        p.life -= dt; p.vy -= 9.f * dt;
        p.x += p.vx * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        if (p.y < 0.05f) { p.y = 0.05f; p.vy = 0; p.vx *= 0.8f; p.vz *= 0.8f; }
        if (p.life <= 0) { parts[i] = parts.back(); parts.pop_back(); } else i++;
    }
    // --- police + traffic population
    int st = stars();
    if (st > 0 && st > g.prevStars) toast(std::string("WANTED LEVEL ") + std::string(st, '*'));
    if (st == 0 && g.prevStars > 0) toast("You lost the cops.");
    g.prevStars = st;
    int cops = 0, traf = 0;
    float nearCop = 1e9f;
    for (int i = 0; i < MAXCARS; i++)
        if (cars[i].used && !cars[i].dead) {
            if (cars[i].kind == 1) { cops++; nearCop = std::min(nearCop, dist2(cars[i].x, cars[i].z, g.x, g.z)); }
            if (cars[i].kind == 0) traf++;
        }
    g.copCool -= dt; g.trafCool -= dt;
    if (st > 0 && cops < std::min(10, st * 2) && g.copCool <= 0) { spawnPolice(); g.copCool = 1.5f; }
    if (traf < 22 && g.trafCool <= 0) { spawnTraffic(true); g.trafCool = 0.7f; }
    if (g.heat > 0) g.heat = std::max(0.f, g.heat - ((nearCop < 45.f) ? 0.4f : 2.5f) * dt);
}

static void updatePlayer(float dt) {
    bool fw = key('W') || key(VK_UP), bk = key('S') || key(VK_DOWN), lf = key('A') || key(VK_LEFT), rt = key('D') || key(VK_RIGHT);
    if (g.shotCool > 0) g.shotCool -= dt;
    if (g.tracerT > 0) g.tracerT -= dt;
    if (g.driving < 0) {
        g.heading += ((lf ? 1.f : 0.f) - (rt ? 1.f : 0.f)) * 2.6f * dt;
        float sp = key(VK_SHIFT) ? 9.5f : 5.5f, mv = ((fw ? 1.f : 0.f) - (bk ? 0.6f : 0.f)) * sp;
        g.x += sinf(g.heading) * mv * dt; g.z += cosf(g.heading) * mv * dt;
        g.walk += fabsf(mv) * 1.3f * dt;
        pushOut(g.x, g.z, 0.6f);
        g.x = clampf(g.x, -HALF - 14, HALF + 14); g.z = clampf(g.z, -HALF - 14, HALF + 14);
        if (key(VK_SPACE) && g.ammo > 0 && g.shotCool <= 0) shoot();
        if (tapped('F')) {
            int best = -1; float bd = 5.5f;
            for (int i = 0; i < MAXCARS; i++) {
                if (!cars[i].used || cars[i].dead || cars[i].kind == 1) continue;
                float d = dist2(cars[i].x, cars[i].z, g.x, g.z);
                if (d < bd) { bd = d; best = i; }
            }
            if (best >= 0) {
                if (cars[best].kind != 2) { addHeat(10.f); toast("Carjacked!"); }
                cars[best].kind = 2; cars[best].speed *= 0.5f; g.driving = best;
            }
        }
    } else {
        Car& c = cars[g.driving];
        g.x = c.x; g.z = c.z; g.heading = c.heading;
        if (tapped('F') && fabsf(c.speed) < 9.f) {
            g.x = c.x + cosf(c.heading) * 2.6f; g.z = c.z - sinf(c.heading) * 2.6f;
            g.driving = -1;
        }
    }
    int si = nearestSpot(5.5f);
    if (si >= 0 && tapped('E')) doInteract(si);
    // job delivery
    if (g.job.active) {
        g.job.time -= dt;
        if (g.driving >= 0 && dist2(g.job.x, g.job.z, g.x, g.z) < 6.f) {
            g.money += g.job.reward; toast("Delivered! +$" + std::to_string(g.job.reward)); g.job.active = false;
        } else if (g.job.time <= 0) { g.job.active = false; toast("Job failed: out of time."); }
    }
    // bills
    g.billTimer += dt;
    if (g.billTimer >= 75.f) {
        g.billTimer = 0; g.billsDue += 100.f;
        toast("New bill: $100. Pay it at the BILLS OFFICE.");
    }
    if (g.billsDue >= 600.f) { g.state = 2; }
    // damage flash / death
    if (g.health < g.lastHealth) g.hurt = 0.3f;
    g.lastHealth = g.health;
    if (g.health <= 0 && g.state == 0) {
        g.state = 1; g.deadTimer = 3.5f; g.health = 0; g.driving = -1;
    }
}

static void update(float dt) {
    if (tapped(VK_ESCAPE)) quit();
    if (tapped('P') && g.state == 0) g.paused = !g.paused;
    if (g.state == 2 && tapped('R')) { resetGame(); return; }
    if (g.paused) return;
    g.clock += dt; g.uptime += dt;
    if (g.hurt > 0) g.hurt -= dt;
    for (size_t i = 0; i < toasts.size();) { toasts[i].t -= dt; if (toasts[i].t <= 0) toasts.erase(toasts.begin() + i); else i++; }
    if (g.state == 0) updatePlayer(dt);
    else if (g.state == 1) { g.deadTimer -= dt; if (g.deadTimer <= 0) respawnPlayer(); }
    if (g.state != 2) updateWorld(dt);
}

// --------------------------------------------------------------- render ----
static void drawCar(const Car& c, bool lightsOn, float t, bool policeLights) {
    glPushMatrix();
    glTranslatef(c.x, 0, c.z);
    glRotatef(c.heading * 180.f / PI, 0, 1, 0);
    float k = c.dead ? 0.12f : 1.f;
    if (c.kind == 1 && !c.dead) glColor3f(0.07f, 0.07f, 0.1f); else glColor3f(c.r * k, c.g * k, c.b * k);
    box(0, 0.7f, 0, 2.0f, 0.7f, 4.4f);
    glColor3f(0.08f * (c.dead ? 0.5f : 1.f), 0.12f, 0.18f);
    box(0, 1.28f, -0.3f, 1.72f, 0.55f, 2.3f);
    if (c.kind == 1 && !c.dead) glColor3f(0.07f, 0.07f, 0.1f); else glColor3f(c.r * k, c.g * k, c.b * k);
    box(0, 1.62f, -0.3f, 1.74f, 0.1f, 2.0f);
    glColor3f(0.04f, 0.04f, 0.04f);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) box(sx * 1.02f, 0.35f, sz * 1.4f, 0.3f, 0.7f, 0.7f);
    if (c.kind == 1 && !c.dead) {
        glColor3f(0.95f, 0.95f, 0.95f);
        box(0, 0.78f, 0, 2.02f, 0.28f, 3.0f);
    }
    if (!c.dead) {
        glDisable(GL_LIGHTING);
        glColor3f(lightsOn ? 1.f : 0.75f, lightsOn ? 0.97f : 0.75f, lightsOn ? 0.7f : 0.6f);
        box(-0.7f, 0.75f, 2.21f, 0.4f, 0.25f, 0.08f); box(0.7f, 0.75f, 2.21f, 0.4f, 0.25f, 0.08f);
        glColor3f(0.8f, 0.05f, 0.05f);
        box(-0.7f, 0.75f, -2.21f, 0.4f, 0.22f, 0.08f); box(0.7f, 0.75f, -2.21f, 0.4f, 0.22f, 0.08f);
        if (c.kind == 1 && policeLights) {
            bool a = sinf(t * 14.f) > 0;
            glColor3f(a ? 1.f : 0.2f, 0.1f, 0.1f); box(-0.45f, 1.78f, -0.3f, 0.6f, 0.16f, 0.35f);
            glColor3f(0.1f, 0.2f, a ? 0.2f : 1.f); box(0.45f, 1.78f, -0.3f, 0.6f, 0.16f, 0.35f);
        }
        glEnable(GL_LIGHTING);
    }
    glPopMatrix();
}

static void drawPerson(float x, float z, float h, float phase, float r, float gg, float b, bool armed) {
    glPushMatrix();
    glTranslatef(x, 0, z);
    glRotatef(h * 180.f / PI, 0, 1, 0);
    float sw = sinf(phase) * 0.5f;
    glColor3f(0.12f, 0.12f, 0.2f);
    for (int s = -1; s <= 1; s += 2) {
        glPushMatrix();
        glTranslatef(s * 0.17f, 0.9f, 0);
        glRotatef(s * sw * 57.f, 1, 0, 0);
        box(0, -0.45f, 0, 0.25f, 0.9f, 0.25f);
        glPopMatrix();
    }
    glColor3f(r, gg, b);
    box(0, 1.25f, 0, 0.6f, 0.7f, 0.32f);
    box(-0.4f, 1.3f, 0, 0.15f, 0.6f, 0.15f); box(0.4f, 1.3f, 0, 0.15f, 0.6f, 0.15f);
    glColor3f(0.85f, 0.65f, 0.5f);
    box(0, 1.85f, 0, 0.34f, 0.34f, 0.34f);
    if (armed) { glColor3f(0.1f, 0.1f, 0.1f); box(0.3f, 1.3f, 0.35f, 0.1f, 0.12f, 0.45f); }
    glPopMatrix();
}

struct Label { float x, y; std::string s; };

static void render() {
    Platform& pl = plat();
    float p = fmodf(g.clock / DAY_LEN, 1.f);
    float sunH = sinf(p * 2.f * PI);
    float day = clampf(sunH * 1.8f + 0.4f, 0.f, 1.f);
    float dusk = clampf(1.f - fabsf(sunH) * 4.f, 0.f, 1.f);
    float sky[4] = {lerpf(0.02f, 0.55f, day) + dusk * 0.35f, lerpf(0.03f, 0.75f, day) + dusk * 0.12f, lerpf(0.08f, 0.95f, day), 1.f};
    for (int i = 0; i < 3; i++) sky[i] = clampf(sky[i], 0.f, 1.f);
    glClearColor(sky[0], sky[1], sky[2], 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);

    begin3D(62.f, 0.3f, 700.f);
    // camera
    bool inCar = g.driving >= 0;
    float spd = inCar ? fabsf(cars[g.driving].speed) : 0.f;
    float dist = inCar ? 10.5f + spd * 0.12f : 6.5f, hgt = inCar ? 4.6f : 3.4f;
    float ch = g.camH;
    float tx = g.x, tz = g.z;
    float cx = tx - sinf(ch) * dist, cz = tz - cosf(ch) * dist;
    gluLookAt(cx, hgt, cz, tx + sinf(ch) * 4.f, 1.6f, tz + cosf(ch) * 4.f, 0, 1, 0);

    // lighting + fog
    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    GLfloat lpos[4] = {0.4f, 0.25f + 0.75f * std::max(sunH, 0.f), 0.5f, 0.f};
    GLfloat ldif[4] = {lerpf(0.20f, 1.0f, day) + dusk * 0.1f, lerpf(0.22f, 0.95f, day), lerpf(0.40f, 0.85f, day), 1.f};
    GLfloat lamb[4] = {0, 0, 0, 1};
    GLfloat gamb[4] = {lerpf(0.10f, 0.38f, day), lerpf(0.11f, 0.40f, day), lerpf(0.20f, 0.45f, day), 1.f};
    glLightfv(GL_LIGHT0, GL_POSITION, lpos); glLightfv(GL_LIGHT0, GL_DIFFUSE, ldif); glLightfv(GL_LIGHT0, GL_AMBIENT, lamb);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gamb);
    glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_LINEAR); glFogfv(GL_FOG_COLOR, sky);
    glFogf(GL_FOG_START, lerpf(70.f, 170.f, day)); glFogf(GL_FOG_END, lerpf(300.f, 520.f, day));

    glCallList(cityList);

    // vehicles & people
    bool night = day < 0.55f;
    bool alarm = stars() > 0;
    for (int i = 0; i < MAXCARS; i++) if (cars[i].used) drawCar(cars[i], night, g.clock, alarm);
    for (int i = 0; i < MAXPEDS; i++) if (peds[i].alive) drawPerson(peds[i].x, peds[i].z, peds[i].heading, peds[i].phase, peds[i].r, peds[i].g, peds[i].b, false);
    if (g.driving < 0 && g.state != 1) drawPerson(g.x, g.z, g.heading, g.walk * 4.f, 0.15f, 0.35f, 0.85f, g.ammo > 0);
    else if (g.state == 1) { glPushMatrix(); glTranslatef(g.x, 0.2f, g.z); glRotatef(90, 1, 0, 0); glColor3f(0.5f, 0.1f, 0.1f); box(0, 0, 0, 0.6f, 1.6f, 0.3f); glPopMatrix(); }

    // effects (unlit)
    glDisable(GL_LIGHTING);
    std::vector<Label> labels;
    float pulse = 0.5f + 0.5f * sinf(g.clock * 4.f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    for (const Spot& s : spots) {
        glColor4f(s.r, s.g, s.b, 0.30f + 0.15f * pulse);
        box(s.x, 5.f, s.z, 1.8f, 10.f, 1.8f);
        float sx, sy;
        if (dist2(s.x, s.z, g.x, g.z) < 140.f && project(s.x, 11.f, s.z, sx, sy)) labels.push_back({sx, sy, s.name});
    }
    if (g.job.active) {
        glColor4f(0.2f, 1.f, 1.f, 0.35f + 0.2f * pulse);
        box(g.job.x, 7.f, g.job.z, 2.4f, 14.f, 2.4f);
        float sx, sy;
        if (project(g.job.x, 15.f, g.job.z, sx, sy)) labels.push_back({sx, sy, "DELIVERY"});
    }
    glDepthMask(GL_TRUE);
    if (night) {  // headlight beams
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        float a = (0.55f - day) * 0.55f;
        for (int i = 0; i < MAXCARS; i++) {
            const Car& c = cars[i];
            if (!c.used || c.dead) continue;
            glPushMatrix();
            glTranslatef(c.x, 0.07f, c.z); glRotatef(c.heading * 180.f / PI, 0, 1, 0);
            glBegin(GL_QUADS);
            glColor4f(1.f, 0.95f, 0.7f, a); glVertex3f(-0.9f, 0, 2.3f); glVertex3f(0.9f, 0, 2.3f);
            glColor4f(1.f, 0.95f, 0.7f, 0.f); glVertex3f(5.f, 0, 22.f); glVertex3f(-5.f, 0, 22.f);
            glEnd();
            glPopMatrix();
        }
        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glDisable(GL_BLEND);
    glPointSize(5.f);
    glBegin(GL_POINTS);
    for (const Particle& pt : parts) { float f = pt.life / pt.maxLife; glColor3f(pt.r * f + 0.1f, pt.g * f, pt.b * f); glVertex3f(pt.x, pt.y, pt.z); }
    glEnd();
    if (g.tracerT > 0) {
        glLineWidth(2.5f);
        glColor3f(1.f, 0.9f, 0.4f);
        glBegin(GL_LINES); glVertex3f(g.tx0, 1.3f, g.tz0); glVertex3f(g.tx1, 1.3f, g.tz1); glEnd();
    }

    // ------------------------------------------------------------ HUD ----
    begin2D();
    float W = (float)pl.w, H = (float)pl.h;
    for (const Label& l : labels) text(l.x - (float)l.s.size() * 4.f, l.y, l.s, 1.f, 1.f, 1.f);
    // main panel
    rect(10, 10, 300, 128, 0, 0, 0, 0.45f);
    char buf[160];
    snprintf(buf, sizeof(buf), "CASH  $%d", g.money);
    text(20, 34, buf, 0.4f, 1.f, 0.4f);
    float hp = clampf(g.health / 100.f, 0.f, 1.f);
    text(20, 56, "HEALTH", 1, 1, 1);
    rect(95, 43, 200, 14, 0.2f, 0.2f, 0.2f, 0.9f); rect(95, 43, 200 * hp, 14, 0.85f, 0.15f, 0.15f, 1.f);
    snprintf(buf, sizeof(buf), "AMMO  %d", g.ammo);
    text(20, 78, buf, 1.f, 0.85f, 0.4f);
    int st = stars();
    std::string ws = "WANTED  ";
    ws += st > 0 ? std::string(st, '*') : "-";
    bool flashOn = st == 0 || sinf(g.uptime * 10.f) > 0;
    text(20, 100, ws, flashOn ? 1.f : 0.4f, 0.4f, flashOn ? 0.4f : 1.f);
    snprintf(buf, sizeof(buf), "BILLS DUE  $%d  (next in %ds)", (int)g.billsDue, (int)(75.f - g.billTimer));
    text(20, 122, buf, g.billsDue >= 400 ? 1.f : 0.9f, g.billsDue >= 400 ? 0.3f : 0.9f, g.billsDue >= 400 ? 0.3f : 0.9f);
    float hour = fmodf(6.f + p * 24.f, 24.f);
    snprintf(buf, sizeof(buf), "%02d:%02d", (int)hour, (int)((hour - floorf(hour)) * 60.f));
    text(W - 215, 205 + 20, buf, 1, 1, 1);
    // job line
    if (g.job.active) {
        float d = dist2(g.job.x, g.job.z, g.x, g.z);
        snprintf(buf, sizeof(buf), "JOB: deliver cargo  %dm  %ds left  reward $%d", (int)d, (int)g.job.time, g.job.reward);
        rect(10, 146, 450, 24, 0, 0, 0, 0.45f);
        text(20, 164, buf, 0.4f, 1.f, 1.f);
    }
    // minimap
    {
        float S = 190, mx = W - S - 12, my = 12, ext = HALF + 40;
        rect(mx, my, S, S, 0.08f, 0.09f, 0.1f, 0.85f);
        auto mapX = [&](float x) { return mx + (x + ext) / (2 * ext) * S; };
        auto mapY = [&](float z) { return my + (ext - z) / (2 * ext) * S; };
        for (int i = 0; i < N; i++)
            for (int j = 0; j < N; j++) {
                float x0 = mapX(blockC(i) - 24), x1 = mapX(blockC(i) + 24), y0 = mapY(blockC(j) + 24), y1 = mapY(blockC(j) - 24);
                rect(x0, y0, x1 - x0, y1 - y0, 0.28f, 0.3f, 0.3f, 1.f);
            }
        for (const Spot& s : spots) rect(mapX(s.x) - 4, mapY(s.z) - 4, 8, 8, s.r, s.g, s.b, 1.f);
        if (g.job.active) rect(mapX(g.job.x) - 4, mapY(g.job.z) - 4, 8, 8, 0.2f, 1.f, 1.f, 0.4f + 0.6f * pulse);
        for (int i = 0; i < MAXCARS; i++)
            if (cars[i].used && !cars[i].dead && cars[i].kind == 1) {
                bool a = sinf(g.uptime * 10.f) > 0;
                rect(mapX(cars[i].x) - 2.5f, mapY(cars[i].z) - 2.5f, 5, 5, a ? 1.f : 0.2f, 0.2f, a ? 0.2f : 1.f, 1.f);
            }
        rect(mapX(g.x) - 3.5f, mapY(g.z) - 3.5f, 7, 7, 1.f, 1.f, 0.f, 1.f);
        rect(mapX(g.x + sinf(g.heading) * 12.f) - 1.5f, mapY(g.z + cosf(g.heading) * 12.f) - 1.5f, 3, 3, 1.f, 1.f, 1.f, 1.f);
    }
    // speed
    if (inCar) {
        snprintf(buf, sizeof(buf), "%d km/h", (int)(spd * 3.6f));
        textBig(W - 260, H - 40, buf, 1, 1, 1);
        float chp = clampf(cars[g.driving].hp / 120.f, 0.f, 1.f);
        text(W - 260, H - 62, "CAR", 1, 1, 1);
        rect(W - 215, H - 74, 140, 10, 0.2f, 0.2f, 0.2f, 0.9f); rect(W - 215, H - 74, 140 * chp, 10, 0.95f, 0.6f, 0.1f, 1.f);
    }
    // prompts
    int si = nearestSpot(5.5f);
    std::string prompt;
    if (si >= 0 && g.state == 0) {
        switch (spots[si].type) {
        case S_SUPER: prompt = "[E] Buy burger  +50 health  ($25)"; break;
        case S_AMMO: prompt = "[E] Buy 30 rounds  ($60)"; break;
        case S_JOB: prompt = "[E] Take a delivery job"; break;
        case S_BILLS: prompt = "[E] Pay your bills"; break;
        case S_HOSP: prompt = "[E] Full treatment  ($50)"; break;
        }
    } else if (g.state == 0 && g.driving < 0) {
        for (int i = 0; i < MAXCARS; i++)
            if (cars[i].used && !cars[i].dead && cars[i].kind != 1 && dist2(cars[i].x, cars[i].z, g.x, g.z) < 5.5f) { prompt = "[F] Enter vehicle"; break; }
    } else if (g.state == 0 && g.driving >= 0 && fabsf(spd) < 9.f) prompt = "[F] Exit vehicle";
    if (!prompt.empty()) {
        float w = (float)prompt.size() * 9.f + 24.f;
        rect(W * 0.5f - w * 0.5f, H - 96, w, 28, 0, 0, 0, 0.55f);
        text(W * 0.5f - w * 0.5f + 12, H - 76, prompt, 1, 1, 0.5f);
    }
    // toasts
    float ty = 80;
    for (const Toast& t : toasts) {
        float w = (float)t.s.size() * 9.f + 24.f;
        rect(W * 0.5f - w * 0.5f, ty - 18, w, 26, 0, 0, 0, 0.5f * std::min(1.f, t.t));
        text(W * 0.5f - w * 0.5f + 12, ty, t.s, 1, 1, 1);
        ty += 30;
    }
    if (g.uptime < 25.f) {
        rect(10, H - 112, 520, 100, 0, 0, 0, 0.45f);
        text(20, H - 90, "W/S gas+brake (walk)   A/D steer (turn)   SHIFT sprint", 1, 1, 1);
        text(20, H - 70, "SPACE shoot (on foot) / handbrake (car)", 1, 1, 1);
        text(20, H - 50, "F enter/exit car   E use shop/job/bills   P pause   ESC quit", 1, 1, 1);
        text(20, H - 30, "Cops, bills and hunger are all trying to ruin your day.", 0.8f, 0.8f, 0.8f);
    }
    // overlays
    if (g.hurt > 0) rect(0, 0, W, H, 0.8f, 0.f, 0.f, clampf(g.hurt, 0.f, 0.3f));
    if (g.state == 1) {
        rect(0, 0, W, H, 0.3f, 0, 0, 0.5f);
        textBig(W * 0.5f - textWidthBig("WASTED") * 0.5f, H * 0.5f, "WASTED", 0.9f, 0.1f, 0.1f);
    }
    if (g.state == 2) {
        rect(0, 0, W, H, 0, 0, 0, 0.7f);
        std::string a = "EVICTED - GAME OVER";
        textBig(W * 0.5f - textWidthBig(a) * 0.5f, H * 0.5f - 10, a, 1.f, 0.3f, 0.3f);
        std::string b = "You ignored your bills. Press R to start over.";
        text(W * 0.5f - b.size() * 4.5f, H * 0.5f + 30, b, 1, 1, 1);
    }
    if (g.paused) {
        rect(0, 0, W, H, 0, 0, 0, 0.5f);
        textBig(W * 0.5f - textWidthBig("PAUSED") * 0.5f, H * 0.5f, "PAUSED", 1, 1, 1);
    }
    end2D();
}

// ----------------------------------------------------------------- main ----
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!init("LITE CITY (beta) - LiteEngine", 1280, 720)) {
        MessageBoxA(nullptr, "Could not create an OpenGL window.", "LiteEngine", MB_OK | MB_ICONERROR);
        return 1;
    }
    buildWorld();
    resetGame();
    float dt = 0.016f;
    while (pump(dt)) {
        update(dt);
        // camera heading follows the player's heading smoothly
        float target = g.heading;
        g.camH += wrapAngle(target - g.camH) * std::min(1.f, 4.f * dt);
        render();
        present();
    }
    return 0;
}
