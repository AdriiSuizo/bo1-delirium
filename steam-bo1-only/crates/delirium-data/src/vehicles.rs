use std::collections::HashMap;

#[derive(Clone, Debug, PartialEq)]
pub struct Handling {
    pub mass: f32,
    pub drag: f32,
    pub percent_submerged: i32,
    pub buoyancy: f32,
    pub centre_of_mass: [f32; 3],
    pub drive_bias: [f32; 2],
    pub gears: u8,
    pub drive_force: f32,
    pub drive_inertia: f32,
    pub drive_max_velocity: f32,
    pub max_flat_velocity: f32,
    pub gear_ratios: Vec<f32>,
    pub brake_force: f32,
    pub brake_bias: [f32; 2],
    pub handbrake: f32,
    pub steering_lock: f32,
    pub traction_max: f32,
    pub traction_min: f32,
    pub traction_lateral: f32,
    pub traction_spring_delta_max: f32,
    pub traction_bias: [f32; 2],
    pub suspension_force: f32,
    pub suspension_comp_damp: f32,
    pub suspension_rebound_damp: f32,
    pub suspension_upper: f32,
    pub suspension_lower: f32,
    pub suspension_raise: f32,
    pub suspension_bias: [f32; 2],
    pub damage: [f32; 4],
    pub seat_offset: f32,
    pub value: i32,
    pub model_flags: u32,
    pub handling_flags: u32,
}

fn split(bias: f32) -> [f32; 2] {
    if bias < crate::kv!(49) {
        [0.0, 1.0]
    } else if bias > crate::kv!(61) {
        [1.0, 0.0]
    } else {
        [bias * 2.0, (1.0 - bias) * 2.0]
    }
}

pub fn gear_ratios(gears: u8) -> Vec<f32> {
    let n = gears.clamp(1, 7) as usize;
    let mut r = vec![-crate::kv!(20 f32) / crate::kv!(2); n + 1];
    r[n] = 1.0;
    if n >= 2 {
        r[1] = crate::kv!(20) / crate::kv!(2);
        let step = (r[1] / r[n]).powf(1.0 / (n - 1) as f32);
        for k in 2..=n {
            r[k] = r[k - 1] / step;
        }
    }
    r
}

fn lines(text: &str) -> impl Iterator<Item = &str> {
    text.lines().map(|l| l.split(['#', ';']).next().unwrap_or("").trim()).filter(|l| !l.is_empty())
}

pub fn read_handling(text: &str) -> HashMap<String, Handling> {
    lines(text)
        .filter(|l| l.starts_with(|c: char| c.is_ascii_alphabetic()))
        .filter_map(|l| {
            let t: Vec<&str> = l.split_whitespace().collect();
            let f = |i: usize| t.get(i)?.parse::<f32>().ok();
            let n = |i: usize| t.get(i)?.parse::<f32>().ok().map(|v| v as i32);
            let hex = |i: usize| u32::from_str_radix(t.get(i)?, 16).ok();
            let handling_flags = hex(35)?;
            let damp = if handling_flags & 0x2000_0000 != 0 { crate::kv!(17) } else { crate::kv!(49) };
            let gears = n(8)?.clamp(1, 7) as u8;
            let percent_submerged = n(3)?;
            let top = f(11)? / crate::kv!(250);
            let h = Handling {
                mass: f(1)?,
                drag: f(2)? * crate::kv!(44),
                percent_submerged,
                buoyancy: crate::kv!(21) / percent_submerged.max(1) as f32,
                centre_of_mass: [f(4)?, f(5)?, f(6)?],
                drive_bias: split(f(7)?),
                gears,
                drive_force: f(9)?,
                drive_inertia: f(10)?,
                drive_max_velocity: top,
                max_flat_velocity: top * crate::kv!(50),
                gear_ratios: gear_ratios(gears),
                brake_force: f(12)?,
                brake_bias: split(f(13)?),
                handbrake: f(14)?,
                steering_lock: f(15)?.to_radians(),
                traction_max: f(16)?,
                traction_min: f(17)?,
                traction_lateral: f(18)?.to_radians(),
                traction_spring_delta_max: f(19)?,
                traction_bias: split(f(20)?),
                suspension_force: f(21)?,
                suspension_comp_damp: f(22)? * damp,
                suspension_rebound_damp: f(23)? * damp,
                suspension_upper: f(24)?,
                suspension_lower: f(25)?,
                suspension_raise: f(26)?,
                suspension_bias: split(f(27)?),
                damage: [f(28)?, f(29)?, f(30)?, f(31)?],
                seat_offset: f(32)?,
                value: n(33)?,
                model_flags: hex(34)?,
                handling_flags,
            };
            Some((t[0].to_ascii_uppercase(), h))
        })
        .collect()
}

