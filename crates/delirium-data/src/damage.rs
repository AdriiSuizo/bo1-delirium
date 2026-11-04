#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Health {
    pub body: f32,
    pub engine: f32,
    pub petrol: f32,
    pub stalled: bool,
    pub wrecked: bool,
    pub blow: bool,
    pub no_damage: bool,
    pub tyres_proof: bool,
}

impl Default for Health {
    fn default() -> Self {
        Health { body: crate::kv!(53), engine: crate::kv!(53), petrol: crate::kv!(53), stalled: false, wrecked: false, blow: false, no_damage: false, tyres_proof: false }
    }
}

pub fn crash_amount(j: f32, mass: f32, mult: f32) -> f32 {
    crate::kv!(20) * mult * j / mass
}

pub fn crash_shocks(j: f32, mass: f32, player_driving: bool, by_ped: bool) -> bool {
    let scale = if player_driving { crate::kv!(2) } else { crate::kv!(7) } * if by_ped { crate::kv!(20) } else { 1.0 };
    j / mass * scale > crate::kv!(20) || j * scale > crate::kv!(298)
}

pub fn crash_shake(d: f32) -> Option<(f32, f32)> {
    (d >= crate::kv!(58)).then(|| ((d * crate::kv!(36)).clamp(crate::kv!(35), crate::kv!(10)), d * crate::kv!(271)))
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Panel {
    Door,
    Bonnet,
    Boot,
}

#[allow(non_snake_case)]
#[inline]
pub fn PANEL_MIN() -> f32 { crate::kv!(26) }

pub fn panel_box(panel: Panel, lo: [f32; 3], hi: [f32; 3]) -> ([f32; 3], [f32; 3]) {
    let by = match panel {
        Panel::Door => [-crate::kv!(34), -crate::kv!(34), crate::kv!(49)],
        Panel::Bonnet => [-crate::kv!(34), -crate::kv!(34), -crate::kv!(60)],
        Panel::Boot => [-crate::kv!(34), -crate::kv!(17), -crate::kv!(60)],
    };
    (std::array::from_fn(|k| lo[k] + by[k]), hi.map(|v| v + crate::kv!(34)))
}

pub fn panel_frees(panel: Panel, amount: f32, normal_x: f32, side_x: f32, random: &mut impl FnMut() -> f32, inside: impl FnOnce() -> bool) -> bool {
    if amount <= PANEL_MIN() {
        return false;
    }
    let side = if side_x > 0.5 { 1.0 } else if side_x < -0.5 { -1.0 } else { 0.0 };
    if normal_x * side * amount < -crate::kv!(299) {
        return random() < 0.5;
    }
    random() < if panel == Panel::Door { 0.5 } else { crate::kv!(17) } && inside()
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Lamps(pub [bool; 14]);

#[allow(non_snake_case)]
#[inline]
pub fn LAMP_REACH2() -> f32 { crate::kv!(300 f32) }

impl Lamps {
    pub fn smash(&mut self, amount: f32, mut dist2: impl FnMut(usize) -> Option<f32>) -> Vec<usize> {
        let mut out = vec![];
        if amount < 0.5 {
            return out;
        }
        for n in 0..16 {
            let slot = n.min(13);
            if !self.0[slot] && dist2(n).is_some_and(|d| d < LAMP_REACH2()) {
                self.0[slot] = true;
                out.push(n);
            }
        }
        out
    }
}

#[allow(non_snake_case)]
#[inline]
fn FIRE_STEPS() -> [f32; 8] { [-crate::kv!(301), -crate::kv!(302), -crate::kv!(303), -crate::kv!(53), -crate::kv!(304), -crate::kv!(305), -crate::kv!(45), -crate::kv!(306)] }

impl Health {
    pub fn collision(&mut self, j: f32, mass: f32, mult: [f32; 4], up: f32, engine_end: bool, random: &mut impl FnMut() -> f32) -> Option<f32> {
        let d = crash_amount(j, mass, mult[0]);
        if up > crate::kv!(63) || d < crate::kv!(20) || self.wrecked || self.no_damage {
            return None;
        }
        let d = d - crate::kv!(20);
        self.take_body(d);
        if engine_end {
            self.engine_damage(d * mult[3], 5, 0, random);
        }
        Some(d)
    }

    pub fn weapon(&mut self, amount: f32, mult: [f32; 4], engine_end: bool, random: &mut impl FnMut() -> f32) {
        if self.wrecked || self.no_damage {
            return;
        }
        let d = amount * mult[1];
        self.take_body(d);
        if engine_end {
            self.engine_damage(d * mult[3], 3, 0, random);
        }
    }

    fn take_body(&mut self, d: f32) {
        if 0.0 < self.body {
            self.body = if d < self.body { self.body + -d } else { 0.0 };
        }
    }

    pub fn engine_damage(&mut self, amount: f32, kind: i32, force: u32, random: &mut impl FnMut() -> f32) {
        let was = self.engine;
        if !(0.0 <= was) {
            return;
        }
        self.engine = was - amount;
        if !(self.engine <= 0.0) {
            return;
        }
        self.engine = crate::kv!(32);
        let chance = if (kind.wrapping_sub(2) as u32) < 3 { 0.5 } else { crate::kv!(46) };
        if !(chance < random() || force & 0xFF != 0) {
            return;
        }
        if kind != 5 || 0.5 <= random() {
            self.engine = -1.0;
        } else {
            (self.engine, self.stalled) = (0.0, true);
        }
    }

    pub fn explosion(&mut self, amount: f32, mult: [f32; 4], by_player: bool, random: &mut impl FnMut() -> f32) {
        if self.wrecked || self.no_damage {
            return;
        }
        let d = amount * crate::kv!(2) * mult[1];
        self.body = (self.body - d).max(0.0);
        self.engine_damage(d, 3, 0, random);
        if self.petrol > -crate::kv!(53) {
            let p = self.petrol - if by_player { d * 2.0 } else { d };
            if p < 0.0 { self.light_petrol() } else { self.petrol = p }
        }
    }

    pub fn light_petrol(&mut self) {
        if self.petrol >= 0.0 {
            self.petrol = -1.0;
        }
    }

    pub fn put_out(&mut self) {
        self.body = self.body.max(crate::kv!(307));
        if (self.engine < 0.0 && self.engine > -crate::kv!(163)) || self.petrol_fire() {
            (self.engine, self.petrol) = (-crate::kv!(163), 1.0);
        }
    }

    pub fn update(&mut self, dt: f32, burn: f32, forward: f32, range: &mut impl FnMut(f32, f32) -> f32) -> bool {
        if self.wrecked {
            return false;
        }
        if std::mem::take(&mut self.blow) && !self.no_damage {
            (self.petrol, self.wrecked, self.stalled, self.body) = (-crate::kv!(53), true, true, 0.0);
            return true;
        }
        if self.engine < 0.0 && self.engine > -crate::kv!(163) {
            let rate = range(crate::kv!(55), crate::kv!(21));
            let was = self.engine;
            let e = -dt.mul_add(rate, -was);
            self.engine = if e < -crate::kv!(163) { -crate::kv!(163) } else { e };
            for (k, &t) in FIRE_STEPS().iter().enumerate() {
                if self.engine < t && t <= was && (-forward.mul_add(crate::kv!(2), -crate::kv!(152)) < rate || k == 7) {
                    self.light_petrol();
                }
            }
        }
        if self.engine <= -crate::kv!(163) {
            self.stalled = true;
        }
        if self.petrol < 0.0 && self.petrol > -crate::kv!(53) {
            self.petrol -= (crate::kv!(128) + crate::kv!(247) * burn) * dt;
            if self.petrol <= -crate::kv!(53) {
                (self.petrol, self.wrecked, self.stalled, self.body) = (-crate::kv!(53), true, true, 0.0);
                return true;
            }
        }
        false
    }

    pub fn smoke(&self) -> f32 {
        if 0.0 < self.engine { -self.engine.mul_add(crate::kv!(308), -1.0) } else { 1.0 }
    }

    pub fn fire(&self) -> f32 {
        let e = self.engine;
        if !(e < 0.0) {
            0.0
        } else if -crate::kv!(11) < e {
            e * crate::kv!(309 f32)
        } else if e < -crate::kv!(310) {
            0.0
        } else if e < -crate::kv!(311) {
            -(e - -crate::kv!(310)).mul_add(crate::kv!(312), -1.0)
        } else {
            1.0
        }
    }

    pub fn tyre_hit(&mut self, tyre: &mut f32, amount: f32) -> bool {
        if self.tyres_proof {
            return false;
        }
        let burst = *tyre == crate::kv!(53);
        let t = *tyre;
        if t <= amount + crate::kv!(10) {
            if 0.0 < t {
                *tyre = crate::kv!(313 f32);
            }
        } else {
            *tyre = t - amount;
        }
        if crate::kv!(53) <= self.body {
            self.body += -1.0;
        }
        burst
    }

    pub fn on_fire(&self) -> bool {
        let (e, p) = (self.engine, self.petrol);
        !(0.0 <= e || e <= -crate::kv!(163)) || !(p <= -crate::kv!(53) || 0.0 <= p)
    }

    pub fn fix(&mut self) {
        (self.body, self.engine, self.petrol) = (crate::kv!(53), crate::kv!(53), crate::kv!(53));
    }

    pub fn petrol_fire(&self) -> bool {
        self.petrol < 0.0 && self.petrol > -crate::kv!(53)
    }
}

pub fn wreck_keep_chance(dist: f32) -> f32 {
    if dist <= crate::kv!(87) {
        if crate::kv!(299) < dist { crate::kv!(60) } else { 0.0 }
    } else {
        crate::kv!(15)
    }
}

pub fn wreck_fire_chance(kind: i32) -> f32 {
    match kind {
        4 => crate::kv!(13),
        0 => crate::kv!(12),
        _ => 0.0,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WreckPart {
    Stays,
    Thrown,
    Gone,
}

pub fn wreck_part(consider: f32, keep: f32, rand: &mut impl FnMut() -> i32) -> WreckPart {
    let unit = |r: i32| (r as f64 * crate::kv!(314)) as f32;
    if !(unit(rand()) < consider) {
        return WreckPart::Stays;
    }
    if keep <= unit(rand()) { WreckPart::Thrown } else { WreckPart::Gone }
}

pub fn wreck_part_fire(bits: u32, chance: f32) -> bool {
    ((((bits & 0x7F_FFFF) as f64) * crate::kv!(315)) as f32) < chance
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Explosion {
    pub damage: [f32; 2],
    pub radius: f32,
    pub speed: f32,
    pub decay: f32,
    pub force: f32,
    pub ragdoll: f32,
    pub fires: [f32; 4],
    pub fx: Option<String>,
    pub fx_air: Option<String>,
    pub fx_scale: f32,
    pub light: [f32; 6],
    pub shake: f32,
    pub weapon: String,
    pub directed: [f32; 2],
    pub infinite: bool,
    pub dir: Option<[f32; 3]>,
    pub held: f32,
    pub row: usize,
}

impl Explosion {
    pub fn push(&self, area: f32, closing: f32, mass: f32) -> f32 {
        if closing <= 0.0 { 0.0 } else { (self.force * area * closing * closing * crate::kv!(34)).min(mass * crate::kv!(52)) }
    }

    pub fn ragdoll_push(&self, area: f32, closing: f32, mass: f32) -> f32 {
        if closing <= 0.0 { 0.0 } else { (self.force * area * closing * closing * crate::kv!(34) * self.ragdoll).min(mass * crate::kv!(82)) }
    }

    pub fn in_air(self, air: bool) -> Explosion {
        if air { Explosion { fx: self.fx_air.clone(), ..self } } else { self }
    }

    pub fn damage_at(&self, r: f32) -> f32 {
        if r >= self.radius { 0.0 } else { self.damage[0] + (self.damage[1] - self.damage[0]) * r / self.radius }
    }
}

pub fn read_explosions(text: &str) -> std::collections::HashMap<String, Explosion> {
    let mut on = false;
    let mut out = std::collections::HashMap::new();
    let mut row = 0;
    for line in text.lines().map(str::trim) {
        match line {
            "EXPLOSIONFX_TABLE_START" => on = true,
            "EXPLOSIONFX_TABLE_END" => on = false,
            _ if on && !line.is_empty() && !line.starts_with('#') => {
                let f: Vec<&str> = line.split_whitespace().collect();
                let n = |i: usize| f.get(i).and_then(|s| s.parse::<f32>().ok());
                if let (Some(c), Some(e), Some(r), Some(force)) = (n(1), n(2), n(5), n(8)) {
                    let fires = [15, 16, 17, 18].map(|i| n(i).unwrap_or(0.0));
                    let light = [22, 23, 24, 25, 26, 27].map(|i| n(i).unwrap_or(0.0));
                    out.insert(f[0].to_string(), Explosion { damage: [c, e], radius: r, speed: n(6).unwrap_or(crate::kv!(52)), decay: n(7).unwrap_or(-1.0), force, ragdoll: n(9).unwrap_or(0.0), fires, fx: f.get(12).map(|s| s.to_string()), fx_air: f.get(13).map(|s| s.to_string()),
                                                             fx_scale: n(14).unwrap_or(1.0), light, shake: n(19).unwrap_or(0.0), weapon: f.get(20).map(|s| s.to_string()).unwrap_or_default(),
                                                             directed: [n(10).unwrap_or(0.0), n(11).unwrap_or(0.0)], infinite: n(21).is_some_and(|v| v != 0.0), dir: None, held: 0.0, row });
                    row += 1;
                }
            }
            _ => {}
        }
    }
    out
}
