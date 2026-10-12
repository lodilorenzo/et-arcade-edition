// Rust adaptation of engine/src/game/q_math.c, Copyright (C) 1999-2010 id Software LLC.
// GPL-3.0-or-later with upstream additional terms: see engine/COPYING.txt.
// Altered version: seeded RNG and shared routines ported to Rust, 2026-10-06.
#![allow(non_snake_case)] // Keep names aligned with the original C API.
mod math;
mod movement;
mod parse;

/// Advance ET's deterministic 32-bit generator (not a security RNG).
///
/// # Safety
/// `seed` must point to an aligned, initialized, exclusively writable C int32.
#[unsafe(export_name = "ET_Q_rand")]
pub unsafe extern "C" fn Q_rand(seed: *mut i32) -> i32 {
    // Explicit wrapping preserves the original x86 signed-overflow behavior.
    unsafe {
        *seed = (*seed).wrapping_mul(69069).wrapping_add(1);
        *seed
    }
}

/// Return the next sample in [0, 1).
///
/// # Safety
/// Same pointer requirements as `Q_rand`.
#[unsafe(export_name = "ET_Q_random")]
pub unsafe extern "C" fn Q_random(seed: *mut i32) -> f32 {
    (unsafe { Q_rand(seed) } & 0xffff) as f32 / 65536.0
}

/// Return the next sample in [-1, 1).
///
/// # Safety
/// Same pointer requirements as `Q_rand`.
#[unsafe(export_name = "ET_Q_crandom")]
pub unsafe extern "C" fn Q_crandom(seed: *mut i32) -> f32 {
    2.0 * (unsafe { Q_random(seed) } - 0.5)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn original_sequence_and_float_bits() {
        let mut seed = 0;
        for expected in [1, 69070, 475628535, -1017563188] {
            assert_eq!(unsafe { Q_rand(&mut seed) }, expected);
            assert_eq!(seed, expected);
        }
        let mut seed = 0;
        assert_eq!(
            unsafe { Q_random(&mut seed) }.to_bits(),
            (1.0f32 / 65536.0).to_bits()
        );
        let mut seed = 0;
        assert_eq!(
            unsafe { Q_crandom(&mut seed) }.to_bits(),
            (-1.0f32 + 2.0 / 65536.0).to_bits()
        );
        let mut seed = i32::MIN;
        assert_eq!(unsafe { Q_rand(&mut seed) }, i32::MIN + 1);
    }
}
