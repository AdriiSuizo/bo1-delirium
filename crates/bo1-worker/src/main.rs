mod disc_setup;
mod disc_motion;
mod protocol;
mod recovery;
mod runtime;
use protocol::*;
use std::{
    ffi::{CString, c_void},
    sync::atomic::{Ordering, fence},
};
type Handle = *mut c_void;
#[link(name = "kernel32")]
unsafe extern "system" {
    fn OpenFileMappingA(access: u32, inherit: i32, name: *const i8) -> Handle;
    fn MapViewOfFile(h: Handle, access: u32, hi: u32, lo: u32, bytes: usize) -> *mut c_void;
    fn UnmapViewOfFile(p: *const c_void) -> i32;
    fn OpenEventA(access: u32, inherit: i32, name: *const i8) -> Handle;
    fn OpenProcess(access: u32, inherit: i32, pid: u32) -> Handle;
    fn WaitForMultipleObjects(n: u32, handles: *const Handle, all: i32, millis: u32) -> u32;
    fn SetEvent(h: Handle) -> i32;
    fn CloseHandle(h: Handle) -> i32;
}
fn main() {
    if let Err(e) = run() {
        eprintln!("Delirium worker failed: {e}");
        std::process::exit(30);
    }
}
fn run() -> Result<(), String> {
    let args: Vec<String> = std::env::args().collect();
    if args.get(1).is_some_and(|value| value == "--prepare-disc") {
        let game = args.get(2).ok_or("Missing BO1 installation path")?;
        let cache = args.get(3).ok_or("Missing Delirium tuning cache path")?;
        return disc_setup::prepare(std::path::Path::new(game), std::path::Path::new(cache));
    }
    let cache = std::env::var_os("BO1_DELIRIUM_GTA_CACHE")
        .ok_or("Missing BO1_DELIRIUM_GTA_CACHE. Run the Plutonium Delirium launcher first.")?;
    disc_setup::load(std::path::Path::new(&cache))?;
    if args.get(1).map(String::as_str) == Some("--selftest") {
        return runtime::selftest();
    }
    if args.get(1).map(String::as_str) == Some("--verify") {
        return runtime::verify();
    }
    if args.get(1).map(String::as_str) == Some("--recover") {
        return runtime::recover_test();
    }
    if args.get(1).map(String::as_str) == Some("--dump") {
        return runtime::dump_test(args.get(2).ok_or("missing dump path")?, args.get(3).is_some_and(|a| a == "rest"));
    }
    if args.get(1).map(String::as_str) == Some("--crowd") {
        let n = args.get(2).and_then(|v| v.parse().ok()).unwrap_or(12);
        let spacing = args.get(3).and_then(|v| v.parse().ok()).unwrap_or(0.6);
        return runtime::crowd_test(n, spacing);
    }
    if args.get(1).map(String::as_str) == Some("--moon") {
        return runtime::moon_test();
    }
    if args.get(1).map(String::as_str) == Some("--blast-recovery") {
        return runtime::blast_recovery_test();
    }
    if args.get(1).map(String::as_str) == Some("--blast-crowd") {
        return runtime::blast_crowd_test(args.get(2).and_then(|v| v.parse().ok()).unwrap_or(20));
    }
    if args.get(1).map(String::as_str) == Some("--front") {
        return runtime::front_test(args.get(2).and_then(|v| v.parse().ok()).unwrap_or(0.0));
    }
    if args.get(1).map(String::as_str) == Some("--corpse") {
        return runtime::corpse_test();
    }
    if args.get(1).map(String::as_str) == Some("--stairs-shot") {
        return runtime::stairs_shot_test(args.get(2).and_then(|v| v.parse().ok()).unwrap_or(8.0), args.get(3).and_then(|v| v.parse().ok()).unwrap_or(10));
    }
    if args.get(1).map(String::as_str) == Some("--melee") {
        return runtime::melee_test();
    }
    if args.get(1).map(String::as_str) == Some("--variety") {
        return runtime::variety_test();
    }
    if args.get(1).map(String::as_str) == Some("--messages") {
        return runtime::message_test();
    }
    if args.get(1).map(String::as_str) == Some("--yaw") {
        return runtime::yaw_test(false);
    }
    if args.get(1).map(String::as_str) == Some("--yaw-large") {
        return runtime::yaw_test(true);
    }
    if args.get(1).map(String::as_str) == Some("--catalog") {
        for m in delirium::msg::MESSAGES
            .iter()
            .chain(delirium::extras::messages())
        {
            println!(
                "{}\t{}\t{}",
                m.name,
                m.params.len(),
                m.params.iter().filter(|p| p.used).count()
            );
        }
        return Ok(());
    }
    let name = args.get(1).ok_or("missing IPC name")?;
    let cname = CString::new(name.as_str()).map_err(|e| e.to_string())?;
    unsafe {
        let mapping = OpenFileMappingA(0xf001f, 0, cname.as_ptr());
        if mapping.is_null() {
            return Err("OpenFileMapping".into());
        }
        let ptr =
            MapViewOfFile(mapping, 0xf001f, 0, 0, std::mem::size_of::<Exchange>()) as *mut Exchange;
        if ptr.is_null() {
            CloseHandle(mapping);
            return Err("MapViewOfFile".into());
        }
        let x = &mut *ptr;
        if x.magic != 0x4e4d3138
            || x.version != 18
            || x.bytes as usize != std::mem::size_of::<Exchange>()
        {
            return Err("IPC layout mismatch".into());
        }
        let request = OpenEventA(
            0x100002,
            0,
            CString::new(format!("{name}-request")).unwrap().as_ptr(),
        );
        let response = OpenEventA(
            0x100002,
            0,
            CString::new(format!("{name}-response")).unwrap().as_ptr(),
        );
        let owner = OpenProcess(0x100000, 0, x.owner);
        if request.is_null() || response.is_null() || owner.is_null() {
            return Err("IPC events/owner".into());
        }
        let mut engine = runtime::Runtime::load(x.flags & 1 != 0)?;
        engine.export(x, 0)?;
        x.response.status = 1;
        fence(Ordering::Release);
        SetEvent(response);
        println!(
            "BO1 character runtime ready: articulated bodies, contextual tasks and interruptible recovery"
        );
        let handles = [request, owner];
        loop {
            let wait = WaitForMultipleObjects(2, handles.as_ptr(), 0, u32::MAX);
            if wait != 0 {
                break;
            }
            fence(Ordering::Acquire);
            let seq = std::ptr::read_volatile(&x.requested);
            let r = x.request;
            let status = engine.command(&r)?;
            engine.export(x, r.actor as usize)?;
            x.response.status = status;
            fence(Ordering::Release);
            std::ptr::write_volatile(&mut x.completed, seq);
            SetEvent(response);
            if r.command == 2 {
                break;
            }
        }
        for h in [request, response, owner, mapping] {
            CloseHandle(h);
        }
        UnmapViewOfFile(ptr as _);
    }
    Ok(())
}

