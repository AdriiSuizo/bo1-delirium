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
    if args.get(1).map(String::as_str) == Some("--verify") {
        return runtime::verify();
    }
    if args.get(1).map(String::as_str) == Some("--recover") {
        return runtime::recover_verify();
    }
    if args.get(1).map(String::as_str) == Some("--lifecycle") {
        return runtime::lifecycle_verify();
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
            || x.version != 19
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
        println!("Official Delirium Physics SDK v0.5.0 b0138418: x64 SDK worker, protocol 19");
        let handles = [request, owner];
        loop {
            let wait = WaitForMultipleObjects(2, handles.as_ptr(), 0, u32::MAX);
            if wait != 0 {
                break;
            }
            fence(Ordering::Acquire);
            let seq = std::ptr::read_volatile(&x.requested);
            let r = x.request;
            let status = if r.command == 16 {
                let mut status = 2;
                for item in x.batch.iter().take(r.steps.min(32) as usize) {
                    status = engine.command(item)?;
                    if status != 2 {
                        break;
                    }
                }
                status
            } else {
                engine.command(&r)?
            };
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