pub fn read_flying(text: &str) -> HashMap<String, [f32; 22]> {
    lines(text)
        .filter_map(|l| {
            let mut t = l.strip_prefix('$')?.split_whitespace();
            let id = t.next()?.to_ascii_uppercase();
            Some((id, t.filter_map(|v| v.parse().ok()).collect::<Vec<f32>>().try_into().ok()?))
        })
        .collect()
}

pub fn read_boat(text: &str) -> HashMap<String, [f32; 19]> {
    lines(text)
        .filter_map(|l| {
            let mut t = l.strip_prefix('%')?.split_whitespace();
            let id = t.next()?.to_ascii_uppercase();
            Some((id, t.filter_map(|v| v.parse().ok()).take(19).collect::<Vec<f32>>().try_into().ok()?))
        })
        .collect()
}

pub fn read_bike(text: &str) -> HashMap<String, [f32; 15]> {
    lines(text)
        .filter_map(|l| {
            let mut t = l.strip_prefix('!')?.split_whitespace();
            let id = t.next()?.to_ascii_uppercase();
            let mut b: [f32; 15] = t.filter_map(|v| v.parse().ok()).collect::<Vec<f32>>().try_into().ok()?;
            for i in [4, 5, 10, 11] {
                b[i] = b[i].to_radians();
            }
            Some((id, b))
        })
        .collect()
}

pub fn read_ide_dirt(text: &str) -> HashMap<String, u32> {
    let mut cars = false;
    lines(text).filter_map(|l| {
        match l {
            "cars" => cars = true,
            "end" => cars = false,
            _ if cars => {
                let t: Vec<&str> = l.split(',').map(str::trim).collect();
                let f: f32 = t.get(11)?.parse().ok()?;
                return Some((t.first()?.to_ascii_lowercase(), ((f as f64 * crate::kv!(67)) as i64 as u32).min(14)));
            }
            _ => {}
        }
        None
    }).collect()
}

pub fn read_ide_kind(text: &str) -> HashMap<String, u32> {
    let mut cars = false;
    lines(text).filter_map(|l| {
        match l {
            "cars" => cars = true,
            "end" => cars = false,
            _ if cars => {
                let t: Vec<&str> = l.split(',').map(str::trim).collect();
                let k: u32 = t.get(12)?.split_whitespace().next()?.parse().ok()?;
                return Some((t.first()?.to_ascii_lowercase(), k));
            }
            _ => {}
        }
        None
    }).collect()
}

pub fn vehicle_type(name: &str) -> Option<(i32, i32)> {
    match name {
        "bmx" => Some((6, 1)),
        "car" => Some((0, 0)),
        "bike" => Some((1, 1)),
        "boat" => Some((2, 2)),
        "train" => Some((3, 3)),
        "heli" => Some((4, 0)),
        "plane" => Some((5, 0)),
        _ => None,
    }
}

pub fn read_ide_types(text: &str) -> HashMap<String, (i32, i32)> {
    let mut cars = false;
    lines(text).filter_map(|l| {
        match l {
            "cars" => cars = true,
            "end" => cars = false,
            _ if cars => {
                let t: Vec<&str> = l.split(',').map(str::trim).collect();
                return Some((t.first()?.to_ascii_lowercase(), vehicle_type(t.get(2)?).unwrap_or((0, 0))));
            }
            _ => {}
        }
        None
    }).collect()
}

