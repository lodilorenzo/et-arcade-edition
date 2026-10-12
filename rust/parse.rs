// Rust adaptation of game/q_shared.c, Copyright (C) 1999-2010 id Software LLC.
// GPL-3.0-or-later with additional terms: engine/COPYING.txt. Altered 2026-10-06.
use core::ffi::c_char;

// Per-call cursor/buffer only. Session state and token ownership stay in C.
struct Lexer<'a> {
    cursor: *mut c_char,
    // Raw storage preserves C's valid case of parsing a suffix of its own token.
    token: *mut c_char,
    capacity: usize,
    len: usize,
    lines: &'a mut i32,
}

impl Lexer<'_> {
    fn peek(&self) -> u8 {
        unsafe { *self.cursor as u8 }
    }

    // Called only after observing a nonzero byte, so the next byte is readable.
    fn next(&self) -> u8 {
        unsafe { *self.cursor.add(1) as u8 }
    }

    fn take(&mut self) -> u8 {
        let c = self.peek();
        // Malformed quoted input stops at NUL, never one byte past it like C did.
        if c != 0 {
            self.cursor = unsafe { self.cursor.add(1) };
        }
        c
    }

    fn push(&mut self, c: u8) {
        if self.len < self.capacity - 1 {
            unsafe { *self.token.add(self.len) = c as c_char };
        }
        // Saturate at capacity: reject overlong tokens, always reserve a NUL.
        if self.len < self.capacity {
            self.len += 1;
        }
    }

    fn finish(&mut self, data: *mut *mut c_char) -> *mut c_char {
        let end = if self.len < self.capacity {
            self.len
        } else {
            0
        };
        unsafe {
            *self.token.add(end) = 0;
            *data = self.cursor;
        }
        self.token
    }
}

/// Tokenize into the existing C-owned token buffer; no allocation or global Rust state.
///
/// # Safety
/// `data` and `lines` must be aligned, initialized, exclusively writable and non-overlapping.
/// `*data` is either null or points into a readable NUL-terminated byte string.
/// `token` must be writable for `capacity >= 1` bytes and disjoint from `data`/`lines`.
/// The input must be disjoint from `lines`; it may be a suffix of `token` like in C.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ET_ParseExt(
    data: *mut *mut c_char,
    allow_line_breaks: i32,
    token: *mut c_char,
    capacity: usize,
    lines: *mut i32,
) -> *mut c_char {
    assert!(capacity > 0);
    // C clears the shared output before reading input, including self-tokenization.
    unsafe { *token = 0 };
    let mut lex = Lexer {
        cursor: unsafe { *data },
        token,
        capacity,
        len: 0,
        lines: unsafe { &mut *lines },
    };
    if lex.cursor.is_null() {
        return lex.finish(data);
    }
    let mut newlines = false;
    loop {
        // C's char is signed on the supported Windows/x86 build. Keep byte semantics.
        while (lex.peek() as i8) <= 32 {
            if lex.peek() == 0 {
                lex.cursor = core::ptr::null_mut();
                return lex.finish(data);
            }
            if lex.take() == b'\n' {
                *lex.lines = lex.lines.wrapping_add(1);
                newlines = true;
            }
        }
        if newlines && allow_line_breaks == 0 {
            return lex.finish(data);
        }
        if lex.peek() == b'/' && lex.next() == b'/' {
            lex.take();
            lex.take();
            while lex.peek() != 0 && lex.peek() != b'\n' {
                lex.take();
            }
        } else if lex.peek() == b'/' && lex.next() == b'*' {
            lex.take();
            lex.take();
            // Legacy block-comment newlines intentionally do not update com_lines.
            while lex.peek() != 0 && !(lex.peek() == b'*' && lex.next() == b'/') {
                lex.take();
            }
            if lex.peek() != 0 {
                lex.take();
                lex.take();
            }
        } else {
            break;
        }
    }
    if lex.peek() == b'"' {
        lex.take();
        loop {
            let mut c = lex.take();
            // ET's special \"...\" string-in-string syntax, not generic C escapes.
            if c == b'\\' && lex.peek() == b'"' {
                lex.push(b'"');
                lex.take();
                loop {
                    c = lex.take();
                    if c == 0 {
                        break;
                    }
                    if c == b'\\' && lex.peek() == b'"' {
                        lex.push(b'"');
                        lex.take();
                        c = lex.take();
                        break;
                    }
                    lex.push(c);
                }
            }
            if c == b'"' || c == 0 {
                return lex.finish(data);
            }
            lex.push(c);
        }
    }
    loop {
        let c = lex.take();
        lex.push(c);
        if lex.peek() == b'\n' {
            // C counts this delimiter here and again when the next parse skips it.
            *lex.lines = lex.lines.wrapping_add(1);
        }
        if (lex.peek() as i8) <= 32 {
            return lex.finish(data);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bounds_and_malformed_input() {
        let mut lines = 0;
        let mut out = [0xa5u8; 6]; // Four-byte buffer surrounded by canaries.
        for input in [b"\"abcdef\"\0".as_slice(), b"abcdef\0"] {
            let mut cursor = input.as_ptr().cast_mut().cast();
            unsafe {
                ET_ParseExt(
                    &mut cursor,
                    1,
                    out.as_mut_ptr().add(1).cast(),
                    4,
                    &mut lines,
                )
            };
            assert_eq!((out[0], out[1], out[5]), (0xa5, 0, 0xa5));
        }
        for (input, expected) in [
            (b"\"abc\0".as_slice(), b"abc\0".as_slice()),
            (b"\"\\\"a\0", b"\"a\0"),
            (b"\"a\\\0", b"a\\\0"),
        ] {
            let mut cursor = input.as_ptr().cast_mut().cast();
            unsafe {
                ET_ParseExt(
                    &mut cursor,
                    1,
                    out.as_mut_ptr().add(1).cast(),
                    4,
                    &mut lines,
                )
            };
            assert_eq!(&out[1..1 + expected.len()], expected);
            assert_eq!((out[0], out[5]), (0xa5, 0xa5));
            assert_eq!(cursor, unsafe {
                input.as_ptr().add(input.len() - 1).cast_mut().cast()
            });
            unsafe {
                ET_ParseExt(
                    &mut cursor,
                    1,
                    out.as_mut_ptr().add(1).cast(),
                    4,
                    &mut lines,
                )
            };
            assert!(cursor.is_null());
        }
    }
}
