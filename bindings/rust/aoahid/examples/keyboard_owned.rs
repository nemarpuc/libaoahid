// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
//! Press and release the letter "a" on the one Android phone that is plugged in.
//!
//! Open a text field on the phone first. The options are `Default`, the values of the C
//! `aoahid_*_init` functions; `aoahid-sys/examples/keyboard.rs` is the same program with every call
//! and value spelled out. The phone must already take AOA 2.0 HID requests (docs/QUICKSTART.md).

use aoahid::{Context, SETTLE};
use aoahid_sys::AOAHID_SEND_TIMEOUT_MS;
use std::time::Duration;

const KEY_A: u16 = 0x04; // HID Keyboard page usage

fn main() -> Result<(), String> {
    let context = Context::new().map_err(|e| format!("{e:?}"))?;
    let device = context.open_only().map_err(|e| format!("{e:?}"))?;
    let keyboard = device.keyboard(&Default::default(), SETTLE).map_err(|e| format!("{e:?}"))?;
    for down in [true, false] {
        keyboard.key(KEY_A, down).map_err(|e| format!("{e:?}"))?;
        keyboard.submit_blocking(AOAHID_SEND_TIMEOUT_MS).map_err(|e| format!("{e:?}"))?;
        std::thread::sleep(Duration::from_millis(50));
    }
    Ok(())
}