pub fn read_ide_flags(text: &str) -> HashMap<String, u32> {
    const NAMES: [(&str, u32); 27] = [
        ("smallworker", 0x1), ("big", 0x2), ("noboot", 0x4), ("onlyduringofficehours", 0x8), ("bootinfront", 0x10), ("isvan", 0x20), ("avoidturns", 0x40), ("livery", 0x80), ("liveryextra", 0x180), ("sports", 0x200),
        ("delivery", 0x400), ("noambocc", 0x800), ("onlyonhighways", 0x1000), ("tallship", 0x2000), ("truckfx", 0x4000),
        ("truckenginefx", 0x8000), ("ext_gang", 0x10000), ("ext_conv", 0x20000), ("ext_taxi", 0x40000), ("ext_rare", 0x80000),
        ("ext_requi", 0x100000), ("ext_strong", 0x200000), ("ext_script", 0x400000), ("ext_all", 0x800000),
        ("ext_sunroof", 0x1000000), ("truckfirefx", 0x2000000), ("bangerexhaustfx", 0x4000000),
    ];
    let mut cars = false;
    lines(text).filter_map(|l| {
        match l {
            "cars" => cars = true,
            "end" => cars = false,
            _ if cars => {
                let t: Vec<&str> = l.split(',').map(str::trim).collect();
                let bits = t.last()?.split_whitespace().last()?.split('+').filter_map(|f| NAMES.iter().find(|(n, _)| n.eq_ignore_ascii_case(f))).fold(0, |a, (_, b)| a | b);
                return Some((t.first()?.to_ascii_lowercase(), bits));
            }
            _ => {}
        }
        None
    }).collect()
}

pub fn start_extras(bike: bool, flags: u32, create: u32, present: impl Fn(u32) -> bool, rand: &mut impl FnMut(i32, i32) -> i32) -> u32 {
    let (mut never, mut a, mut b, mut c, mut d) = (0u32, 0u32, 0u32, 0u32, u32::MAX);
    if bike {
        (a, b, c, d) = (0xE, 0x30, 0x1C0, 0x600);
    } else if flags & 0x10000 == 0 {
        if flags & 0x20000 == 0 && flags & 0x100_0000 == 0 {
            if flags & 0x40000 == 0 {
                if flags & 0x10_0000 == 0 {
                    if flags & 0x80_0000 != 0 {
                        (never, d) = (u32::MAX, 0);
                    }
                } else {
                    (a, d) = (u32::MAX, 0);
                }
            } else {
                a = 0x1E0;
                if flags & 0x80_0000 == 0 { d = !0x1E0 } else { (d, never) = (0, 0x1E0) }
            }
        } else {
            a = if create & 0x100 != 0 { 0x1C } else if create & 0x400 != 0 { 0x2 } else { 0x1E };
            if flags & 0x80_0000 == 0 { d = !0x1E } else { (never, d) = (!a, 0) }
        }
    } else if create & 0x200 == 0 {
        if flags & 0x80_0000 == 0 {
            if flags & 0x10_0000 == 0 { d = 0x1E } else { (d, a) = (0, 0x1E) }
        } else {
            (d, never) = (0, !0x1E0);
        }
    } else {
        (a, d, never) = (0, 0, 0x1E0);
    }
    if flags & 0x40_0000 != 0 {
        (a, d) = (a & !0x1E0, d & !0x1E0);
    }
    if !bike {
        (never, a, d) = (never & !0x200, a & !0x200, d & !0x200);
    }
    let extras: Vec<u32> = (1..=10).filter(|&n| present(n)).collect();
    if extras.is_empty() {
        return 0;
    }
    let count = |m: u32| extras.iter().filter(|&&n| m & 1 << n != 0).count() as i32;
    let (ca, cb, cc, cd) = (count(a), count(b), count(c), count(d));
    let mut pa = if ca > 0 { rand(1, ca + 1) } else { -1 };
    let mut pb = if cb > 0 { rand(1, cb + 1) } else { -1 };
    let mut pc = if cc > 0 { rand(1, cc + 1) } else { -1 };
    let mut pd = -1;
    if cd > 0 {
        let m = if flags & 0x80000 != 0 { (cd & 0x3FFF_FFFF) << 2 } else { cd };
        pd = rand(0, m + 1);
        if cd < pd {
            pd = 0;
        }
    }
    let mut off = 0;
    for n in extras {
        let bit = 1 << n;
        if never & bit != 0 {
            continue;
        }
        let pick = if a & bit != 0 { Some(&mut pa) } else if b & bit != 0 { Some(&mut pb) } else if c & bit != 0 { Some(&mut pc) } else if d & bit != 0 { Some(&mut pd) } else { None };
        if let Some(p) = pick {
            *p -= 1;
            if *p == 0 {
                continue;
            }
        }
        off |= bit;
    }
    off
}

