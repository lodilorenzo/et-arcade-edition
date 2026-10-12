// Rust adaptation of game/q_math.c and game/q_shared.c, Copyright (C) 1999-2010 id Software LLC.
// GPL-3.0-or-later with additional terms: engine/COPYING.txt. Altered 2026-10-06.

#[unsafe(export_name = "ET_ClampChar")]
pub extern "C" fn ClampChar(value: i32) -> i8 {
    value.clamp(-128, 127) as i8
}

#[unsafe(export_name = "ET_ClampShort")]
pub extern "C" fn ClampShort(value: i32) -> i16 {
    value.clamp(-32768, 32767) as i16
}

#[unsafe(export_name = "ET_Q_fabs")]
pub extern "C" fn Q_fabs(value: f32) -> f32 {
    value.abs()
}

#[unsafe(export_name = "ET_Q_rsqrt")]
pub extern "C" fn Q_rsqrt(number: f32) -> f32 {
    // Preserve ET's approximation, including operation order; sqrt().recip() differs.
    let x2 = number * 0.5;
    let bits = 0x5f3759df_i32.wrapping_sub((number.to_bits() as i32) >> 1);
    let y = f32::from_bits(bits as u32);
    y * (1.5 - (x2 * y * y))
}

#[unsafe(export_name = "ET_LerpAngle")]
pub extern "C" fn LerpAngle(from: f32, mut to: f32, fraction: f32) -> f32 {
    if to - from > 180.0 {
        to -= 360.0;
    }
    if to - from < -180.0 {
        to += 360.0;
    }
    from + fraction * (to - from)
}

#[unsafe(export_name = "ET_Com_Clamp")]
pub extern "C" fn Com_Clamp(min: f32, max: f32, value: f32) -> f32 {
    // Unlike f32::clamp, C accepts reversed bounds and preserves NaNs/signed zero.
    if value < min {
        min
    } else if value > max {
        max
    } else {
        value
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn boundaries_and_operation_order() {
        assert_eq!(ClampChar(i32::MIN), -128);
        assert_eq!(ClampChar(128), 127);
        assert_eq!(ClampShort(-32769), -32768);
        assert_eq!(ClampShort(i32::MAX), 32767);
        assert_eq!(Q_fabs(-0.0).to_bits(), 0);
        assert_eq!(Com_Clamp(-1.0, 1.0, -0.0).to_bits(), (-0.0f32).to_bits());
        assert_eq!(Com_Clamp(5.0, -5.0, 0.0), 5.0);
        assert!(Com_Clamp(-1.0, 1.0, f32::NAN).is_nan());
        assert_eq!(LerpAngle(350.0, 10.0, 0.5), 360.0);
        assert_eq!(LerpAngle(10.0, 350.0, 0.5), 0.0);
        assert_eq!(Q_rsqrt(1.0).to_bits(), 0x3f7f910f);
    }
}
