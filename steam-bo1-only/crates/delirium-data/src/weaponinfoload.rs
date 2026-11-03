use crate::map::hash_bytes;
use crate::weapons;

pub type Row = [u8; ROW];
pub const ROW: usize = 272;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Element {
    pub name: String,
    pub kind: u8,
    pub text: Option<String>,
    pub attrs: Vec<(String, String)>,
    pub children: Vec<Element>,
}

fn hash(name: &str) -> u32 {
    hash_bytes(name.as_bytes(), 0)
}

const RELOAD: u32 = 0x5da46b68;
const DAMAGE: u32 = 0x431375e1;
const AIMING: u32 = 0x09a5b381;
const PROJECTILE: u32 = 0x4ace140a;
const FLAGS: u32 = 0x67d26872;
const PICKUP: u32 = 0xbc9403c8;
const PHYSICS: u32 = 0x22b6caa7;
const CONTROLLER: u32 = 0xcebcfd68;
const OFFSET: u32 = 0xa13c79ef;
const CROUCHED_OFFSET: u32 = 0x04bf038a;
const AIM_PITCH: u32 = 0x9446c004;
const AIM_STANCE: u32 = 0xcb57b1ff;
const TYPE_TO_CREATE: u32 = 0x82527040;
const EXPLOSION: u32 = 0xdd91088f;
const PROJECTILE_VECTOR: u32 = 0x36f9e805;

pub const WEAPON_TYPES: [&str; 58] = [
    "UNARMED", "BASEBALLBAT", "POOLCUE", "KNIFE", "GRENADE", "MOLOTOV", "ROCKET", "PISTOL", "UNUSED0", "DEAGLE", "SHOTGUN",
    "BARETTA", "MICRO_UZI", "MP5", "AK47", "M4", "SNIPERRIFLE", "M40A1", "RLAUNCHER", "FTHROWER", "MINIGUN", "EPISODIC_1",
    "EPISODIC_2", "EPISODIC_3", "EPISODIC_4", "EPISODIC_5", "EPISODIC_6", "EPISODIC_7", "EPISODIC_8", "EPISODIC_9",
    "EPISODIC_10", "EPISODIC_11", "EPISODIC_12", "EPISODIC_13", "EPISODIC_14", "EPISODIC_15", "EPISODIC_16", "EPISODIC_17",
    "EPISODIC_18", "EPISODIC_19", "EPISODIC_20", "EPISODIC_21", "EPISODIC_22", "EPISODIC_23", "EPISODIC_24", "CAMERA", "OBJECT",
    "WEAPONTYPE_LAST_WEAPONTYPE", "ARMOUR", "RAMMEDBYCAR", "RUNOVERBYCAR", "EXPLOSION", "UZI_DRIVEBY", "DROWNING", "FALL",
    "UNIDENTIFIED", "ANYMELEE", "ANYWEAPON",
];
pub const FIRE_TYPES: [&str; 8] = ["MELEE", "INSTANT_HIT", "DELAYED_HIT", "PROJECTILE", "AREA_EFFECT", "CAMERA", "USE", "UNKNOWN"];
pub const PROJECTILE_TYPES: [&str; 7] = ["NONE", "FUSE", "IMPACT", "ROCKET", "STICKY", "UNKNOWN", ""];
pub const EXPLOSIONS: [&str; 26] = [
    "GRENADE", "MOLOTOV", "ROCKET", "HI_OCTANE", "CAR", "PLANE", "PETROL_PUMP", "BIKE", "DIR_STEAM", "DIR_FLAME",
    "DIR_WATER_HYDRANT", "DIR_GAS_CANISTER", "BOAT", "SHIP_DESTROY", "TRUCK", "EPISODIC_1", "EPISODIC_2", "EPISODIC_3",
    "EPISODIC_4", "EPISODIC_5", "EPISODIC_6", "EPISODIC_7", "EPISODIC_8", "EPISODIC_9", "EPISODIC_10", "UNKNOWN",
];