#[cfg(test)]
mod blast_finish_tests {
    #[test]
    fn original_explosion_finish_and_velocity_gate() {
        // As with the upstream oracle tests, local game data is optional in CI.
        let Some(cache) = std::env::var_os("BO1_DELIRIUM_GTA_CACHE") else { return };
        crate::disc_setup::load(std::path::Path::new(&cache)).unwrap();
        use delirium::nm::{self, Task, Next};
        let t = Task::Explosion;
        assert_eq!(t.times(), [1.0, 10.0]);
        let finish = |age, speed, z, health| nm::task_finish(t, age, t.times(), speed, z, false, 0, health);
        assert_eq!(finish(0.9, 0.0, 1.0, 160.0), None);
        assert_eq!(finish(1.1, 0.15, 1.0, 160.0), Some(Next::Blend));
        assert_eq!(finish(1.1, 0.15, 1.0, 100.0), Some(Next::Relax));
        assert_eq!(finish(1.1, 0.3, 1.0, 160.0), None);
        assert_eq!(finish(10.1, 1.9, 1.0, 160.0), Some(Next::Blend));
        assert_eq!(finish(10.1, 2.1, 1.0, 160.0), None);
        assert_eq!(finish(10.1, 2.1, -0.1, 160.0), Some(Next::Blend));
        assert_eq!(finish(100.1, 2.1, 1.0, 160.0), Some(Next::Blend));
    }
}