pub fn read_ide_livery(text: &str) -> HashMap<String, (bool, bool)> {
    let mut cars = false;
    lines(text).filter_map(|l| {
        match l {
            "cars" => cars = true,
            "end" => cars = false,
            _ if cars => {
                let t: Vec<&str> = l.split(',').map(str::trim).collect();
                let flags: Vec<&str> = t.last()?.split('+').collect();
                let extra = flags.contains(&"liveryextra");
                let livery = extra || flags.contains(&"livery");
                return livery.then(|| (t.first().unwrap().to_ascii_lowercase(), (livery, extra)));
            }
            _ => {}
        }
        None
    }).collect()
}

pub fn livery_textures(sign_1: &str, has: impl Fn(&str) -> bool) -> Vec<String> {
    let Some(prefix) = sign_1.strip_suffix("_sign_1") else { return vec![] };
    let prefix = prefix.rsplit('/').next().unwrap_or(prefix);
    (1..=4).map(|n| format!("{prefix}_sign_{n}")).take_while(|n| has(n)).collect()
}

pub fn rand_range(a: i32, b: i32, rand: u32) -> i32 {
    ((rand as u16 as f32 * crate::kv!(382 f32)) * (b - a) as f32) as i32 + a
}

pub fn pick_livery(count: u8, extra: bool, off: u32, mut rand: impl FnMut() -> u32) -> i32 {
    let last = count as i8 as i32 - 1;
    if !extra {
        return rand_range(0, count as i8 as i32, rand());
    }
    let first_clear = if off == 0 { -1 } else { (0..8).find(|b| off & 1 << b == 0).map_or(-1, |b| b as i32) };
    if first_clear == -1 {
        return last;
    }
    if rand_range(0, 3, rand()) & 1 != 0 { last } else { first_clear }
}

pub fn new_dirt(min: u32, rand: u32) -> f32 {
    let span = 16 - min.min(15);
    (rand % span + min) as f32
}

pub fn dirt_step(dirt: f32, velocity: [f32; 3], step: f32) -> f32 {
    let speed = velocity[0].mul_add(velocity[0], velocity[1] * velocity[1]).sqrt();
    let k = crate::kv!(387 f32);
    let d = ((speed * k) as f64 * step as f64 + dirt as f64) as f32;
    if d - crate::kv!(19) >= 0.0 { crate::kv!(19) } else { d }
}

pub fn read_ide_game_names(text: &str) -> HashMap<String, String> {
    let mut cars = false;
    lines(text).filter_map(|l| {
        match l {
            "cars" => cars = true,
            "end" => cars = false,
            _ if cars => {
                let t: Vec<&str> = l.split(',').map(str::trim).collect();
                return Some((t.first()?.to_ascii_lowercase(), t.get(4)?.to_string()));
            }
            _ => {}
        }
        None
    }).collect()
}

pub fn read_ide_handling(text: &str) -> HashMap<String, (String, [f32; 2])> {
    let mut out = HashMap::new();
    let mut cars = false;
    for l in lines(text) {
        match l {
            "cars" => cars = true,
            "end" => cars = false,
            _ if cars => {
                let t: Vec<&str> = l.split(',').map(str::trim).collect();
                if let [model, _, _, handling, ..] = t.as_slice() {
                    let rim = |i: usize| t.get(i).and_then(|v| v.parse().ok()).unwrap_or(0.0);
                    out.insert(model.to_ascii_lowercase(), (handling.to_ascii_uppercase(), [rim(9), rim(10)]));
                }
            }
            _ => {}
        }
    }
    out
}

pub struct CarColours {
    pub table: Vec<[u8; 3]>,
    pub cars: HashMap<String, Vec<Vec<usize>>>,
}

impl CarColours {
    pub fn combo(&self, model: &str, k: usize) -> Option<Vec<[u8; 3]>> {
        let combos = self.cars.get(&model.to_ascii_lowercase()).filter(|c| !c.is_empty())?;
        combos[k % combos.len()].iter().map(|&i| self.table.get(i).copied()).collect()
    }
}