fn attr<'a>(el: &'a Element, name: &str) -> Option<&'a str> {
    el.attrs.iter().find(|(k, _)| k.eq_ignore_ascii_case(name)).map(|(_, v)| v.as_str())
}

fn attr_or_empty<'a>(el: &'a Element, name: &str) -> &'a str {
    attr(el, name).unwrap_or("")
}

pub fn int_text(s: &str) -> i32 {
    s.trim().parse().unwrap_or(0)
}

pub fn float_text(s: &str) -> f32 {
    s.trim().parse().unwrap_or(0.0)
}

pub fn token(text: Option<&str>) -> &str {
    text.and_then(|t| t.split_whitespace().next()).unwrap_or("")
}

pub fn hex_text(s: &str) -> u32 {
    let digits: String = s.chars().take_while(|c| c.is_ascii_hexdigit()).collect();
    if digits.is_empty() {
        0
    } else {
        u32::from_str_radix(&digits, 16).unwrap_or(u32::MAX)
    }
}

fn int_attr(el: &Element, name: &str, default: i32) -> i32 {
    attr(el, name).map_or(default, int_text)
}

fn float_attr(el: &Element, name: &str, default: f32) -> f32 {
    attr(el, name).map_or(default, float_text)
}

fn put32(row: &mut Row, at: usize, v: u32) {
    row[at..at + 4].copy_from_slice(&v.to_be_bytes());
}

fn put16(row: &mut Row, at: usize, v: u16) {
    row[at..at + 2].copy_from_slice(&v.to_be_bytes());
}

fn putf(row: &mut Row, at: usize, v: f32) {
    put32(row, at, v.to_bits());
}

fn get32(row: &Row, at: usize) -> u32 {
    u32::from_be_bytes([row[at], row[at + 1], row[at + 2], row[at + 3]])
}

fn vector(el: &Element, row: &mut Row, at: usize) {
    for (i, k) in ["x", "y", "z"].iter().enumerate() {
        putf(row, at + 4 * i, float_attr(el, k, 0.0));
    }
}

pub fn fire_type_index(name: &str) -> i32 {
    if name.is_empty() {
        return 1;
    }
    FIRE_TYPES.iter().position(|t| t.eq_ignore_ascii_case(name)).map_or(1, |i| i as i32)
}

pub fn projectile_type_index(name: &str) -> i32 {
    if name.is_empty() {
        return 5;
    }
    PROJECTILE_TYPES.iter().position(|t| t.eq_ignore_ascii_case(name)).map_or(5, |i| i as i32)
}

pub fn explosion_index(name: &str) -> i32 {
    if name.is_empty() {
        return -1;
    }
    EXPLOSIONS.iter().position(|t| t.eq_ignore_ascii_case(name)).map_or(-1, |i| i as i32)
}

pub fn weapon_type_index(name: &str, default: i32) -> i32 {
    if name.is_empty() {
        return default;
    }
    let h = hash(name);
    WEAPON_TYPES.iter().position(|t| hash(t) == h).map_or(default, |i| i as i32)
}

pub fn aiming(el: &Element, row: &mut Row) {
    putf(row, 0x34, float_attr(el, "accuracy", 1.0));
    putf(row, 0x38, float_attr(el, "accuracyfps", 1.0));
    putf(row, 0xF8, float_attr(el, "accuracyTime", 0.0));
    put32(row, 0xFC, int_attr(el, "pellets", 0) as u32);
    for c in &el.children {
        match hash(&c.name) {
            OFFSET => vector(c, row, 0x40),
            CROUCHED_OFFSET => vector(c, row, 0x50),
            AIM_PITCH => putf(row, 0x60, float_attr(c, "pitch", 0.0)),
            AIM_STANCE => {
                putf(row, 0x64, float_attr(c, "standing", 0.0));
                putf(row, 0x68, float_attr(c, "ducked", 0.0));
                putf(row, 0x6C, float_attr(c, "scale", 0.0));
            }
            _ => {}
        }
    }
}

