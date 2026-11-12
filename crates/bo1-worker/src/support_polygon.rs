// BO1 locomotion handoff using the horizontal hull of planted sole contacts.
use delirium::prelude::{Vec2, Vec3};

fn cross(a: Vec2, b: Vec2, c: Vec2) -> f32 {
    (b - a).perp_dot(c - a)
}

fn hull(mut points: Vec<Vec2>) -> Vec<Vec2> {
    points.sort_unstable_by(|a, b| a.x.total_cmp(&b.x).then(a.y.total_cmp(&b.y)));
    points.dedup();
    let mut boundary = Vec::with_capacity(points.len() + 1);
    for point in points.iter().copied() {
        add_corner(&mut boundary, point, 0);
    }
    let lower = boundary.len().saturating_sub(1);
    for point in points.iter().rev().skip(1).copied() {
        add_corner(&mut boundary, point, lower);
    }
    boundary.pop();
    boundary
}

fn add_corner(boundary: &mut Vec<Vec2>, point: Vec2, start: usize) {
    while boundary.len() >= start + 2 {
        let n = boundary.len();
        if cross(boundary[n - 2], boundary[n - 1], point) > 0.0 {
            break;
        }
        boundary.pop();
    }
    boundary.push(point);
}

// A point or line provides no measured footprint. Keep the existing BO1
// handoff for those cases, including sparse contacts on a stair edge.
pub fn capture_distance(contacts: [&[Vec3]; 2], com: Vec3, velocity: Vec3) -> Option<f32> {
    let mut points = Vec::new();
    let mut support_z = 0.0;
    let mut supported_feet = 0;
    for foot in contacts {
        let mut height = 0.0;
        let mut count = 0;
        for point in foot.iter().filter(|point| point.is_finite()) {
            points.push(point.truncate());
            height += point.z;
            count += 1;
        }
        if count > 0 {
            support_z += height / count as f32;
            supported_feet += 1;
        }
    }
    let boundary = hull(points);
    if boundary.len() < 3 {
        return None;
    }

    // Each planted foot contributes equally to the support height. A dense
    // triangle seam under one foot must not bias prediction on a stair.
    let height = com.z - support_z / supported_feet as f32;
    if height < 0.2 || !com.is_finite() || !velocity.is_finite() {
        return Some(f32::INFINITY);
    }
    let capture = com.truncate() + velocity.truncate() * (height / 9.81).sqrt();
    let mut inside = true;
    let mut distance = f32::INFINITY;
    for (a, b) in boundary.iter().copied().zip(boundary.iter().copied().cycle().skip(1)) {
        let edge = b - a;
        inside &= cross(a, b, capture) >= 0.0;
        let t = ((capture - a).dot(edge) / edge.length_squared()).clamp(0.0, 1.0);
        distance = distance.min(capture.distance(a + edge * t));
    }
    Some(if inside { 0.0 } else { distance })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn planted_toe_supports_forward_lean_but_not_lateral_drift() {
        let sole = [
            Vec3::new(-0.09, -0.05, 0.0),
            Vec3::new(0.18, -0.05, 0.0),
            Vec3::new(0.18, 0.05, 0.0),
            Vec3::new(-0.09, 0.05, 0.0),
        ];
        let com = Vec3::new(0.14, 0.0, 0.9);
        assert_eq!(capture_distance([&sole, &[]], com, Vec3::ZERO), Some(0.0));
        let sideways = capture_distance([&sole, &[]], com, Vec3::Y * 0.5).unwrap();
        assert!(sideways > 0.09, "capture point escaped the side of the sole");
    }

    #[test]
    fn polygon_spans_staggered_feet_on_a_step() {
        let lower = [Vec3::new(-0.3, -0.1, 0.0), Vec3::new(-0.3, 0.1, 0.0)];
        let upper = [Vec3::new(0.3, -0.1, 0.2), Vec3::new(0.3, 0.1, 0.2)];
        let com = Vec3::new(0.0, 0.0, 1.0);
        assert_eq!(capture_distance([&lower, &upper], com, Vec3::ZERO), Some(0.0));
        assert_eq!(capture_distance([&lower, &[]], com, Vec3::ZERO), None);
        assert_eq!(capture_distance([&[], &[]], com, Vec3::ZERO), None);
    }
}