pub fn read_carcols(text: &str) -> CarColours {
    let (mut table, mut cars) = (vec![], HashMap::new());
    let mut section = "";
    for l in lines(text) {
        match l {
            "col" | "car3" | "car4" | "end" => section = l,
            _ => {
                let t: Vec<&str> = l.split([',', ' ', '\t']).filter(|s| !s.is_empty()).collect();
                match section {
                    "col" => {
                        if let [r, g, b, ..] = t.as_slice() {
                            if let (Ok(r), Ok(g), Ok(b)) = (r.parse(), g.parse(), b.parse()) {
                                table.push([r, g, b]);
                            }
                        }
                    }
                    "car3" | "car4" => {
                        let n = if section == "car3" { 3 } else { 4 };
                        let Some((model, rest)) = t.split_first() else { continue };
                        let ids: Vec<usize> = rest.iter().filter_map(|s| s.parse().ok()).collect();
                        let combos = ids.chunks_exact(n).map(<[usize]>::to_vec).collect();
                        cars.insert(model.to_ascii_lowercase(), combos);
                    }
                    _ => {}
                }
            }
        }
    }
    CarColours { table, cars }
}

pub fn read_materials(text: &str) -> Vec<[f32; 2]> {
    text_rows(text)
        .map(|l| {
            let f: Vec<f32> = l.split_whitespace().skip(3).take(5).map(|v| v.parse().unwrap_or(f32::NAN)).collect();
            match f[..] {
                [_, _, _, grip, wet] if grip.is_finite() && wet.is_finite() => [grip, wet],
                _ => [1.0, 0.0],
            }
        })
        .collect()
}

pub fn read_material_names(text: &str) -> Vec<String> {
    text_rows(text).map(|l| l.split_whitespace().next().unwrap_or_default().to_string()).collect()
}

pub fn read_friction(text: &str) -> Vec<[f32; 2]> {
    text_rows(text)
        .map(|l| {
            let f: Vec<f32> = l.split_whitespace().skip(3).take(2).filter_map(|v| v.parse().ok()).collect();
            if f.len() == 2 { [f[0], f[1]] } else { [1.0, crate::kv!(49)] }
        })
        .collect()
}

pub fn read_burn(text: &str) -> Vec<[f32; 3]> {
    text_rows(text)
        .map(|l| {
            let f: Vec<f32> = l.split_whitespace().skip(10).take(3).filter_map(|v| v.parse().ok()).collect();
            if f.len() == 3 { [f[0], f[1], f[2]] } else { [0.0; 3] }
        })
        .collect()
}

pub fn material_row(text: &str, name: &str) -> Option<usize> {
    text_rows(text).position(|l| l.split_whitespace().next() == Some(name))
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WheelFx {
    pub skid: f32,
    pub friction: [f32; 2],
    pub group: u16,
}

#[allow(non_snake_case)]
#[inline]
pub fn NO_FX() -> WheelFx { WheelFx { skid: crate::kv!(388), friction: [crate::kv!(388); 2], group: 0 } }

pub fn read_wheel_fx(materials: &str, vehiclefx: &str) -> Vec<[WheelFx; 4]> {
    let mut groups = HashMap::<String, [WheelFx; 4]>::new();
    let (mut section, mut row) = (None, 0);
    for l in vehiclefx.lines().map(str::trim) {
        if let Some(name) = l.strip_prefix("VEHFX_INFO_").filter(|n| n.ends_with("START")) {
            section = ["START", "BURST_START", "WET_START", "BURST_WET_START"].iter().position(|s| name == *s);
            row = 0;
        } else if l.starts_with("VEHFX_INFO_") {
            section = None;
        } else if let (Some(k), true) = (section, l.starts_with(|c: char| c.is_ascii_alphabetic())) {
            let f: Vec<&str> = l.split_whitespace().collect();
            let num = |i: usize| f.get(i).and_then(|v| v.parse().ok()).unwrap_or(crate::kv!(388));
            let fx = WheelFx { skid: num(1), friction: [num(11), num(12)], group: row };
            groups.entry(f[0].to_ascii_uppercase()).or_insert([NO_FX(); 4])[k] = fx;
            row += 1;
        }
    }
    text_rows(materials)
        .map(|l| l.split_whitespace().nth(1).and_then(|g| groups.get(&g.to_ascii_uppercase())).copied().unwrap_or([NO_FX(); 4]))
        .collect()
}

fn text_rows(text: &str) -> impl Iterator<Item = &str> {
    text.lines().filter(|l| l.starts_with(|c: char| c.is_ascii_alphabetic()))
}