pub fn projectile(el: &Element, row: &mut Row) {
    if get32(row, 8) != 3 {
        return;
    }
    put32(row, 0xA0, projectile_type_index(attr_or_empty(el, "type")) as u32);
    put32(row, 0xA4, int_attr(el, "fusetime", 0) as u32);
    for c in &el.children {
        match hash(&c.name) {
            TYPE_TO_CREATE => put32(row, 0xA8, weapon_type_index(attr_or_empty(c, "type"), 0) as u32),
            OFFSET => vector(c, row, 0xB0),
            EXPLOSION => put32(row, 0xAC, explosion_index(attr_or_empty(c, "type")) as u32),
            PHYSICS => {
                putf(row, 0x18, float_attr(c, "force", 0.0));
                putf(row, 0xD0, float_attr(c, "explodeimpactthreshold", 0.0));
                putf(row, 0xD4, float_attr(c, "explodeimpactwithvehiclethreshold", 0.0));
                putf(row, 0xD8, float_attr(c, "vehiclevelocity", 0.0));
            }
            PROJECTILE_VECTOR => vector(c, row, 0xC0),
            _ => {}
        }
    }
}

pub fn flags(el: &Element, row: &mut Row) {
    for c in &el.children {
        if c.kind == 0 {
            continue;
        }
        if c.name.eq_ignore_ascii_case("flag") {
            let bit = weapons::flag_bit(token(c.text.as_deref()));
            let word = get32(row, 0x20);
            let add = if (0..32).contains(&bit) { 1u32 << bit } else { 0 };
            put32(row, 0x20, word | add);
        } else if c.name.eq_ignore_ascii_case("flags") {
            put32(row, 0x20, hex_text(token(c.text.as_deref())));
        }
    }
}

pub fn controller(el: &Element, row: &mut Row) {
    for c in &el.children {
        if c.name.eq_ignore_ascii_case("rumble") {
            put32(row, 0x70, int_attr(c, "duration", 0) as u32);
            putf(row, 0x74, float_attr(c, "intensity", 0.0));
        }
    }
}

pub fn load(el: &Element, row: &mut Row) {
    put32(row, 4, weapons::slot_index(attr_or_empty(el, "slot")) as u32);
    put32(row, 8, fire_type_index(attr_or_empty(el, "firetype")) as u32);
    put32(row, 0xC, weapons::damage_type_index(attr_or_empty(el, "damagetype")) as u32);
    put32(row, 0x10, weapons::group_index(attr_or_empty(el, "group")) as u32);
    putf(row, 0x14, float_attr(el, "targetrange", 0.0));
    putf(row, 0x18, float_attr(el, "weaponrange", 0.0));
    put32(row, 0x1C, weapon_type_index(attr_or_empty(el, "stattype"), -1) as u32);
    put16(row, 0x86, int_attr(el, "clipsize", 0) as u16);
    let ammo = ((int_attr(el, "ammomax", 25000) as i16) as i32).clamp(0, 25000);
    put16(row, 0x88, ammo as u16);
    put32(row, 0x8C, int_attr(el, "timebetweenshots", 0) as u32);
    for c in &el.children {
        match hash(&c.name) {
            RELOAD => {
                put32(row, 0x94, int_attr(c, "time", 0) as u32);
                put32(row, 0x98, int_attr(c, "fasttime", 0) as u32);
                put32(row, 0x9C, int_attr(c, "crouchtime", 0) as u32);
            }
            DAMAGE => {
                put16(row, 0x82, int_attr(c, "base", 0) as u16);
                put16(row, 0x84, int_attr(c, "fps", 0) as u16);
                putf(row, 0xF0, float_attr(c, "networkplayermod", 0.0));
                putf(row, 0xF4, float_attr(c, "networkpedmod", 0.0));
            }
            AIMING => aiming(c, row),
            PROJECTILE => projectile(c, row),
            FLAGS => flags(c, row),
            PICKUP => {
                put32(row, 0x78, int_attr(c, "regentime", 0) as u32);
                put16(row, 0x80, int_attr(c, "ammoonstreet", 0) as u16);
            }
            PHYSICS => putf(row, 0x90, float_attr(c, "force", 0.0)),
            CONTROLLER => controller(c, row),
            _ => {}
        }
    }
}

