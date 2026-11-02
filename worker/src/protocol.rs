// The existing BO1 C++ host's numeric exchange. No game assets are embedded.
pub const PARTS: usize = 21;
pub const ACTORS: usize = 32;
pub const FRAGMENTS: usize = 128;
pub const BODY_SUPPORT: u32 = 1 << 30;
pub const BODY_CONTACT: u32 = 1 << 28;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Hit {
    pub part: u32,
    pub actor: u32,
    pub point: [f32; 4],
    pub impulse: [f32; 4],
    pub bullet_velocity: [f32; 4],
    pub normal: [f32; 4],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Mesh {
    pub vertices: u32,
    pub triangles: u32,
    pub points: [[f32; 4]; 128],
    pub indices: [[u16; 3]; 256],
    pub transform: [f32; 16],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Initial {
    pub pose: [[f32; 16]; PARTS],
    pub linear: [[f32; 4]; PARTS],
    pub angular: [[f32; 4]; PARTS],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Target {
    pub position: [f32; 4],
    pub lean: f32,
    pub force: u32,
    pub enabled: u32,
    pub reach_position: [f32; 4],
    pub reach: f32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct GaitReference {
    pub rotation: [[i16; 4]; 7],
    pub weight: u16,
    pub stance: u16,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Advance {
    pub hits: [Hit; 32],
    pub targets: [Target; ACTORS],
    pub target_mask: u32,
    pub gait: [GaitReference; ACTORS],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Fragment {
    pub pose: [f32; 16],
    pub dimensions: [f32; 4],
    pub linear: [f32; 4],
    pub angular: [f32; 4],
    pub mass: f32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub union Payload {
    pub advance: Advance,
    pub mesh: Mesh,
    pub initial: Initial,
    pub target: Target,
    pub fragment: Fragment,
    pub active: u32,
    pub bytes: [u8; 5764],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Request {
    pub command: u32,
    pub steps: u32,
    pub hit_count: u32,
    pub actor: u32,
    pub data: Payload,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ActorState {
    pub present: u32,
    pub active_controllers: u32,
    pub poses: [[f32; 16]; PARTS],
    pub com: [f32; 4],
    pub velocity: [f32; 4],
    pub feedback: [u32; 6],
    pub recovery_active: u32,
    pub recovery_completed: u32,
    pub recovery_interrupted: u32,
    pub recovery_time: f32,
    pub support_mask: u32,
    pub foot_speed: f32,
    pub trunk_angular_speed: f32,
    pub detached_mask: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Response {
    pub status: u32,
    pub frame: u32,
    pub parts: u32,
    pub active_controllers: u32,
    pub poses: [[f32; 16]; PARTS],
    pub com: [f32; 4],
    pub velocity: [f32; 4],
    pub feedback: [u32; 6],
    pub recovery_active: u32,
    pub recovery_completed: u32,
    pub recovery_interrupted: u32,
    pub recovery_time: f32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Shape {
    pub orientation: [f32; 4],
    pub offset: [f32; 4],
    pub size: [f32; 4],
    pub length: f32,
    pub radius: f32,
    pub boxed: u32,
    pub mass: f32,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Rig {
    pub parent: [i32; PARTS],
    pub anchor: [[f32; 4]; PARTS],
    pub shapes: [Shape; PARTS],
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct FragmentState {
    pub present: u32,
    pub pose: [f32; 16],
}
#[repr(C)]
pub struct Exchange {
    pub magic: u32,
    pub version: u32,
    pub bytes: u32,
    pub owner: u32,
    pub flags: u32,
    pub requested: i32,
    pub completed: i32,
    pub request: Request,
    pub batch: [Request; 32],
    pub response: Response,
    pub actors: [ActorState; ACTORS],
    pub rig: Rig,
    pub fragments: [FragmentState; FRAGMENTS],
}
const _: () = {
    assert!(std::mem::size_of::<Exchange>() == 248748);
    assert!(std::mem::size_of::<Response>() == 1432);
    assert!(std::mem::size_of::<ActorState>() == 1440);
    assert!(std::mem::size_of::<Request>() == 5780);
};
