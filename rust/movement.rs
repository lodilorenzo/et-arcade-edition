// Rust adaptation of game/bg_pmove.c, Copyright (C) 1999-2010 id Software LLC.
// GPL-3.0-or-later with additional terms: engine/COPYING.txt. Altered 2026-10-06.

/// Clip velocity against a plane with ET's original operation order.
///
/// # Safety
/// `input` and `normal` must each point to three aligned, initialized floats.
/// `output` must be writable for three aligned floats, with no concurrent access.
/// Input/output overlap is supported, including partial overlap, as in C.
#[unsafe(export_name = "ET_PM_ClipVelocity")]
pub unsafe extern "C" fn PM_ClipVelocity(
    input: *const f32,
    normal: *const f32,
    output: *mut f32,
    overbounce: f32,
) {
    unsafe {
        let mut backoff =
            *input * *normal + *input.add(1) * *normal.add(1) + *input.add(2) * *normal.add(2);
        if backoff < 0.0 {
            backoff *= overbounce;
        } else {
            backoff /= overbounce;
        }
        for i in 0..3 {
            let change = *normal.add(i) * backoff;
            // Do not copy the inputs first: C reads sequentially when buffers overlap.
            *output.add(i) = *input.add(i) - change;
        }
    }
}

/// Apply ET's q2-style acceleration, including its variable AI friction.
///
/// # Safety
/// `velocity` must be writable for three aligned, initialized floats, and
/// `direction` readable for three, without concurrent access. Overlap is allowed.
#[unsafe(export_name = "ET_PM_Accelerate")]
pub unsafe extern "C" fn PM_Accelerate(
    velocity: *mut f32,
    direction: *const f32,
    wishspeed: f32,
    acceleration: f32,
    frametime: f32,
    on_ground: i32,
    friction: f32,
) {
    unsafe {
        let currentspeed = *velocity * *direction
            + *velocity.add(1) * *direction.add(1)
            + *velocity.add(2) * *direction.add(2);
        let addspeed = wishspeed - currentspeed;
        if addspeed <= 0.0 {
            return;
        }
        let mut accelspeed = acceleration * frametime * wishspeed;
        if accelspeed > addspeed {
            accelspeed = addspeed;
        }
        if on_ground != 0 {
            // C's 1.0 literal promotes the reciprocal AND multiplication to double.
            accelspeed = (accelspeed as f64 * (1.0 / friction as f64)) as f32;
        }
        if accelspeed > addspeed {
            accelspeed = addspeed;
        }
        for i in 0..3 {
            // Preserve sequential C reads, including partially overlapping direction.
            *velocity.add(i) += accelspeed * *direction.add(i);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn acceleration_caps_friction_and_overlap() {
        let direction = [1.0, 0.0, 0.0];
        for (start, friction, expected) in
            [(400.0, 2.0, 400.0), (0.0, 2.0, 160.0), (300.0, 0.25, 320.0)]
        {
            let mut velocity = [start, 0.0, 0.0];
            unsafe {
                PM_Accelerate(
                    velocity.as_mut_ptr(),
                    direction.as_ptr(),
                    320.0,
                    100.0,
                    0.2,
                    1,
                    friction,
                );
            }
            assert_eq!(velocity, [expected, 0.0, 0.0]);
        }
        let mut velocity = [0.0; 3];
        unsafe {
            PM_Accelerate(
                velocity.as_mut_ptr(),
                direction.as_ptr(),
                100.0,
                1.0,
                0.1,
                1,
                3.0,
            );
        }
        assert_eq!(velocity[0].to_bits(), 0x4055_5555); // A float reciprocal would round one ULP higher.
        let mut shifted = [1.0, 2.0, 3.0, 4.0];
        let pointer = shifted.as_mut_ptr();
        unsafe { PM_Accelerate(pointer.add(1), pointer, 30.0, 2.0, 1.0, 1, 2.0) };
        assert_eq!(shifted, [1.0, 7.0, 38.0, 194.0]);
    }

    #[test]
    fn both_directions_and_overlapping_buffers() {
        let normal = [0.0, 0.0, 1.0];
        for (z, expected) in [(-2.0, 2.0), (2.0, 1.0)] {
            let mut velocity = [8.0, 4.0, z];
            let pointer = velocity.as_mut_ptr();
            unsafe { PM_ClipVelocity(pointer, normal.as_ptr(), pointer, 2.0) };
            assert_eq!(velocity, [8.0, 4.0, expected]);
        }
        let mut shifted = [1.0, 2.0, 3.0, 4.0];
        let pointer = shifted.as_mut_ptr();
        unsafe { PM_ClipVelocity(pointer, normal.as_ptr(), pointer.add(1), 1.0) };
        assert_eq!(shifted, [1.0, 1.0, 1.0, -2.0]);
        let input = [1.0, 2.0, 3.0];
        let mut shifted_normal = [0.0, 0.0, 1.0, 99.0];
        let pointer = shifted_normal.as_mut_ptr();
        unsafe { PM_ClipVelocity(input.as_ptr(), pointer, pointer.add(1), 1.0) };
        assert_eq!(shifted_normal, [0.0, 1.0, -1.0, 6.0]);
    }
}
