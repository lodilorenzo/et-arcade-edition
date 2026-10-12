#ifndef ET_GAMEPAD_MATH_H
#define ET_GAMEPAD_MATH_H

#include <math.h>

/* Optional native cgame import. No legacy import numbers or wire fields change. */
#define ET_GAMEPAD_AIM 1000

static float GP_Clamp(float x, float lo, float hi) {
    return !isfinite(x) ? lo : x < lo ? lo : x > hi ? hi : x;
}

/* Axial deadzone/remap, then Halo's circle-to-square stick normalization. */
static float GP_Axis(int raw, float deadzone) {
    int dead = (int)(GP_Clamp(deadzone, 0.f, .95f) * 32767.f);
    raw = raw < -32768 ? -32768 : raw > 32767 ? 32767 : raw;
    if (raw > dead) return (float)((raw - dead) * 32767 / (32767 - dead)) / 32767.f;
    if (raw < -dead) return (float)((raw + dead) * -32768 / (-32768 + dead)) / 32768.f;
    return 0.f;
}

static void GP_Stick(int rawX, int rawY, float deadzone, float *x, float *y) {
    float largest, length;
    *x = GP_Axis(rawX, deadzone);
    *y = GP_Axis(rawY, deadzone);
    largest = fmaxf(fabsf(*x), fabsf(*y));
    length = sqrtf(*x * *x + *y * *y);
    if (largest > 0.f) {
        *x = GP_Clamp(*x * length / largest, -1.f, 1.f);
        *y = GP_Clamp(*y * length / largest, -1.f, 1.f);
    }
}

/* ponytail: estimated response knots, replace with Halo globals tag if available.
   The linked halo-re repository does not contain that tag or its control code. */
static float GP_LookCurve(float x) {
    static const float knots[] = {0.f, .08f, .25f, .55f, 1.f};
    float a = GP_Clamp(fabsf(x), 0.f, 1.f) * 4.f;
    int i = (int)a;
    float value = i == 4 ? 1.f : knots[i] + (knots[i + 1] - knots[i]) * (a - i);
    return x < 0.f ? -value : value;
}

static float GP_Acceleration(float yaw, float dt, float duration, float scale, float *held) {
    float result = 1.f;
    if (fabsf(yaw) >= .95f) {
        result += (GP_Clamp(scale, 1.f, 4.f) - 1.f) * GP_Clamp(*held / GP_Clamp(duration, .01f, 2.f), 0.f, 1.f);
        *held = fminf(*held + dt, 2.f);
    } else {
        *held = 0.f;
    }
    return result;
}

/* Full weight in the inner half; linear falloff in the outer half. */
static float GP_Attenuation(float value, float maximum) {
    if (!isfinite(value) || !isfinite(maximum) || maximum <= 0.f || value >= maximum) return 0.f;
    return value <= maximum * .5f ? 1.f : 2.f * (maximum - value) / maximum;
}

/* Closest capsule silhouette point to the camera ray (ET's encoded player bounds). */
static void GP_CapsulePoint(const float *eye, const float *forward, const float *origin,
    float bottom, float top, float radius, float *point) {
    float h2 = forward[0] * forward[0] + forward[1] * forward[1];
    float z = eye[2], projection = 0.f, normal[3], length;
    int i;
    if (h2 > .0001f)
        z += ((origin[0] - eye[0]) * forward[0] + (origin[1] - eye[1]) * forward[1]) / h2 * forward[2];
    point[0] = origin[0]; point[1] = origin[1];
    point[2] = GP_Clamp(z, origin[2] + bottom, origin[2] + top);
    for (i = 0; i < 3; ++i) projection += (point[i] - eye[i]) * forward[i];
    projection = fmaxf(projection, 0.f);
    for (i = 0; i < 3; ++i) normal[i] = point[i] - (eye[i] + projection * forward[i]);
    length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (length > .0001f)
        for (i = 0; i < 3; ++i) point[i] -= normal[i] * fminf(radius / length, 1.f);
}

/* Relative angular velocity in degrees/second; ET pitch points downward. */
static void GP_TargetVelocity(const float *p, const float *v, float *yaw, float *pitch) {
    float horizontal2, horizontal, distance2;
    int i;
    *yaw = *pitch = 0.f;
    for (i = 0; i < 3; ++i) if (!isfinite(p[i]) || !isfinite(v[i])) return;
    horizontal2 = p[0] * p[0] + p[1] * p[1];
    horizontal = sqrtf(horizontal2); distance2 = horizontal2 + p[2] * p[2];
    if (horizontal2 < .0001f || !isfinite(distance2)) return;
    *yaw = GP_Clamp((p[0] * v[1] - p[1] * v[0]) / horizontal2 * 57.2957795f, -180.f, 180.f);
    *pitch = GP_Clamp(-(horizontal * v[2] - p[2] / horizontal * (p[0] * v[0] + p[1] * v[1])) / distance2 * 57.2957795f, -90.f, 90.f);
}

/* Player strength multiplier for both assist terms; slowdown stays below a full stop. */
static void GP_AssistStrength(float strength, float *friction, float *adhesion) {
    strength = GP_Clamp(strength, 0.f, 2.f);
    *friction = GP_Clamp(*friction * strength, 0.f, .9f);
    *adhesion = GP_Clamp(*adhesion * strength, 0.f, 1.f);
}
static float GP_Assist(float inputRate, float targetRate, float level, float friction, float adhesion, float limit) {
    level = GP_Clamp(level, 0.f, 1.f);
    return inputRate * (1.f - level * GP_Clamp(friction, 0.f, 1.f)) +
        GP_Clamp(targetRate, -limit, limit) * level * GP_Clamp(adhesion, 0.f, 1.f);
}

#endif
