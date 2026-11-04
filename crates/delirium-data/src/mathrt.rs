fn fsel(test: f64, a: f64, b: f64) -> f64 {
    if test >= 0.0 { a } else { b }
}

pub fn floor(x: f64) -> f64 {
    let whole = (x as i64) as f64;
    let r = fsel(x - whole, whole, whole - 1.0);
    let r = fsel(crate::kv!(334) - x.abs(), r, x);
    fsel(-x.abs(), x, r)
}

pub fn ceil(x: f64) -> f64 {
    let whole = (x as i64) as f64;
    let r = fsel(whole - x, whole, whole + 1.0);
    let r = fsel(crate::kv!(334) - x.abs(), r, x);
    fsel(-x.abs(), x, r)
}

#[allow(non_snake_case)]
#[inline]
fn INV_PI() -> f64 { crate::kv!(335 f64) }
#[allow(non_snake_case)]
#[inline]
fn PI_HIGH() -> f64 { crate::kv!(336 f64) }
#[allow(non_snake_case)]
#[inline]
fn PI_LOW() -> f64 { crate::kv!(337 f64) }
#[allow(non_snake_case)]
#[inline]
fn LIMIT() -> f64 { crate::kv!(338) }
const NAN: f64 = f64::from_bits(0xFFF8_0000_0000_0000);
#[allow(non_snake_case)]
#[inline]
fn SERIES() -> [f64; 8] { [
    crate::kv!(339 f64),
    crate::kv!(340 f64),
    crate::kv!(341 f64),
    crate::kv!(342 f64),
    crate::kv!(343 f64),
    crate::kv!(344 f64),
    crate::kv!(345 f64),
    crate::kv!(346 f64),
] }

fn series(r: f64) -> f64 {
    let z = r * r;
    let mut p = SERIES()[0];
    for &c in &SERIES()[1..] {
        p = p.mul_add(z, c);
    }
    p.mul_add(z, 1.0) * r
}

pub fn sin(x: f64) -> f64 {
    let ax = x.abs();
    let sign = fsel(x, 1.0, -1.0);
    let turns = (INV_PI() * ax).round_ties_even();
    let r = -(PI_HIGH().mul_add(turns, -ax));
    let r = -(PI_LOW().mul_add(turns, -r));
    let mut v = series(r);
    if (turns as i64) & 1 != 0 {
        v = -v;
    }
    if ax.to_bits() == 0 {
        return x;
    }
    fsel(ax - LIMIT(), NAN, v * sign)
}

pub fn cos(x: f64) -> f64 {
    let ax = x.abs();
    let shifted = std::f64::consts::FRAC_PI_2 + ax;
    let turns = (INV_PI() * shifted).round_ties_even();
    let half = turns - 0.5;
    let r = -(PI_HIGH().mul_add(half, -ax));
    let r = -(PI_LOW().mul_add(half, -r));
    let mut v = series(r);
    if (turns as i64) & 1 != 0 {
        v = -v;
    }
    if ax == 0.0 {
        return 1.0;
    }
    fsel(shifted - LIMIT(), NAN, v)
}

pub fn atan2(y: f64, x: f64) -> f64 {
    #[allow(non_snake_case)]
    #[inline]
    fn PI() -> f64 { crate::kv!(347) }
    let r;
    if x == 0.0 {
        if y == 0.0 {
            if x.to_bits() >> 63 == 0 {
                return y;
            }
            return if y.to_bits() >> 63 == 0 { PI() } else { -PI() };
        }
        r = crate::kv!(348);
    } else {
        let (mut num, mut den, mut quadrant) = (y.abs(), x.abs(), 0usize);
        if num > den {
            (num, den, quadrant) = (den, num, 2);
        }
        let mut t = num / den;
        if t > crate::kv!(349) {
            quadrant += 1;
            let sqrt3: f64 = crate::kv!(350);
            t = sqrt3.mul_add(t, -1.0) / (sqrt3 + t);
        }
        let z = t * t;
        let mut p = (-crate::kv!(351 f64)).mul_add(z, -crate::kv!(352));
        let mut q = crate::kv!(353) + z;
        q = q.mul_add(z, crate::kv!(354));
        p = p.mul_add(z, -crate::kv!(355));
        q = q.mul_add(z, crate::kv!(356));
        p = p.mul_add(z, -crate::kv!(357));
        q = q.mul_add(z, crate::kv!(358));
        let mut v = p * z * t / q + t;
        if quadrant > 1 {
            v = -v;
        }
        r = [0.0, crate::kv!(359), crate::kv!(348), crate::kv!(360)][quadrant] + v;
    }
    let v = fsel(x, r, PI() - r);
    if y.to_bits() >> 63 == 0 { v } else { -v }
}

pub fn acos(x: f64) -> f64 {
    let (r, small) = arc(x.abs());
    let (lo, hi) = if small { (HALF_PI(), HALF_PI()) } else { (0.0, crate::kv!(347)) };
    if x >= 0.0 { lo - r } else { hi + r }
}

#[allow(non_snake_case)]
#[inline]
fn HALF_PI() -> f64 { crate::kv!(348) }

fn arc(a: f64) -> (f64, bool) {
    #[allow(non_snake_case)]
    #[inline]
    fn P() -> [f64; 5] { [crate::kv!(361 f64), crate::kv!(362 f64), crate::kv!(363 f64), crate::kv!(364 f64), crate::kv!(365 f64)] }
    #[allow(non_snake_case)]
    #[inline]
    fn Q() -> [f64; 5] { [crate::kv!(366 f64), crate::kv!(367 f64), crate::kv!(368 f64), crate::kv!(369 f64), crate::kv!(370 f64)] }
    let (p, q) = (P(), Q());
    let (z, s, small) = if !(a > 0.5) { (a * a, a, true) } else {
        let z = (1.0 - a) * 0.5;
        (z, -(z.sqrt() * 2.0), false)
    };
    let top = p[4].mul_add(z, p[3]).mul_add(z, p[2]).mul_add(z, p[1]).mul_add(z, p[0]);
    let bottom = (q[4] + z).mul_add(z, q[3]).mul_add(z, q[2]).mul_add(z, q[1]).mul_add(z, q[0]);
    (top * z * s / bottom + s, small)
}

pub fn asin(x: f64) -> f64 {
    if x == 0.0 {
        return x;
    }
    let (r, small) = arc(x.abs());
    let r = r + if small { 0.0 } else { HALF_PI() };
    if x >= 0.0 { r } else { -r }
}
